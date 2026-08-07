#pragma once
#include <cstdint>
#include <cstdlib> // std::abs
#include <vector>
#include <algorithm>
#include <set>
#include <map>
#include <string>
#include "core/types.h"
#include "core/pcg32.h"
#include "engine/genome.h"
#include "engine/needs.h"
#include "cognition/resolver.h"        // Verb, PerceptionContext, Env
#include "cognition/cognitive_store.h" // CognitiveStore, MemoryEntry

namespace tbv {

// Forward declarations — defined in their own TUs. WorldState only holds non-owning
// pointers and calls them from world.cpp, so we avoid the world<->llama_bridge include cycle.
class LlamaBridge;
class Database;

constexpr size_t MAX_VILLAGERS = 100;

// Run 2 Plan §B1 — grounded-experience floor. Every GROUNDED_FLOOR_MODth cognition turn
// (per villager, by dispatch ordinal) is forced down the ACTION path regardless of
// dream/hearsay eligibility, so firsthand experience isn't starved by the dense-grid
// HEARSAY/DREAM priority (Run 1: 0.6% ACTION). Survival reflex still overrides everything.
constexpr uint64_t GROUNDED_FLOOR_MOD = 4; // 1-in-4 = 25%

// Spatial grid (Gemini [052]: 32x24 = 768 cells, ~13% density for 100 villagers).
constexpr int GRID_W = 32;
constexpr int GRID_H = 24;

// Run 2 Plan §C1 — world events (famine + wanderer), v1: two types only. Rolled off a
// DEDICATED RNG stream (pcg32_event_state, seeded like every other per-purpose stream in
// init()) so it never desyncs anything else. Both the roll and its effects are gated
// `if (bridge)` at every call site -- headless Phase 0 must never touch this code, proven by
// `--phase0_gate` (re-run byte-identical to the pre-C1 baseline after this landed).
constexpr uint64_t EVENT_CHECK_EVERY = 5000; // ~9.7h wall at measured HF throughput
constexpr uint64_t EVENT_DURATION    = 1000; // famine length, ticks
enum class VillageEvent : uint8_t { NONE = 0, FAMINE = 1 }; // WANDERER is one-shot, no ongoing state

// Run 3 Fix B — social & safety decay (Finding 2: previously never moved from 100000 at all).
// Starting constants, NOT locked -- same "needs empirical validation" status as Fix A's tile
// constants, gated together in the post-cutover combined scratch run, not hand-tuned here.
// Social: fixed per-tick decay (no RNG draw -- deterministic, no new stream needed), roughly
// matching hunger's ~24.5 avg/tick so isolation drains on a comparable timescale. Floored
// (not driven to 0) so isolation reads as "lonely" (crosses build_perception's <30000 prompt
// threshold) not "catatonic" -- Finding 6 measured 19.5-28.7% of dispatch-turns isolated, with
// no SPEAK/HEARSAY recovery path reachable for that slice; DREAM's small passive recovery
// (apply_task) is the only reachable recovery for a fully isolated villager.
constexpr int32_t SOCIAL_DECAY_PER_TICK  = 20;
constexpr int32_t SOCIAL_ISOLATION_FLOOR = 20000;
constexpr int32_t DREAM_SOCIAL_RECOVERY  = 2000; // small -- a dream isn't real company
// Safety: stress model, not hunger-style monotonic drain -- decays only while a famine is
// active (source-verified: FAMINE is a fixed periodic roll off a dedicated stream, ~25% of
// every EVENT_CHECK_EVERY window, zero dependence on hunger/Fix A), recovers passively once
// it ends. Decay rate sized so a full EVENT_DURATION=1000-tick famine can plausibly drive
// safety into build_perception's <30000 "afraid" band; recovery deliberately slower than
// decay -- stress lingering past the immediate threat, not snapping back instantly.
constexpr int32_t SAFETY_DECAY_PER_TICK    = 100;
constexpr int32_t SAFETY_RECOVERY_PER_TICK = 30;

// Run 3 Fix A — regrowing ground food. Starting constants, NOT locked (same status as Fix
// B's above): system-wide hunger decay ~= 100 villagers x ~24.5 avg/tick ~= 2,450 units/tick;
// one EAT restores 40,000, so equilibrium needs ~1 successful EAT every ~16 ticks. N/REGROW
// ~= 1/16 gives this starting point, deliberately looser than the bare ratio since MOVE_TO
// stays a pure random walk (Finding 1's fix, not directed foraging -- see plan §0.1 scope
// fence, MSG_034) so a critical villager can't seek a tile, only stumble onto one. Needs the
// N=15/20/25 x REGROW=200/250/300 sensitivity sweep (post-cutover combined scratch run)
// before these are trusted, not hand-tuned here.
constexpr size_t   N_FOOD_TILES        = 20;
constexpr uint64_t FOOD_REGROW_PERIOD  = 250;

enum class TaskKind { ACTION, HEARSAY, DREAM };

// An async cognition result resolved at dispatch, folded back into the world at fold_tick.
// operator< is UNCHANGED (villager_id, fold_tick, task_seq_id) — the tested strict total
// order that neutralizes scheduling races. The payload below rides along, untouched by sort.
struct DeferredTask {
    VillagerID villager_id = 0;
    uint64_t fold_tick = 0;
    uint64_t task_seq_id = 0;     // monotonic, assigned at dispatch

