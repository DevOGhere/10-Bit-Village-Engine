// Run 2 Plan §C1: active_event/event_start_tick/pcg32_event_state must survive checkpoint
// save/restore. NOT meaningfully covered by --checkpoint_gate: it runs K=80 ticks, far under
// EVENT_CHECK_EVERY=5000, so the event roll never fires either way and these fields would
// round-trip as all-zero regardless of whether the plumbing actually works (same blind-spot
// pattern as B3's retell_count -- world_hash() doesn't read any of these fields either).
// This test hand-places a mid-famine state and a distinguishable event-RNG stream instead.
// No LlamaBridge needed -- links against db.cpp + sqlite3 only.
#include "infra/db.h"
#include "engine/world.h"
#include <cassert>
#include <cstdio>
#include <cstdlib>

using namespace tbv;

int main() {
    const char* dbpath = "/tmp/tbv_event_state_gate.db";
    std::remove(dbpath);

    WorldState w;
    w.init(424242);
    w.run_id = "event_gate";
    // Simulate mid-famine: not what init() sets, so a restore that silently fell back to
    // NONE/0 would go undetected by an all-defaults test.
    w.active_event = VillageEvent::FAMINE;
    w.event_start_tick = 12345;
    w.current_tick = 12500; // 155 ticks into the famine window
    // Advance the event stream a few draws so it's in a state distinguishable from a
    // freshly-seeded one -- a restore bug that re-seeded instead of copying would still
    // "work" from tick 0 but desync every subsequent roll.
    for (int i = 0; i < 7; ++i) pcg32_random_r(&w.pcg32_event_state);
    pcg32_state expected_event_rng = w.pcg32_event_state;

    Database db(dbpath);
    db.init_schema();
    db.save_checkpoint(w);

    WorldState restored;
    restored.init(1); // different seed on purpose -- restore must fully overwrite
    restored.run_id = "event_gate";
    bool loaded = db.load_checkpoint(restored);
    if (!loaded) { std::fprintf(stderr, "FAIL: load_checkpoint found no row\n"); return 1; }

    bool ok = true;
    auto check = [&](const char* name, bool cond) {
        std::printf("%s: %s\n", name, cond ? "OK" : "FAIL");
        ok = ok && cond;
    };
    check("active_event == FAMINE", restored.active_event == VillageEvent::FAMINE);
    check("event_start_tick == 12345", restored.event_start_tick == 12345);
    check("pcg32_event_state.state matches", restored.pcg32_event_state.state == expected_event_rng.state);
    check("pcg32_event_state.inc matches", restored.pcg32_event_state.inc == expected_event_rng.inc);
    // Draw once more from each and confirm they agree -- the strongest form of "the stream
    // really is the same," not just that the raw state bytes happen to match.
    uint32_t r_orig = pcg32_random_r(&expected_event_rng);
    uint32_t r_restored = pcg32_random_r(&restored.pcg32_event_state);
    check("next draw from restored stream matches original", r_orig == r_restored);

    std::printf(ok ? "PASS: event state round-trips through checkpoint save/restore\n"
                    : "FAIL: event state did not round-trip\n");
    return ok ? 0 : 1;
}
