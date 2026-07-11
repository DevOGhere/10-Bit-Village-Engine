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

    // --serve mode calls log_tick_state() every tick with no cap (added for Step 7's live
    // spectator feed) -- at 100 rows/tick this is the dominant growth term for a long-running
    // Space (~357MB/week, ~18.6GB/year at the real measured ~354 ticks/hour). VillagerState's
    // only consumers are the live /ws feed (reads the newest tick) and short-window debugging,
    // neither needs full history. Deletes tick_id < keep_from_tick from both VillagerState and
    // Ticks; called periodically from --serve, not every tick (DELETE cost scales with rows).
    void prune_villager_state(uint64_t keep_from_tick);

    // §C2 (Run 2 Plan) -- the OTHER dominant growth term: ~34MB/day of MemoryGraph/
    // CognitionLog/HearsayChain text (Run 1 measured, 3 tables not 1 like VillagerState).
    // Only safe to call AFTER a confirmed archive upload covers the range being deleted
    // (supervisor.py triggers this via SIGUSR2, never on a timer alone) -- unlike
    // prune_villager_state (whose only consumers are "newest tick" reads, so losing old rows
    // is never a real loss), this data is genuinely gone from the LIVE db afterward; the
    // archive release is what makes that safe.
    void prune_text_tables(uint64_t keep_from_tick);

    // Phase 2: append-only persistence of free-text memories (observer interface).
    void persist_memory(const std::string& run_id, VillagerID villager_id, const MemoryEntry& m);
    int  count_memories(const std::string& run_id); // verification only (not used in tick loop)

    // Coinage harvest: first-registration-wins (run_id, term) record of a novel word.
    void register_coinage(const std::string& run_id, const std::string& term,
                           VillagerID coiner, uint64_t birth_tick);

    // ---- MSG_062 §4 observational tables (Step 4) ----
    void log_hearsay_chain(const std::string& run_id, uint32_t chain_id, uint32_t origin_mem_id,
                            uint8_t hop, VillagerID src, VillagerID dst, uint16_t src_genome,
                            uint16_t dst_genome, const std::string& inbound_text,
                            const std::string& outbound_text, int word_delta, uint64_t tick);

    void log_cognition(const std::string& run_id, VillagerID villager_id, uint64_t tick,
                        const std::string& perception_hash, const std::string& raw_thought,
                        const std::string& extracted_verb, int token_count, uint16_t genome_pack,
                        int32_t hunger, int32_t social, int32_t safety, uint64_t seed_used);

    void log_belief_birth(const std::string& run_id, uint32_t memory_id, MemType type,
                           uint64_t birth_tick, int32_t importance, uint8_t hop_count,
                           uint64_t content_hash);
    // population_reach computed internally: COUNT(DISTINCT villager) over MemoryGraph rows
    // sharing this belief's origin_mem_id (the lineage, not just this one instance).
    void mark_belief_death(const std::string& run_id, uint32_t memory_id, uint64_t death_tick,
                            uint32_t origin_mem_id);

    void log_coinage_adoption(const std::string& run_id, const std::string& term,
                               VillagerID coiner_id, uint16_t coiner_genome, uint64_t birth_tick,
                               VillagerID adopter_id, uint64_t adoption_tick,
                               const std::string& context);

    void log_world_digest(const std::string& run_id, uint64_t tick, uint64_t seed,
                           uint64_t world_hash, int belief_count, int coined_terms,
                           int avg_importance, int max_hearsay_depth, uint64_t genome_dist_hash);

    // EngineCheckpoint — full live-state serialize/restore (Step 4). DELETE+INSERT overwrite
    // semantics per call, scoped to w.run_id. load_checkpoint returns false (world untouched)
    // if no checkpoint row exists for that run_id.
    void save_checkpoint(const WorldState& w);
    bool load_checkpoint(WorldState& w);

    // Verification-only row counter (Step 4 gate use, not used in the tick loop). `table` is
    // always a hardcoded literal at the call site, never external input.
    int count_rows(const std::string& table, const std::string& run_id);

    // Online Backup API copy of this live DB to dest_path (Step 5). Called from the --serve
    // tick loop in response to SIGUSR1/SIGTERM, never from the signal handler itself.
    bool backup_to(const std::string& dest_path);

private:
    sqlite3* db = nullptr;

    // Real bug, caught by AGY (packet 105) on B3's retell_count, same pattern independently
    // repeated by C1's EngineCheckpoint columns before that catch reached the builder seat:
    // `CREATE TABLE IF NOT EXISTS` is a documented no-op on a table that already exists --
    // it does NOT add missing columns. A schema-changing deploy against an EXISTING db
    // (exactly what the Run 1 -> Run 2 cutover does, however briefly, before backups get
    // deleted) would silently fail every query touching the new column, load_checkpoint()
    // returns false cleanly (no crash) and the caller falls back to "fresh boot" -- which
    // happens to be harmless AT cutover (that's the intended end state anyway) but would
    // silently wipe REAL accumulated Run 2 progress on any future schema-changing deploy
    // once Run 2 has real data. Idempotent: checks PRAGMA table_info first, only runs
    // ALTER TABLE ADD COLUMN if genuinely missing -- safe to call on a freshly-created table
    // too (where the column was already in the CREATE TABLE text and this becomes a no-op).
    void ensure_column(const std::string& table, const std::string& column,
                       const std::string& col_def_sql);
};

} // namespace tbv
