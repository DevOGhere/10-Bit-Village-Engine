#include "engine/world.h"
#include "cognition/llama_bridge.h" // LlamaBridge, Cognition
#include "cognition/degradation.h"  // hearsay_hop, degrade, content_word_delta
#include "cognition/coinage.h"      // coined_terms — coinage harvest
#include "infra/db.h"               // Database::persist_memory + Step 4 logging
#include <sstream>
#include <cstdlib> // std::abs
#include <cstdio>  // snprintf
#include <iterator>

namespace tbv {

// Word count, used as CognitionLog's token_count (a rough proxy, not a real LLM tokenizer count
// — fine for instrumentation, this isn't fed back into anything determinism-critical).
static int word_count(const std::string& s) {
    std::istringstream iss(s);
    std::string w; int n = 0;
    while (iss >> w) n++;
    return n;
}

static std::string hex64(uint64_t h) {
    char buf[20]; snprintf(buf, sizeof(buf), "%016llx", (unsigned long long)h);
    return buf;
}

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

    // §B1 GROUNDED_FLOOR: every GROUNDED_FLOOR_MODth cognition turn is forced down the
    // ACTION path (25% at MOD=4), so firsthand experience isn't starved by dream/hearsay
    // priority on the dense grid. Dispatch ordinal k derives from current_tick (this
    // villager dispatches at ticks v, v+100, v+200, ... -- k = 0, 1, 2, ...), not from any
    // RNG draw, so it doesn't touch stream consumption order. Survival still overrides it.
    // (42% was considered, for Hitchhiker's sake. The towel stays packed. — Oji, 07-09)
    const uint64_t k = (current_tick - v) / MAX_VILLAGERS;
    const bool force_action = ((k % GROUNDED_FLOOR_MOD) == GROUNDED_FLOOR_MOD - 1);

    // Dream when cooldown has passed AND the villager is lonely (no neighbour) OR a 1-in-6
    // cognition roll hits — so dreams surface even on the dense grid, without starving hearsay.
    bool can_dream   = !force_action && (current_tick > last_dream_tick[v] + 100) && (nbrs.empty() || (r % 6 == 0));
    bool can_hearsay = !force_action && !nbrs.empty();

    DeferredTask t;
    t.villager_id = v;
    t.task_seq_id = task_seq_counter++;
    t.birth_tick  = current_tick;
    t.mem_id      = next_mem_id++;

    int32_t hunger_snap = needs[v].hunger, social_snap = needs[v].social, safety_snap = needs[v].safety;

    // ---- 2. DREAM (cooldown + lonely-or-roll) — checked before hearsay so it isn't starved. ----
    if (!survival && can_dream) {
        std::vector<std::string> frags;
        const auto& mems = stores[v].all();
        for (size_t k = mems.size(); k-- > 0 && frags.size() < 3; ) frags.push_back(mems[k].text);
        if (!frags.empty()) {
            t.kind          = TaskKind::DREAM;
            t.mtype         = MemType::DREAM;
            t.target        = v;
            t.origin_mem_id = t.mem_id;                    // self-origin: a dream is a new belief
            t.fold_tick     = current_tick + 20;
            t.text          = bridge->dream(v, frags, trait_prose(genomes[v]), cseed); // §B2
            t.importance    = importance(MemType::DREAM, 0, lowest_need(v), t.mem_id, v, current_tick);
            last_dream_tick[v] = current_tick;
            if (db) {
                std::string frag_blob; for (const auto& f : frags) frag_blob += f;
                db->log_cognition(run_id, v, current_tick, hex64(fnv64_text(frag_blob)), t.text,
                                  "DREAM", word_count(t.text), genomes[v].pack(),
                                  hunger_snap, social_snap, safety_snap, cseed);
            }
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
            // §B3: bump fatigue on the memory just selected -- in-place mutation via mem_id
            // lookup, doesn't touch/invalidate `m` (see cognitive_store.h note_retold()).
            stores[src].note_retold(m->mem_id);
            // MSG_062 §2: degrade the heard memory -> two-segment retell -> forced novelty.
            // hearsay_hop is the shared engine/gate path (no logic the gate can't see).
            RetellResult rr = hearsay_hop(*bridge, v, *m, genomes[v], current_tick, cseed, &coined_words);
            t.kind          = TaskKind::HEARSAY;
            t.mtype         = MemType::HEARSAY;
            t.target        = src;                          // actor_id = source of the gossip
            t.source_depth  = (uint8_t)(m->source_depth + 1);
            t.origin_mem_id = m->origin_mem_id;             // inherit the belief-lineage root
            t.fold_tick     = current_tick + 5;
            t.text          = rr.out_text;
            t.importance    = importance(MemType::HEARSAY, t.source_depth, lowest_need(v),
                                         t.mem_id, v, current_tick);
            if (db) {
                // Re-derive the degraded text the listener actually worked from (pure function
                // of heard/tick, no RNG — safe to call again here purely for the delta metric).
                // Same coined_words set as hearsay_hop's internal degrade() call (§B6-1) --
                // otherwise this logged delta wouldn't reflect what was actually masked.
                std::string degraded = degrade(*m, current_tick, &coined_words);
                int delta = content_word_delta(degraded, rr.out_text);
                db->log_hearsay_chain(run_id, t.origin_mem_id, t.origin_mem_id, t.source_depth,
                                      src, v, genomes[src].pack(), genomes[v].pack(),
                                      m->text, rr.out_text, delta, current_tick);
                db->log_cognition(run_id, v, current_tick, hex64(fnv64_text(degraded)), rr.out_text,
                                  "HEARSAY", word_count(rr.out_text), genomes[v].pack(),
                                  hunger_snap, social_snap, safety_snap, cseed);
            }
            async_queue.push_back(std::move(t));
            return;
        }
        // neighbour has nothing to say yet -> fall through to ACTION
    }

