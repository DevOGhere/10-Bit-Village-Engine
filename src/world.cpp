#include "engine/world.h"
#include "cognition/llama_bridge.h" // LlamaBridge, Cognition
#include "cognition/degradation.h"  // hearsay_hop — degradation + two-segment retell + forced novelty
#include "cognition/coinage.h"      // coined_terms — coinage harvest
#include "infra/db.h"               // Database::persist_memory
#include <sstream>
#include <cstdlib> // std::abs
#include <iterator>

namespace tbv {

// ============================================================================
// tick() — three stages: DISPATCH (one cognition event) -> APPLY (fold back) ->
// PHYSICS (unchanged from Phase 0). Cognition only runs when a bridge is attached;
// the headless Phase 0 path is therefore byte-identical to before.
// ============================================================================
void WorldState::tick() {
    // ---- A. DISPATCH: exactly ONE villager does ONE cognition event (LOCK 1). ----
    if (bridge) {
        VillagerID v = (VillagerID)(current_tick % MAX_VILLAGERS);
        dispatch_cognition(v);
    }

    // ---- B. APPLY: fold back tasks due this tick, in strict total order. ----
    std::vector<DeferredTask> tasks_to_apply;
    auto ready_it = std::stable_partition(async_queue.begin(), async_queue.end(),
        [this](const DeferredTask& t) { return t.fold_tick > current_tick; });
    tasks_to_apply.assign(std::make_move_iterator(ready_it),
                          std::make_move_iterator(async_queue.end()));
    async_queue.erase(ready_it, async_queue.end());
    std::sort(tasks_to_apply.begin(), tasks_to_apply.end());
    for (const auto& task : tasks_to_apply) apply_task(task);

    // ---- C. PHYSICS: hunger decay (UNCHANGED — keeps Phase 0 hashes byte-identical). ----
    for (uint32_t i = 0; i < MAX_VILLAGERS; ++i) {
        int32_t decay = pcg32_random_bounded_r(&pcg32_physics_state[i], 50);
        needs[i].hunger -= decay;
        if (needs[i].hunger < 0) needs[i].hunger = 0;
    }

    current_tick++;
}

// ============================================================================
// dispatch_cognition — pick the single event for villager v this tick and resolve
// it inline (strict sequential per Gemini [052]). Priority (R2-resolution):
//   1 survival reflex  2 hearsay(neighbour)  3 dream(lonely+cooldown)  4 action
// ============================================================================
void WorldState::dispatch_cognition(VillagerID v) {
    std::vector<VillagerID> nbrs = neighbours(v);

    Env env;
    env.holding_food = holding_food[v];
    env.food_in_reach = holding_food[v];      // no ground-food model in v1
    env.neighbour_present = !nbrs.empty();

    // Cognition seed for this event (consumes the per-villager cognition stream).
    uint32_t r = pcg32_random_r(&pcg32_cognition_state[v]);
    uint64_t cseed = splitmix64_avalanche(((uint64_t)r << 16) ^ ((uint64_t)v << 32) ^ current_tick);

    bool survival = (needs[v].hunger <= HUNGER_CRIT && holding_food[v]);
    // Dream when cooldown has passed AND the villager is lonely (no neighbour) OR a 1-in-6
    // cognition roll hits — so dreams surface even on the dense grid, without starving hearsay.
    bool can_dream   = (current_tick > last_dream_tick[v] + 100) && (nbrs.empty() || (r % 6 == 0));
    bool can_hearsay = !nbrs.empty();

    DeferredTask t;
    t.villager_id = v;
    t.task_seq_id = task_seq_counter++;
    t.birth_tick  = current_tick;
    t.mem_id      = next_mem_id++;

    // ---- 2. DREAM (cooldown + lonely-or-roll) — checked before hearsay so it isn't starved. ----
    if (!survival && can_dream) {
        std::vector<std::string> frags;
        const auto& mems = stores[v].all();
        for (size_t k = mems.size(); k-- > 0 && frags.size() < 3; ) frags.push_back(mems[k].text);
        if (!frags.empty()) {
            t.kind       = TaskKind::DREAM;
            t.mtype      = MemType::DREAM;
            t.target     = v;
            t.fold_tick  = current_tick + 20;
            t.text       = bridge->dream(v, frags, cseed);
            t.importance = importance(MemType::DREAM, 0, lowest_need(v), t.mem_id, v, current_tick);
            last_dream_tick[v] = current_tick;
            async_queue.push_back(std::move(t));
            return;
        }
        // nothing to dream about yet -> fall through
    }
    // ---- 3. HEARSAY ----
    if (!survival && can_hearsay) {
        // R1: draw always, index into the ascending-id adjacency list.
        uint32_t pick = (uint32_t)pcg32_random_bounded_r(&pcg32_cognition_state[v], (int32_t)nbrs.size());
        VillagerID src = nbrs[pick];
        const MemoryEntry* m = stores[src].most_salient(current_tick);
        if (m) {
            // MSG_062 §2: degrade the heard memory -> two-segment retell -> forced novelty.
            // hearsay_hop is the shared engine/gate path (no logic the gate can't see).
            RetellResult rr = hearsay_hop(*bridge, v, *m, genomes[v], current_tick, cseed);
            t.kind         = TaskKind::HEARSAY;
            t.mtype        = MemType::HEARSAY;
            t.target       = src;                          // actor_id = source of the gossip
            t.source_depth = (uint8_t)(m->source_depth + 1);
            t.fold_tick    = current_tick + 5;
            t.text         = rr.out_text;                  // rr.fields / distortion_injected -> HearsayChain (Step 4)
            t.importance   = importance(MemType::HEARSAY, t.source_depth, lowest_need(v),
                                        t.mem_id, v, current_tick);
            async_queue.push_back(std::move(t));
            return;
        }
        // neighbour has nothing to say yet -> fall through to ACTION
    }

    // ---- 4. ACTION (default + survival path): free thought -> intent -> hybrid resolver. ----
    PerceptionContext ctx = build_perception(v, nbrs, env);
    Cognition c = bridge->infer(v, ctx, cseed);
    t.kind   = TaskKind::ACTION;
    t.mtype  = MemType::EXPERIENCE;
    t.verb   = c.verb;
    t.target = v;
    if ((c.verb == Verb::GIVE || c.verb == Verb::SPEAK) && !nbrs.empty()) {
        uint32_t pick = (uint32_t)pcg32_random_bounded_r(&pcg32_cognition_state[v], (int32_t)nbrs.size());
        t.target = nbrs[pick];
    }
    t.fold_tick  = current_tick + 5;
    t.text       = c.thought;
    t.importance = importance(MemType::EXPERIENCE, 0, lowest_need(v), t.mem_id, v, current_tick);
    async_queue.push_back(std::move(t));
}

// ============================================================================
// apply_task — physical effect (ACTION) + store the memory + persist it.
// ============================================================================
void WorldState::apply_task(const DeferredTask& t) {
    if (t.kind == TaskKind::ACTION) {
        switch (t.verb) {
            case Verb::EAT:
                if (holding_food[t.villager_id]) {
                    needs[t.villager_id].hunger =
                        std::min(FIXED_POINT_ONE, needs[t.villager_id].hunger + 40000);
                    holding_food[t.villager_id] = false;
                }
                break;
            case Verb::MOVE_TO: {
                int dir = pcg32_random_bounded_r(&pcg32_physics_state[t.villager_id], 8);
                static const int dx[8] = {-1, -1, -1,  0, 0,  1, 1, 1};
                static const int dy[8] = {-1,  0,  1, -1, 1, -1, 0, 1};
                int nx = (int)pos_x[t.villager_id] + dx[dir];
                int ny = (int)pos_y[t.villager_id] + dy[dir];
                nx = std::min(std::max(nx, 0), GRID_W - 1);
                ny = std::min(std::max(ny, 0), GRID_H - 1);
                pos_x[t.villager_id] = (uint16_t)nx;
                pos_y[t.villager_id] = (uint16_t)ny;
                break;
            }
            case Verb::GIVE:
                if (holding_food[t.villager_id] && t.target != t.villager_id) {
                    holding_food[t.villager_id] = false;
                    holding_food[t.target] = true;
                    needs[t.villager_id].social = std::min(FIXED_POINT_ONE, needs[t.villager_id].social + 20000);
                    needs[t.target].social      = std::min(FIXED_POINT_ONE, needs[t.target].social + 20000);
                }
                break;
            case Verb::SPEAK:
                needs[t.villager_id].social = std::min(FIXED_POINT_ONE, needs[t.villager_id].social + 15000);
                if (t.target != t.villager_id)
                    needs[t.target].social = std::min(FIXED_POINT_ONE, needs[t.target].social + 10000);
                break;
            case Verb::WAIT:
            default: break;
        }
    }

    // Store in the actor's own store (for HEARSAY, the listener v remembers the gossip).
    MemoryEntry e;
    e.mem_id       = t.mem_id;
    e.tick         = t.birth_tick;
    e.actor_id     = (t.kind == TaskKind::HEARSAY) ? t.target : t.villager_id;
    e.importance   = t.importance;
    e.type         = t.mtype;
    e.source_depth = t.source_depth;
    e.text         = t.text;
    stores[t.villager_id].add(e, current_tick);
    if (db) db->persist_memory(run_id, t.villager_id, e);

    // ---- Coinage harvest: flag free-generation tokens absent from the system dictionary.
    // First coiner wins; the term then rides hearsay/dream/action text naturally. ----
    for (const auto& term : coined_terms(t.text)) {
        if (coined_words.insert(term).second && db)
            db->register_coinage(run_id, term, t.villager_id, t.birth_tick);
    }
}

// ============================================================================
// neighbours — Chebyshev radius 1 (8-neighbourhood), ascending villager_id.
// ============================================================================
std::vector<VillagerID> WorldState::neighbours(VillagerID v) const {
    std::vector<VillagerID> r;
    int vx = pos_x[v], vy = pos_y[v];
    for (uint32_t i = 0; i < MAX_VILLAGERS; ++i) {
        if (i == v) continue;
        int dx = std::abs((int)pos_x[i] - vx);
        int dy = std::abs((int)pos_y[i] - vy);
        if (std::max(dx, dy) <= 1) r.push_back(i);
    }
    return r;
}

// ============================================================================
// build_perception — render real state into a grounded 2nd-person situation.
// ============================================================================
PerceptionContext WorldState::build_perception(VillagerID v,
                                               const std::vector<VillagerID>& nbrs,
                                               const Env& env) const {
    PerceptionContext ctx;
    ctx.needs = needs[v];
    ctx.env = env;

    const Genome& g = genomes[v];
    std::ostringstream s;
    s << "You are a thronglet living in the village. ";
    if (g.suspicion >= 2)   s << "You are wary, quick to suspect hidden motives. ";
    if (g.curiosity >= 2)   s << "You are endlessly curious about the world. ";
    s << (g.sociability >= 2 ? "You crave the company of others. " : "You prefer to keep to yourself. ");
    if (g.generosity >= 2)  s << "You are openhanded and giving. ";
    if (g.aggression >= 2)  s << "You have a fierce, short temper. ";
    if (needs[v].hunger < 30000)      s << "You are very hungry. ";
    else if (needs[v].hunger < 60000) s << "You feel peckish. ";
    if (needs[v].social < 30000)      s << "You feel painfully lonely. ";
    if (needs[v].safety < 30000)      s << "You feel afraid and on edge. ";
    if (env.holding_food)             s << "You are holding some food. ";
    if (!nbrs.empty()) {
        s << "Nearby stand " << nbrs.size() << " other thronglet" << (nbrs.size() > 1 ? "s" : "") << " (";
        for (size_t k = 0; k < nbrs.size(); ++k) { if (k) s << ", "; s << "villager " << nbrs[k]; }
        s << "). ";
    } else {
        s << "You are alone here. ";
    }
    s << "Think about your situation.";
    ctx.situation = s.str();
    return ctx;
}

} // namespace tbv