    // ---- payload (does not participate in ordering) ----
    TaskKind kind = TaskKind::ACTION;
    Verb verb = Verb::WAIT;       // ACTION: resolved engine verb
    VillagerID target = 0;        // ACTION GIVE/SPEAK: neighbour; HEARSAY: source villager
    MemType mtype = MemType::EXPERIENCE;
    uint8_t source_depth = 0;     // hearsay hop count
    int32_t importance = 0;       // LOCK 2, computed at dispatch
    uint64_t birth_tick = 0;      // dispatch tick (memory's true timestamp, for decay)
    uint32_t mem_id = 0;
    uint32_t origin_mem_id = 0;   // belief-lineage root (Step 4): self for ACTION/DREAM,
                                  // inherited from the heard memory for HEARSAY
    std::string text;             // the free prose to store

    bool operator<(const DeferredTask& other) const {
        if (villager_id != other.villager_id) return villager_id < other.villager_id;
        if (fold_tick != other.fold_tick) return fold_tick < other.fold_tick;
        return task_seq_id < other.task_seq_id;
    }
};

// Quick avalanche to decorrelate adjacent villager seeds.
inline uint64_t splitmix64_avalanche(uint64_t z) {
    z = (z ^ (z >> 30)) * 0xbf58476d1ce4e5b9ULL;
    z = (z ^ (z >> 27)) * 0x94d049bb133111ebULL;
    return z ^ (z >> 31);
}

class WorldState {
public:
    Tick current_tick = 0;

    // SOA (Struct of Arrays) for fixed population 100
    Genome genomes[MAX_VILLAGERS];
    Needs needs[MAX_VILLAGERS];

    // Spatial state (Phase 3)
    uint16_t pos_x[MAX_VILLAGERS];
    uint16_t pos_y[MAX_VILLAGERS];
    bool holding_food[MAX_VILLAGERS];

    // Run 3 Fix A — ground food tiles. Positions are fixed at genesis (splitmix64_avalanche
    // from master_seed, same non-stream pattern holding_food already uses) and NEVER mutate,
    // so unlike every array above they are deliberately NOT persisted in EngineCheckpoint --
    // --serve's boot always calls init(seed) before load_checkpoint(), so init() alone always
    // reconstructs them correctly on restart, the same way genome would if it weren't
    // (redundantly, harmlessly) also blobbed for consistency with this codebase's existing
    // precedent. regrow_tick DOES mutate (a tile goes unavailable until regrow_tick <= tick
    // once eaten) and is the one piece of tile state that needs the checkpoint round-trip.
    uint16_t food_tile_x[N_FOOD_TILES];
    uint16_t food_tile_y[N_FOOD_TILES];
    uint64_t food_tile_regrow_tick[N_FOOD_TILES]; // 0 = available from genesis

