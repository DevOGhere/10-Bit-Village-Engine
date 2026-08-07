// Run 3 Fix A gate: food_tile_regrow_tick must survive checkpoint save/restore.
// NOT covered by --checkpoint_gate's hash proof by itself -- that gate's window (K=80,
// N=200) may or may not organically land a critical villager on a tile, so a pass there
// doesn't prove this field round-trips; a round-trip of an all-zero array is the same kind
// of vacuous result Fix D/F hit at 200 ticks. This test hand-places non-zero regrow_tick
// values (including an edge case at the exact tick boundary) so the round-trip is actually
// exercised, same pattern as retell_count_checkpoint.cpp. No LlamaBridge needed.
#include "infra/db.h"
#include "engine/world.h"
#include <cassert>
#include <cstdio>
#include <cstdlib>

using namespace tbv;

int main() {
    const char* dbpath = "/tmp/tbv_food_tile_gate.db";
    std::remove(dbpath);

    WorldState w;
    w.init(12345);
    w.run_id = "food_tile_gate";

    // Force a mix: tile 0 consumed long ago (regrow_tick in the past relative to check
    // tick), tile 5 consumed just now (regrow_tick == check tick, boundary case --
    // nearest_available_food_tile treats regrow_tick <= tick as available, so this tile
    // must read back as AVAILABLE), tile 19 (last index) consumed with a far-future
    // regrow_tick (still unavailable), everything else stays at init()'s default 0.
    const uint64_t check_tick = 1000;
    w.food_tile_regrow_tick[0] = 500;               // regrown by check_tick -> available
    w.food_tile_regrow_tick[5] = check_tick;         // exact boundary -> available
    w.food_tile_regrow_tick[19] = check_tick + 5000; // still unavailable

    Database db(dbpath);
    db.init_schema();
    db.save_checkpoint(w);

    WorldState restored;
    restored.init(999); // different seed on purpose -- restore must fully overwrite, not merge
    restored.run_id = "food_tile_gate"; // load_checkpoint keys on this; init() doesn't set it
    bool loaded = db.load_checkpoint(restored);
    if (!loaded) { std::fprintf(stderr, "FAIL: load_checkpoint found no row\n"); return 1; }

    struct Check { size_t tile; uint64_t expected_regrow_tick; bool expected_available_at_check_tick; };
    Check checks[] = {
        {0, 500, true},
        {5, check_tick, true},
        {19, check_tick + 5000, false},
        {7, 0, true}, // untouched tile -- must still read init()'s all-zero default, not garbage
    };
    bool all_ok = true;
    for (const auto& c : checks) {
        bool value_ok = (restored.food_tile_regrow_tick[c.tile] == c.expected_regrow_tick);
        bool avail_ok = ((restored.food_tile_regrow_tick[c.tile] <= check_tick) == c.expected_available_at_check_tick);
        bool ok = value_ok && avail_ok;
        std::printf("tile %zu: regrow_tick=%llu expected=%llu available@%llu=%s expected=%s %s\n",
                    c.tile, (unsigned long long)restored.food_tile_regrow_tick[c.tile],
                    (unsigned long long)c.expected_regrow_tick, (unsigned long long)check_tick,
                    (restored.food_tile_regrow_tick[c.tile] <= check_tick) ? "yes" : "no",
                    c.expected_available_at_check_tick ? "yes" : "no", ok ? "OK" : "FAIL");
        all_ok = all_ok && ok;
    }

    // Positions must NOT round-trip through the checkpoint (deliberately not persisted --
    // they're a pure function of seed) -- restored (seed 999) must show seed-999 positions,
    // matching a fresh init(999), not seed-12345's positions from w.
    WorldState fresh999;
    fresh999.init(999);
    bool positions_from_init_not_checkpoint = true;
    for (size_t i = 0; i < N_FOOD_TILES; ++i) {
        if (restored.food_tile_x[i] != fresh999.food_tile_x[i] ||
            restored.food_tile_y[i] != fresh999.food_tile_y[i]) {
            positions_from_init_not_checkpoint = false;
            std::fprintf(stderr, "FAIL: tile %zu position diverged from init(999) -- "
                                  "positions must come from init(), never the checkpoint\n", i);
        }
    }
    std::printf(positions_from_init_not_checkpoint
                    ? "OK: tile positions came from init(999), not leaked from w's seed-12345 checkpoint\n"
                    : "FAIL: tile positions did not match init(999)\n");
    all_ok = all_ok && positions_from_init_not_checkpoint;

    std::printf(all_ok ? "PASS: food_tile_regrow_tick round-trips through checkpoint save/restore\n"
                        : "FAIL: food_tile_regrow_tick did not round-trip correctly\n");
    return all_ok ? 0 : 1;
}
