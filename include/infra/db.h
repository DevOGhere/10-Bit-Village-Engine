#pragma once
#include <string>
#include <vector>
#include "core/types.h"
#include "engine/world.h"
#include "cognition/cognitive_store.h" // MemoryEntry

struct sqlite3;

namespace tbv {

class Database {
public:
    Database(const std::string& path);
    ~Database();

    void init_schema();

    // Phase 0: Record seed-driven dynamic state per tick to prove determinism
    void log_tick_state(const WorldState& state);

    // Phase 2: append-only persistence of free-text memories (observer interface).
    void persist_memory(const std::string& run_id, VillagerID villager_id, const MemoryEntry& m);
    int  count_memories(const std::string& run_id); // verification only (not used in tick loop)

    // Coinage harvest: first-registration-wins (run_id, term) record of a novel word.
    void register_coinage(const std::string& run_id, const std::string& term,
                           VillagerID coiner, uint64_t birth_tick);

private:
    sqlite3* db = nullptr;
};

} // namespace tbv