    // Two isolated RNG streams per villager: LLM retry/cognition never desyncs physics.
    pcg32_state pcg32_physics_state[MAX_VILLAGERS];
    pcg32_state pcg32_cognition_state[MAX_VILLAGERS];

    // §C1 — one dedicated village-wide stream for the event roll (not per-villager: this is
    // a single shared decision, not 100 independent ones). Isolated the same way physics vs
    // cognition is -- nothing else ever draws from this.
    pcg32_state pcg32_event_state;
    VillageEvent active_event = VillageEvent::NONE;
    uint64_t event_start_tick = 0; // only meaningful while active_event == FAMINE

    // Cognition state (Phase 3)
    CognitiveStore stores[MAX_VILLAGERS];
    uint64_t last_dream_tick[MAX_VILLAGERS];
    uint32_t next_mem_id = 0;       // monotonic, deterministic id source
    uint64_t task_seq_counter = 0;  // monotonic dispatch counter

    // Queue of resolved cognition tasks waiting to fold back at their fold_tick.
    std::vector<DeferredTask> async_queue;

    // Coinage harvest (v1): terms flagged not-in-dictionary, first-coiner wins. In-memory
    // mirror of the CoinedWords table — lets gates check spread without a DB round-trip.
    std::set<std::string> coined_words;

    // CoinageSpread bookkeeping (Step 4, observational only — not determinism-critical,
    // doesn't feed any RNG draw or dispatch decision). origin = coiner identity captured at
    // first coinage; adopters = villagers already credited for this term (coiner included),
    // so re-use of an already-coined term by the SAME villager isn't logged twice.
    struct CoinageOrigin { VillagerID coiner = 0; uint16_t coiner_genome = 0; uint64_t birth_tick = 0; };
    std::map<std::string, CoinageOrigin> coinage_origin;
    std::map<std::string, std::set<VillagerID>> term_adopters;

    // Non-owning IO. Null in headless physics-only runs (Phase 0) -> no cognition, no persistence.
    LlamaBridge* bridge = nullptr;
    Database* db = nullptr;
    std::string run_id;

    void init(uint64_t master_seed) {
        current_tick = 0;
        next_mem_id = 0;
        task_seq_counter = 0;
        async_queue.clear();
        coined_words.clear();
        active_event = VillageEvent::NONE;
        event_start_tick = 0;
        // Own offset (0x70000), same splitmix-derived-seed pattern as every other stream --
        // never collides with the per-villager 0x10000-0x60000 range above.
        uint64_t e_seed = splitmix64_avalanche(master_seed + 0x70000);
        pcg32_srandom_r(&pcg32_event_state, e_seed, 2);
        for (uint32_t i = 0; i < MAX_VILLAGERS; ++i) {
            // Seed RNG streams (unchanged from Phase 0 — keeps Phase 0 hashes byte-identical).
            uint64_t p_seed = splitmix64_avalanche(master_seed + i + 0x10000);
            pcg32_srandom_r(&pcg32_physics_state[i], p_seed, 0);
            uint64_t c_seed = splitmix64_avalanche(master_seed + i + 0x20000);
            pcg32_srandom_r(&pcg32_cognition_state[i], c_seed, 1);

            needs[i] = Needs();
            // Diverse genome per villager, deterministically from the seed (NOT a stream draw,
            // and genome isn't logged in VillagerState, so Phase 0 hashes are untouched).
            // "Genome is semantics": these traits flavour each villager's prompt (build_perception).
            uint64_t g = splitmix64_avalanche(master_seed + i + 0x60000);
            genomes[i] = Genome{ (uint8_t)(g & 3),        (uint8_t)((g >> 2) & 3),
                                 (uint8_t)((g >> 4) & 3), (uint8_t)((g >> 6) & 3),
                                 (uint8_t)((g >> 8) & 3) };

            // Spatial/food init from splitmix (NOT the streams) so Phase 0 physics is untouched.
            pos_x[i] = (uint16_t)(splitmix64_avalanche(master_seed + i + 0x30000) % GRID_W);
            pos_y[i] = (uint16_t)(splitmix64_avalanche(master_seed + i + 0x40000) % GRID_H);
            holding_food[i] = (splitmix64_avalanche(master_seed + i + 0x50000) & 1ULL) != 0;
            last_dream_tick[i] = 0;
        }

        // Run 3 Fix A — tile positions, fixed forever at genesis. New offset range (0x80000/
        // 0x90000) so it can never collide with the per-villager 0x10000-0x70000 range above
        // (villager index i only ever reaches MAX_VILLAGERS-1=99, leaving huge headroom).
        // regrow_tick=0 means available from tick 0 -- matches EngineCheckpoint's restore path
        // (a checkpoint that predates this fix, or a fresh genesis, both correctly read as
        // "every tile available").
        for (uint32_t i = 0; i < N_FOOD_TILES; ++i) {
            food_tile_x[i] = (uint16_t)(splitmix64_avalanche(master_seed + i + 0x80000) % GRID_W);
            food_tile_y[i] = (uint16_t)(splitmix64_avalanche(master_seed + i + 0x90000) % GRID_H);
            food_tile_regrow_tick[i] = 0;
        }
    }