    // ---- 4. ACTION (default + survival path): free thought -> intent -> hybrid resolver. ----
    PerceptionContext ctx = build_perception(v, nbrs, env);
    Cognition c = bridge->infer(v, ctx, cseed);
    t.kind          = TaskKind::ACTION;
    t.mtype         = MemType::EXPERIENCE;
    t.verb          = c.verb;
    t.target        = v;
    t.origin_mem_id = t.mem_id;                             // self-origin: a firsthand experience
    if ((c.verb == Verb::GIVE || c.verb == Verb::SPEAK) && !nbrs.empty()) {
        uint32_t pick = (uint32_t)pcg32_random_bounded_r(&pcg32_cognition_state[v], (int32_t)nbrs.size());
        t.target = nbrs[pick];
    }
    t.fold_tick  = current_tick + 5;
    t.text       = c.thought;
    t.importance = importance(MemType::EXPERIENCE, 0, lowest_need(v), t.mem_id, v, current_tick);
    if (db) {
        db->log_cognition(run_id, v, current_tick, hex64(fnv64_text(ctx.situation)), c.thought,
                          verb_name(c.verb), word_count(c.thought), genomes[v].pack(),
                          hunger_snap, social_snap, safety_snap, cseed);
    }
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
    e.mem_id        = t.mem_id;
    e.tick          = t.birth_tick;
    e.actor_id      = (t.kind == TaskKind::HEARSAY) ? t.target : t.villager_id;
    e.importance    = t.importance;
    e.type          = t.mtype;
    e.source_depth  = t.source_depth;
    e.origin_mem_id = t.origin_mem_id;
    e.text          = t.text;
    std::optional<MemoryEntry> evicted = stores[t.villager_id].add(e, current_tick);
    if (db) {
        db->persist_memory(run_id, t.villager_id, e);
        db->log_belief_birth(run_id, e.mem_id, e.type, e.tick, e.importance, e.source_depth,
                             fnv64_text(e.text));
        if (evicted) db->mark_belief_death(run_id, evicted->mem_id, current_tick, evicted->origin_mem_id);
    }

    // ---- Coinage harvest: flag free-generation tokens absent from the system dictionary
    // AND surviving the §B5 inflection/fragment filter (coinage.h::coined_terms). First
    // coiner wins; the term then rides hearsay/dream/action text naturally. Adoption
    // (a DIFFERENT villager's text reusing an already-coined term, credited once each) feeds
    // CoinageSpread — observational for DISPATCH/RNG (still true: nothing here consumes a
    // stream draw or changes what runs next tick). §B6-2 (QA-approved, packet 102 item 4)
    // deliberately changes the other half of that old claim: `coined_words` now ALSO feeds
    // back into generated TEXT via distortion_word() (degradation.h) -- a hash-picked,
    // RNG-free read of a set that's itself a pure function of run history, so determinism
    // is unaffected, but "coinage never influences anything downstream" is no longer true.
    // That feedback loop -- a culture mutating itself via its own invented words -- IS the
    // thesis (Research Dossier §B), not a smuggled side-channel. ----
    for (const auto& term : coined_terms(t.text)) {
        if (coined_words.insert(term).second) {
            CoinageOrigin origin;
            origin.coiner = t.villager_id;
            origin.coiner_genome = genomes[t.villager_id].pack();
            origin.birth_tick = t.birth_tick;
            coinage_origin[term] = origin;
            term_adopters[term].insert(t.villager_id);
            if (db) db->register_coinage(run_id, term, t.villager_id, t.birth_tick);
        } else if (term_adopters[term].insert(t.villager_id).second && db) {
            const CoinageOrigin& origin = coinage_origin[term];
            const char* ctx = (t.kind == TaskKind::HEARSAY) ? "hearsay"
                            : (t.kind == TaskKind::DREAM)   ? "dream" : "speech";
            db->log_coinage_adoption(run_id, term, origin.coiner, origin.coiner_genome,
                                     origin.birth_tick, t.villager_id, t.birth_tick, ctx);
        }
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
    s << trait_prose(g);  // §B2: single source of truth, shared with retell()/dream() now
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
