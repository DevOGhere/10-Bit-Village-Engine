// Run 2 Plan §B3, gate item 2: retell_count must survive checkpoint save/restore.
// NOT covered by --checkpoint_gate's hash proof -- world_hash() (main.cpp) hashes
// mem_id/importance/type/source_depth/text but never retell_count, so a bug here would
// pass that gate silently. This test touches the SQLite round-trip directly instead.
// No LlamaBridge needed (WorldState only holds a pointer to it), so this links against
// db.cpp + sqlite3 only -- no llama.cpp, fast to build and run.
#include "infra/db.h"
#include "engine/world.h"
#include <cassert>
#include <cstdio>
#include <cstdlib>

using namespace tbv;

int main() {
    const char* dbpath = "/tmp/tbv_retell_count_gate.db";
    std::remove(dbpath);

    WorldState w;
    w.init(12345);
    w.run_id = "retell_gate";

    // Hand-place a few memories with distinct retell_count values across different
    // villagers, including edge values (0, and a large one near uint16_t range) --
    // exercises the actual bind/column int path in db.cpp, not just "happens to be 0".
    auto make_mem = [](uint32_t mem_id, uint16_t retell_count, const std::string& text) {
        MemoryEntry m;
        m.mem_id = mem_id;
        m.tick = 5;
        m.actor_id = 0;
        m.importance = 1000;
        m.type = MemType::EXPERIENCE;
        m.source_depth = 0;
        m.origin_mem_id = mem_id;
        m.text = text;
        m.retell_count = retell_count;
        return m;
    };
    w.stores[0].add(make_mem(1, 0, "never retold"), 5);
    w.stores[0].add(make_mem(2, 5, "retold five times"), 5);
    w.stores[3].add(make_mem(3, 65535, "max uint16 retell count"), 5);
    w.stores[99].add(make_mem(4, 150, "villager 99, one RETELL_PENALTY worth"), 5);

    Database db(dbpath);
    db.init_schema();
    db.save_checkpoint(w);

    WorldState restored;
    restored.init(999); // different seed on purpose -- restore must fully overwrite, not merge
    restored.run_id = "retell_gate"; // load_checkpoint keys on this; init() doesn't set it
    bool loaded = db.load_checkpoint(restored);
    if (!loaded) { std::fprintf(stderr, "FAIL: load_checkpoint found no row\n"); return 1; }

    auto find = [](const CognitiveStore& store, uint32_t mem_id) -> const MemoryEntry* {
        for (const auto& m : store.all()) if (m.mem_id == mem_id) return &m;
        return nullptr;
    };

    struct Check { uint32_t villager; uint32_t mem_id; uint16_t expected; };
    Check checks[] = {
        {0, 1, 0}, {0, 2, 5}, {3, 3, 65535}, {99, 4, 150},
    };
    bool all_ok = true;
    for (const auto& c : checks) {
        const MemoryEntry* m = find(restored.stores[c.villager], c.mem_id);
        if (!m) {
            std::fprintf(stderr, "FAIL: villager %u mem_id %u missing after restore\n", c.villager, c.mem_id);
            all_ok = false;
            continue;
        }
        bool ok = (m->retell_count == c.expected);
        std::printf("villager %u mem_id %u: retell_count=%u expected=%u %s\n",
                    c.villager, c.mem_id, m->retell_count, c.expected, ok ? "OK" : "FAIL");
        all_ok = all_ok && ok;
    }

    std::printf(all_ok ? "PASS: retell_count round-trips through checkpoint save/restore\n"
                        : "FAIL: retell_count did not round-trip\n");
    return all_ok ? 0 : 1;
}