    // Run 3 Fix A — index of the lowest-numbered AVAILABLE food tile within Chebyshev r=1 of
    // villager v (on or adjacent), else -1. Iterating ascending and returning the first hit
    // gives the deterministic lowest-tile-index tiebreak for free when a villager is in reach
    // of more than one available tile at once. Called both at dispatch time (as a verb-
    // selection hint via env.food_in_reach) and again at apply time (as the authoritative,
    // re-checked source of truth for which tile actually gets consumed -- see apply_task's
    // EAT case; the dispatch-time call never mutates state itself).
    int nearest_available_food_tile(VillagerID v, uint64_t tick) const {
        int vx = pos_x[v], vy = pos_y[v];
        for (size_t i = 0; i < N_FOOD_TILES; ++i) {
            if (food_tile_regrow_tick[i] > tick) continue;
            int dx = std::abs((int)food_tile_x[i] - vx);
            int dy = std::abs((int)food_tile_y[i] - vy);
            if (std::max(dx, dy) <= 1) return (int)i;
        }
        return -1;
    }

    // Wire live cognition + persistence (call before --run; leave unset for Phase 0).
    void attach(LlamaBridge* b, Database* d, const std::string& rid) {
        bridge = b; db = d; run_id = rid;
    }

    void tick(); // defined in src/world.cpp

    // LOCK 2 — engine-derived salience. Deterministic, integer, jitter from a hash (never a
    // stream draw, which would desync dispatch order). FLOOR keeps deep rumors alive; CEIL stops
    // an immortal memory dominating salience.
    static int32_t importance(MemType type, uint8_t depth, int32_t lowest_need,
                              uint32_t mem_id, VillagerID v, uint64_t tick) {
        int32_t base = (type == MemType::EXPERIENCE) ? 1000
                     : (type == MemType::HEARSAY)    ? (800 - 150 * (int32_t)depth)
                                                     : 600; // DREAM
        int32_t press = (FIXED_POINT_ONE - lowest_need) / 100;            // 0..1000
        uint64_t h = splitmix64_avalanche(((uint64_t)mem_id)
                                          ^ ((uint64_t)v << 20)
                                          ^ (tick << 40));
        int32_t jitter = (int32_t)(h % 129) - 64;                        // [-64, +64]
        int32_t imp = base + press + jitter;
        if (imp < 100)  imp = 100;     // high-pass floor
        if (imp > 2000) imp = 2000;    // low-pass ceil
        return imp;
    }

private:
    // --- Phase 3 helpers, defined in src/world.cpp ---
    void dispatch_cognition(VillagerID v);
    void apply_task(const DeferredTask& t);
    void check_world_event(); // §C1 -- roll/apply/expire, called from tick(), `if (bridge)` gated
    std::vector<VillagerID> neighbours(VillagerID v) const; // Chebyshev r=1, ascending id
    PerceptionContext build_perception(VillagerID v,
                                       const std::vector<VillagerID>& nbrs,
                                       const Env& env) const;
    int32_t lowest_need(VillagerID v) const {
        return std::min({needs[v].hunger, needs[v].social, needs[v].safety});
    }
};

} // namespace tbv
