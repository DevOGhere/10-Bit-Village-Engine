#pragma once
#include <cstdint>
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
