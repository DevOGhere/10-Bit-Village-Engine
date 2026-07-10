#include "infra/db.h"
#include <sqlite3.h>
#include <stdexcept>
#include <iostream>
#include <cstring>
#include <cstdio>

namespace tbv {

Database::Database(const std::string& path) {
    if (sqlite3_open(path.c_str(), &db) != SQLITE_OK) {
        throw std::runtime_error("Failed to open SQLite database");
    }
}

Database::~Database() {
    if (db) {
        sqlite3_close(db);
    }
}

void Database::init_schema() {
    const char* sql = 
        "PRAGMA synchronous = NORMAL;"
        "PRAGMA journal_mode = WAL;"
        "CREATE TABLE IF NOT EXISTS Ticks ("
        "   tick_id INTEGER PRIMARY KEY"
        ");"
        "CREATE TABLE IF NOT EXISTS VillagerState ("
        "   tick_id INTEGER,"
        "   villager_id INTEGER,"
        "   hunger INTEGER,"
        "   social INTEGER,"
        "   safety INTEGER,"
        "   pos_x INTEGER,"
        "   pos_y INTEGER,"
        "   holding_food INTEGER,"
        "   PRIMARY KEY(tick_id, villager_id)"
        ");"
        "CREATE TABLE IF NOT EXISTS MemoryGraph ("
        "   id INTEGER PRIMARY KEY AUTOINCREMENT,"
        "   run_id TEXT,"
        "   mem_id INTEGER,"
        "   villager_id INTEGER,"
        "   tick INTEGER,"
        "   actor_id INTEGER,"
        "   importance INTEGER,"
        "   type INTEGER,"
        "   source_depth INTEGER,"
        "   origin_mem_id INTEGER,"
        "   text TEXT"
        ");"
        "CREATE INDEX IF NOT EXISTS idx_mem_villager_tick ON MemoryGraph(villager_id, tick);"
        "CREATE INDEX IF NOT EXISTS idx_mem_origin ON MemoryGraph(run_id, origin_mem_id);"
        "CREATE TABLE IF NOT EXISTS CoinedWords ("
        "   run_id TEXT,"
        "   term TEXT,"
        "   coiner INTEGER,"
        "   birth_tick INTEGER,"
        "   PRIMARY KEY(run_id, term)"
        ");"

        // ---- MSG_062 §4 — observational tables (Step 4). run_id added on every table for
        // multi-run hygiene, consistent with MemoryGraph/CoinedWords above. ----
        "CREATE TABLE IF NOT EXISTS HearsayChain ("
        "   run_id TEXT,"
        "   chain_id INTEGER,"
        "   origin_mem_id INTEGER,"
        "   hop INTEGER,"
        "   src_villager INTEGER,"
        "   dst_villager INTEGER,"
        "   src_genome INTEGER,"
        "   dst_genome INTEGER,"
        "   inbound_text TEXT,"
        "   outbound_text TEXT,"
        "   content_word_delta INTEGER,"
        "   tick INTEGER"
        ");"
        "CREATE INDEX IF NOT EXISTS idx_hearsay_chain ON HearsayChain(run_id, chain_id);"

        "CREATE TABLE IF NOT EXISTS CognitionLog ("
        "   run_id TEXT,"
        "   villager_id INTEGER,"
        "   tick INTEGER,"
        "   perception_hash TEXT,"
        "   raw_thought TEXT,"
        "   extracted_verb TEXT,"
        "   token_count INTEGER,"
        "   genome_pack INTEGER,"
        "   hunger INTEGER,"
        "   social INTEGER,"
        "   safety INTEGER,"
        "   seed_used INTEGER"
        ");"

        "CREATE TABLE IF NOT EXISTS BeliefSurvival ("
        "   run_id TEXT,"
        "   memory_id INTEGER,"
        "   memory_type TEXT,"
        "   birth_tick INTEGER,"
        "   death_tick INTEGER,"
        "   max_importance INTEGER,"
        "   hop_count INTEGER,"
        "   population_reach INTEGER,"
        "   content_hash TEXT,"
        "   PRIMARY KEY(run_id, memory_id)"
        ");"

        "CREATE TABLE IF NOT EXISTS CoinageSpread ("
        "   run_id TEXT,"
        "   term TEXT,"
        "   coiner_id INTEGER,"
        "   coiner_genome INTEGER,"
        "   birth_tick INTEGER,"
        "   adopter_id INTEGER,"
        "   adoption_tick INTEGER,"
        "   context TEXT"
        ");"

        "CREATE TABLE IF NOT EXISTS WorldDigest ("
        "   run_id TEXT,"
        "   tick INTEGER,"
        "   seed INTEGER,"
        "   world_hash TEXT,"
        "   belief_count INTEGER,"
        "   coined_terms INTEGER,"
        "   avg_importance INTEGER,"
        "   max_hearsay_depth INTEGER,"
        "   genome_dist_hash TEXT"
        ");"

        // ---- EngineCheckpoint — full live-state serialize/restore (Step 4). One row per
        // run_id, overwritten (DELETE+INSERT) on every save_checkpoint() call. Arrays are raw
        // POD memcpy BLOBs — safe only within the same process/binary architecture, which is
        // the actual restart model here (HF Spaces container reboot on the same image). ----
        "CREATE TABLE IF NOT EXISTS EngineCheckpoint ("
        "   run_id TEXT PRIMARY KEY,"
        "   tick INTEGER,"
        "   next_mem_id INTEGER,"
        "   task_seq_counter INTEGER,"
        "   genomes BLOB,"
        "   needs BLOB,"
        "   pos_x BLOB,"
        "   pos_y BLOB,"
        "   holding_food BLOB,"
        "   pcg32_physics BLOB,"
        "   pcg32_cognition BLOB,"
        "   last_dream_tick BLOB,"
        "   pcg32_event BLOB,"          // §C1 (Run 2 Plan) -- must round-trip on restore
        "   active_event INTEGER DEFAULT 0,"
        "   event_start_tick INTEGER DEFAULT 0"
        ");"
        "CREATE TABLE IF NOT EXISTS CheckpointMemory ("
        "   run_id TEXT,"
        "   villager_id INTEGER,"
        "   seq INTEGER,"
        "   mem_id INTEGER,"
        "   tick INTEGER,"
        "   actor_id INTEGER,"
        "   importance INTEGER,"
        "   type INTEGER,"
        "   source_depth INTEGER,"
        "   origin_mem_id INTEGER,"
        "   text TEXT,"
        "   retell_count INTEGER DEFAULT 0," // §B3 (Run 2 Plan) -- must round-trip on restore
        "   PRIMARY KEY(run_id, villager_id, seq)"
        ");"
        "CREATE TABLE IF NOT EXISTS CheckpointAsyncQueue ("
        "   run_id TEXT,"
        "   seq INTEGER,"
        "   villager_id INTEGER,"
        "   fold_tick INTEGER,"
        "   task_seq_id INTEGER,"
        "   kind INTEGER,"
        "   verb INTEGER,"
        "   target INTEGER,"
        "   mtype INTEGER,"
        "   source_depth INTEGER,"
        "   importance INTEGER,"
        "   birth_tick INTEGER,"
        "   mem_id INTEGER,"
        "   origin_mem_id INTEGER,"
        "   text TEXT,"
        "   PRIMARY KEY(run_id, seq)"
        ");"
        "CREATE TABLE IF NOT EXISTS CheckpointCoinedWords ("
        "   run_id TEXT,"
        "   term TEXT,"
        "   coiner INTEGER,"
        "   coiner_genome INTEGER,"
        "   birth_tick INTEGER,"
        "   PRIMARY KEY(run_id, term)"
        ");"
        "CREATE TABLE IF NOT EXISTS CheckpointTermAdopters ("
        "   run_id TEXT,"
        "   term TEXT,"
        "   villager_id INTEGER,"
        "   PRIMARY KEY(run_id, term, villager_id)"
        ");";

    char* errmsg = nullptr;
    if (sqlite3_exec(db, sql, nullptr, nullptr, &errmsg) != SQLITE_OK) {
        std::string err(errmsg);
        sqlite3_free(errmsg);
        throw std::runtime_error("Schema init failed: " + err);
    }
}

void Database::log_tick_state(const WorldState& state) {
    // For Phase 0, we log the exact hunger of every villager to prove the PRNG physics stream varies it
    // Use transaction for speed
    sqlite3_exec(db, "BEGIN TRANSACTION;", nullptr, nullptr, nullptr);
    
    std::string sql = "INSERT INTO Ticks (tick_id) VALUES (?);";
    sqlite3_stmt* stmt;
    sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, nullptr);
    sqlite3_bind_int64(stmt, 1, state.current_tick);
    sqlite3_step(stmt);
    sqlite3_finalize(stmt);

    sql = "INSERT INTO VillagerState (tick_id, villager_id, hunger, social, safety, pos_x, pos_y, holding_food) "
          "VALUES (?, ?, ?, ?, ?, ?, ?, ?);";
    sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, nullptr);
    for (uint32_t i = 0; i < MAX_VILLAGERS; ++i) {
        sqlite3_bind_int64(stmt, 1, state.current_tick);
        sqlite3_bind_int(stmt, 2, i);
        sqlite3_bind_int(stmt, 3, state.needs[i].hunger);
        sqlite3_bind_int(stmt, 4, state.needs[i].social);
        sqlite3_bind_int(stmt, 5, state.needs[i].safety);
        sqlite3_bind_int(stmt, 6, state.pos_x[i]);
        sqlite3_bind_int(stmt, 7, state.pos_y[i]);
        sqlite3_bind_int(stmt, 8, state.holding_food[i] ? 1 : 0);
        sqlite3_step(stmt);
        sqlite3_reset(stmt);
    }
    sqlite3_finalize(stmt);

    sqlite3_exec(db, "COMMIT;", nullptr, nullptr, nullptr);
}

void Database::prune_villager_state(uint64_t keep_from_tick) {
    sqlite3_exec(db, "BEGIN TRANSACTION;", nullptr, nullptr, nullptr);
    const char* sqls[] = {
        "DELETE FROM VillagerState WHERE tick_id < ?;",
        "DELETE FROM Ticks WHERE tick_id < ?;",
    };
    for (const char* sql : sqls) {
        sqlite3_stmt* stmt;
        sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr);
        sqlite3_bind_int64(stmt, 1, (sqlite3_int64)keep_from_tick);
        sqlite3_step(stmt);
        sqlite3_finalize(stmt);
    }
    sqlite3_exec(db, "COMMIT;", nullptr, nullptr, nullptr);
    // reclaim disk space -- otherwise SQLite keeps the freed pages in the file for reuse,
    // so village.db's on-disk size would never actually shrink after a prune
    sqlite3_exec(db, "VACUUM;", nullptr, nullptr, nullptr);
}

void Database::prune_text_tables(uint64_t keep_from_tick) {
    sqlite3_exec(db, "BEGIN TRANSACTION;", nullptr, nullptr, nullptr);
    const char* sqls[] = {
        "DELETE FROM MemoryGraph WHERE tick < ?;",
        "DELETE FROM CognitionLog WHERE tick < ?;",
        "DELETE FROM HearsayChain WHERE tick < ?;",
    };
    for (const char* sql : sqls) {
        sqlite3_stmt* stmt;
        sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr);
        sqlite3_bind_int64(stmt, 1, (sqlite3_int64)keep_from_tick);
        sqlite3_step(stmt);
        sqlite3_finalize(stmt);
    }
    sqlite3_exec(db, "COMMIT;", nullptr, nullptr, nullptr);
    sqlite3_exec(db, "VACUUM;", nullptr, nullptr, nullptr);
}

void Database::persist_memory(const std::string& run_id, VillagerID villager_id, const MemoryEntry& m) {
    const char* sql =
        "INSERT INTO MemoryGraph (run_id, mem_id, villager_id, tick, actor_id, importance, type, source_depth, origin_mem_id, text) "
        "VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?);";
    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK) return;
    sqlite3_bind_text(stmt, 1, run_id.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(stmt, 2, (int)m.mem_id);
    sqlite3_bind_int(stmt, 3, (int)villager_id);
    sqlite3_bind_int64(stmt, 4, (sqlite3_int64)m.tick);
    sqlite3_bind_int(stmt, 5, (int)m.actor_id);
    sqlite3_bind_int(stmt, 6, m.importance);
    sqlite3_bind_int(stmt, 7, (int)m.type);
    sqlite3_bind_int(stmt, 8, (int)m.source_depth);
    sqlite3_bind_int(stmt, 9, (int)m.origin_mem_id);
    sqlite3_bind_text(stmt, 10, m.text.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_step(stmt);
    sqlite3_finalize(stmt);
}

void Database::register_coinage(const std::string& run_id, const std::string& term,
                                  VillagerID coiner, uint64_t birth_tick) {
    const char* sql =
        "INSERT OR IGNORE INTO CoinedWords (run_id, term, coiner, birth_tick) VALUES (?, ?, ?, ?);";
    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK) return;
    sqlite3_bind_text(stmt, 1, run_id.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, term.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(stmt, 3, (int)coiner);
    sqlite3_bind_int64(stmt, 4, (sqlite3_int64)birth_tick);
    sqlite3_step(stmt);
    sqlite3_finalize(stmt);
}

void Database::log_hearsay_chain(const std::string& run_id, uint32_t chain_id, uint32_t origin_mem_id,
                                  uint8_t hop, VillagerID src, VillagerID dst, uint16_t src_genome,
                                  uint16_t dst_genome, const std::string& inbound_text,
                                  const std::string& outbound_text, int word_delta, uint64_t tick) {
    const char* sql =
        "INSERT INTO HearsayChain (run_id, chain_id, origin_mem_id, hop, src_villager, dst_villager, "
        "src_genome, dst_genome, inbound_text, outbound_text, content_word_delta, tick) "
        "VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?);";
    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK) return;
    sqlite3_bind_text(stmt, 1, run_id.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(stmt, 2, (int)chain_id);
    sqlite3_bind_int(stmt, 3, (int)origin_mem_id);
    sqlite3_bind_int(stmt, 4, (int)hop);
    sqlite3_bind_int(stmt, 5, (int)src);
    sqlite3_bind_int(stmt, 6, (int)dst);
    sqlite3_bind_int(stmt, 7, (int)src_genome);
    sqlite3_bind_int(stmt, 8, (int)dst_genome);
    sqlite3_bind_text(stmt, 9, inbound_text.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 10, outbound_text.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(stmt, 11, word_delta);
    sqlite3_bind_int64(stmt, 12, (sqlite3_int64)tick);
    sqlite3_step(stmt);
    sqlite3_finalize(stmt);
}

void Database::log_cognition(const std::string& run_id, VillagerID villager_id, uint64_t tick,
                              const std::string& perception_hash, const std::string& raw_thought,
                              const std::string& extracted_verb, int token_count, uint16_t genome_pack,
                              int32_t hunger, int32_t social, int32_t safety, uint64_t seed_used) {
    const char* sql =
        "INSERT INTO CognitionLog (run_id, villager_id, tick, perception_hash, raw_thought, "
        "extracted_verb, token_count, genome_pack, hunger, social, safety, seed_used) "
        "VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?);";
    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK) return;
    sqlite3_bind_text(stmt, 1, run_id.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(stmt, 2, (int)villager_id);
    sqlite3_bind_int64(stmt, 3, (sqlite3_int64)tick);
    sqlite3_bind_text(stmt, 4, perception_hash.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 5, raw_thought.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 6, extracted_verb.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(stmt, 7, token_count);
    sqlite3_bind_int(stmt, 8, (int)genome_pack);
    sqlite3_bind_int(stmt, 9, hunger);
    sqlite3_bind_int(stmt, 10, social);
    sqlite3_bind_int(stmt, 11, safety);
    sqlite3_bind_int64(stmt, 12, (sqlite3_int64)seed_used);
    sqlite3_step(stmt);
    sqlite3_finalize(stmt);
}

void Database::log_belief_birth(const std::string& run_id, uint32_t memory_id, MemType type,
                                 uint64_t birth_tick, int32_t importance, uint8_t hop_count,
                                 uint64_t content_hash) {
    const char* sql =
        "INSERT INTO BeliefSurvival (run_id, memory_id, memory_type, birth_tick, death_tick, "
        "max_importance, hop_count, population_reach, content_hash) VALUES (?, ?, ?, ?, NULL, ?, ?, 1, ?);";
    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK) return;
    static const char* names[] = {"EXPERIENCE", "HEARSAY", "DREAM"};
    sqlite3_bind_text(stmt, 1, run_id.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(stmt, 2, (int)memory_id);
    sqlite3_bind_text(stmt, 3, names[(int)type], -1, SQLITE_TRANSIENT);
    sqlite3_bind_int64(stmt, 4, (sqlite3_int64)birth_tick);
    sqlite3_bind_int(stmt, 5, importance);
    sqlite3_bind_int(stmt, 6, (int)hop_count);
    char hbuf[32]; snprintf(hbuf, sizeof(hbuf), "%016llx", (unsigned long long)content_hash);
    sqlite3_bind_text(stmt, 7, hbuf, -1, SQLITE_TRANSIENT);
    sqlite3_step(stmt);
    sqlite3_finalize(stmt);
}

void Database::mark_belief_death(const std::string& run_id, uint32_t memory_id, uint64_t death_tick,
                                  uint32_t origin_mem_id) {
    int reach = 1;
    const char* csql = "SELECT COUNT(DISTINCT villager_id) FROM MemoryGraph WHERE run_id = ? AND origin_mem_id = ?;";
    sqlite3_stmt* cstmt;
    if (sqlite3_prepare_v2(db, csql, -1, &cstmt, nullptr) == SQLITE_OK) {
        sqlite3_bind_text(cstmt, 1, run_id.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_int(cstmt, 2, (int)origin_mem_id);
        if (sqlite3_step(cstmt) == SQLITE_ROW) reach = sqlite3_column_int(cstmt, 0);
        sqlite3_finalize(cstmt);
    }
    const char* usql = "UPDATE BeliefSurvival SET death_tick = ?, population_reach = ? WHERE run_id = ? AND memory_id = ?;";
    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(db, usql, -1, &stmt, nullptr) != SQLITE_OK) return;
    sqlite3_bind_int64(stmt, 1, (sqlite3_int64)death_tick);
    sqlite3_bind_int(stmt, 2, reach);
    sqlite3_bind_text(stmt, 3, run_id.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(stmt, 4, (int)memory_id);
    sqlite3_step(stmt);
    sqlite3_finalize(stmt);
}

void Database::log_coinage_adoption(const std::string& run_id, const std::string& term,
                                     VillagerID coiner_id, uint16_t coiner_genome, uint64_t birth_tick,
                                     VillagerID adopter_id, uint64_t adoption_tick,
                                     const std::string& context) {
    const char* sql =
        "INSERT INTO CoinageSpread (run_id, term, coiner_id, coiner_genome, birth_tick, adopter_id, "
        "adoption_tick, context) VALUES (?, ?, ?, ?, ?, ?, ?, ?);";
    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK) return;
    sqlite3_bind_text(stmt, 1, run_id.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, term.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(stmt, 3, (int)coiner_id);
    sqlite3_bind_int(stmt, 4, (int)coiner_genome);
    sqlite3_bind_int64(stmt, 5, (sqlite3_int64)birth_tick);
    sqlite3_bind_int(stmt, 6, (int)adopter_id);
    sqlite3_bind_int64(stmt, 7, (sqlite3_int64)adoption_tick);
    sqlite3_bind_text(stmt, 8, context.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_step(stmt);
    sqlite3_finalize(stmt);
}

void Database::log_world_digest(const std::string& run_id, uint64_t tick, uint64_t seed,
                                 uint64_t world_hash, int belief_count, int coined_terms,
                                 int avg_importance, int max_hearsay_depth, uint64_t genome_dist_hash) {
    const char* sql =
        "INSERT INTO WorldDigest (run_id, tick, seed, world_hash, belief_count, coined_terms, "
        "avg_importance, max_hearsay_depth, genome_dist_hash) VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?);";
    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK) return;
    sqlite3_bind_text(stmt, 1, run_id.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int64(stmt, 2, (sqlite3_int64)tick);
    sqlite3_bind_int64(stmt, 3, (sqlite3_int64)seed);
    char wbuf[32]; snprintf(wbuf, sizeof(wbuf), "%016llx", (unsigned long long)world_hash);
    sqlite3_bind_text(stmt, 4, wbuf, -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(stmt, 5, belief_count);
    sqlite3_bind_int(stmt, 6, coined_terms);
    sqlite3_bind_int(stmt, 7, avg_importance);
    sqlite3_bind_int(stmt, 8, max_hearsay_depth);
    char gbuf[32]; snprintf(gbuf, sizeof(gbuf), "%016llx", (unsigned long long)genome_dist_hash);
    sqlite3_bind_text(stmt, 9, gbuf, -1, SQLITE_TRANSIENT);
    sqlite3_step(stmt);
    sqlite3_finalize(stmt);
}

// ============================================================================
// EngineCheckpoint — full live-state serialize/restore (Step 4). POD arrays are raw memcpy
// BLOBs: safe because restore always happens within the same process/binary architecture
// (HF Spaces container reboot on the same image), never cross-platform.
// ============================================================================
void Database::save_checkpoint(const WorldState& w) {
    sqlite3_exec(db, "BEGIN TRANSACTION;", nullptr, nullptr, nullptr);

    auto del = [&](const char* table) {
        std::string sql = std::string("DELETE FROM ") + table + " WHERE run_id = ?;";
        sqlite3_stmt* stmt;
        sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, nullptr);
        sqlite3_bind_text(stmt, 1, w.run_id.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_step(stmt);
        sqlite3_finalize(stmt);
    };
    del("EngineCheckpoint"); del("CheckpointMemory"); del("CheckpointAsyncQueue");
    del("CheckpointCoinedWords"); del("CheckpointTermAdopters");

    {
        const char* sql =
            "INSERT INTO EngineCheckpoint (run_id, tick, next_mem_id, task_seq_counter, genomes, needs, "
            "pos_x, pos_y, holding_food, pcg32_physics, pcg32_cognition, last_dream_tick, "
            "pcg32_event, active_event, event_start_tick) "
            "VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?);";
        sqlite3_stmt* stmt;
        sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr);
        sqlite3_bind_text(stmt, 1, w.run_id.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_int64(stmt, 2, (sqlite3_int64)w.current_tick);
        sqlite3_bind_int(stmt, 3, (int)w.next_mem_id);
        sqlite3_bind_int64(stmt, 4, (sqlite3_int64)w.task_seq_counter);
        sqlite3_bind_blob(stmt, 5, w.genomes, sizeof(w.genomes), SQLITE_TRANSIENT);
        sqlite3_bind_blob(stmt, 6, w.needs, sizeof(w.needs), SQLITE_TRANSIENT);
        sqlite3_bind_blob(stmt, 7, w.pos_x, sizeof(w.pos_x), SQLITE_TRANSIENT);
        sqlite3_bind_blob(stmt, 8, w.pos_y, sizeof(w.pos_y), SQLITE_TRANSIENT);
        sqlite3_bind_blob(stmt, 9, w.holding_food, sizeof(w.holding_food), SQLITE_TRANSIENT);
        sqlite3_bind_blob(stmt, 10, w.pcg32_physics_state, sizeof(w.pcg32_physics_state), SQLITE_TRANSIENT);
        sqlite3_bind_blob(stmt, 11, w.pcg32_cognition_state, sizeof(w.pcg32_cognition_state), SQLITE_TRANSIENT);
        sqlite3_bind_blob(stmt, 12, w.last_dream_tick, sizeof(w.last_dream_tick), SQLITE_TRANSIENT);
        sqlite3_bind_blob(stmt, 13, &w.pcg32_event_state, sizeof(w.pcg32_event_state), SQLITE_TRANSIENT);
        sqlite3_bind_int(stmt, 14, (int)w.active_event);
        sqlite3_bind_int64(stmt, 15, (sqlite3_int64)w.event_start_tick);
        sqlite3_step(stmt);
        sqlite3_finalize(stmt);
    }
    {
        const char* sql =
            "INSERT INTO CheckpointMemory (run_id, villager_id, seq, mem_id, tick, actor_id, importance, "
            "type, source_depth, origin_mem_id, text, retell_count) VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?);";
        sqlite3_stmt* stmt;
        sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr);
        for (uint32_t v = 0; v < MAX_VILLAGERS; ++v) {
            const auto& mems = w.stores[v].all();
            for (size_t seq = 0; seq < mems.size(); ++seq) {
                const auto& m = mems[seq];
                sqlite3_bind_text(stmt, 1, w.run_id.c_str(), -1, SQLITE_TRANSIENT);
                sqlite3_bind_int(stmt, 2, (int)v);
                sqlite3_bind_int64(stmt, 3, (sqlite3_int64)seq);
                sqlite3_bind_int(stmt, 4, (int)m.mem_id);
                sqlite3_bind_int64(stmt, 5, (sqlite3_int64)m.tick);
                sqlite3_bind_int(stmt, 6, (int)m.actor_id);
                sqlite3_bind_int(stmt, 7, m.importance);
                sqlite3_bind_int(stmt, 8, (int)m.type);
                sqlite3_bind_int(stmt, 9, (int)m.source_depth);
                sqlite3_bind_int(stmt, 10, (int)m.origin_mem_id);
                sqlite3_bind_text(stmt, 11, m.text.c_str(), -1, SQLITE_TRANSIENT);
                sqlite3_bind_int(stmt, 12, (int)m.retell_count);
                sqlite3_step(stmt);
                sqlite3_reset(stmt);
            }
        }
        sqlite3_finalize(stmt);
    }
    {
        const char* sql =
            "INSERT INTO CheckpointAsyncQueue (run_id, seq, villager_id, fold_tick, task_seq_id, kind, "
            "verb, target, mtype, source_depth, importance, birth_tick, mem_id, origin_mem_id, text) "
            "VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?);";
        sqlite3_stmt* stmt;
        sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr);
        for (size_t seq = 0; seq < w.async_queue.size(); ++seq) {
            const auto& t = w.async_queue[seq];
            sqlite3_bind_text(stmt, 1, w.run_id.c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_int64(stmt, 2, (sqlite3_int64)seq);
            sqlite3_bind_int(stmt, 3, (int)t.villager_id);
            sqlite3_bind_int64(stmt, 4, (sqlite3_int64)t.fold_tick);
            sqlite3_bind_int64(stmt, 5, (sqlite3_int64)t.task_seq_id);
            sqlite3_bind_int(stmt, 6, (int)t.kind);
            sqlite3_bind_int(stmt, 7, (int)t.verb);
            sqlite3_bind_int(stmt, 8, (int)t.target);
            sqlite3_bind_int(stmt, 9, (int)t.mtype);
            sqlite3_bind_int(stmt, 10, (int)t.source_depth);
            sqlite3_bind_int(stmt, 11, t.importance);
            sqlite3_bind_int64(stmt, 12, (sqlite3_int64)t.birth_tick);
            sqlite3_bind_int(stmt, 13, (int)t.mem_id);
            sqlite3_bind_int(stmt, 14, (int)t.origin_mem_id);
            sqlite3_bind_text(stmt, 15, t.text.c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_step(stmt);
            sqlite3_reset(stmt);
        }
        sqlite3_finalize(stmt);
    }
    {
        const char* sql =
            "INSERT INTO CheckpointCoinedWords (run_id, term, coiner, coiner_genome, birth_tick) "
            "VALUES (?, ?, ?, ?, ?);";
        sqlite3_stmt* stmt;
        sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr);
        for (const auto& kv : w.coinage_origin) {
            sqlite3_bind_text(stmt, 1, w.run_id.c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_text(stmt, 2, kv.first.c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_int(stmt, 3, (int)kv.second.coiner);
            sqlite3_bind_int(stmt, 4, (int)kv.second.coiner_genome);
            sqlite3_bind_int64(stmt, 5, (sqlite3_int64)kv.second.birth_tick);
            sqlite3_step(stmt);
            sqlite3_reset(stmt);
        }
        sqlite3_finalize(stmt);
    }
    {
        const char* sql = "INSERT INTO CheckpointTermAdopters (run_id, term, villager_id) VALUES (?, ?, ?);";
        sqlite3_stmt* stmt;
        sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr);
        for (const auto& kv : w.term_adopters) {
            for (VillagerID a : kv.second) {
                sqlite3_bind_text(stmt, 1, w.run_id.c_str(), -1, SQLITE_TRANSIENT);
                sqlite3_bind_text(stmt, 2, kv.first.c_str(), -1, SQLITE_TRANSIENT);
                sqlite3_bind_int(stmt, 3, (int)a);
                sqlite3_step(stmt);
                sqlite3_reset(stmt);
            }
        }
        sqlite3_finalize(stmt);
    }

    sqlite3_exec(db, "COMMIT;", nullptr, nullptr, nullptr);
}

bool Database::load_checkpoint(WorldState& w) {
    sqlite3_stmt* stmt;
    {
        const char* sql =
            "SELECT tick, next_mem_id, task_seq_counter, genomes, needs, pos_x, pos_y, "
            "holding_food, pcg32_physics, pcg32_cognition, last_dream_tick, "
            "pcg32_event, active_event, event_start_tick "
            "FROM EngineCheckpoint WHERE run_id = ?;";
        if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK) return false;
        sqlite3_bind_text(stmt, 1, w.run_id.c_str(), -1, SQLITE_TRANSIENT);
        if (sqlite3_step(stmt) != SQLITE_ROW) { sqlite3_finalize(stmt); return false; }

        w.current_tick = (uint64_t)sqlite3_column_int64(stmt, 0);
        w.next_mem_id = (uint32_t)sqlite3_column_int(stmt, 1);
        w.task_seq_counter = (uint64_t)sqlite3_column_int64(stmt, 2);

        auto copy_blob = [&](int col, void* dst, size_t expected) {
            const void* blob = sqlite3_column_blob(stmt, col);
            size_t n = (size_t)sqlite3_column_bytes(stmt, col);
            if (blob && n == expected) std::memcpy(dst, blob, expected);
        };
        copy_blob(3, w.genomes, sizeof(w.genomes));
        copy_blob(4, w.needs, sizeof(w.needs));
        copy_blob(5, w.pos_x, sizeof(w.pos_x));
        copy_blob(6, w.pos_y, sizeof(w.pos_y));
        copy_blob(7, w.holding_food, sizeof(w.holding_food));
        copy_blob(8, w.pcg32_physics_state, sizeof(w.pcg32_physics_state));
        copy_blob(9, w.pcg32_cognition_state, sizeof(w.pcg32_cognition_state));
        copy_blob(10, w.last_dream_tick, sizeof(w.last_dream_tick));
        copy_blob(11, &w.pcg32_event_state, sizeof(w.pcg32_event_state));
        w.active_event = (VillageEvent)sqlite3_column_int(stmt, 12);
        w.event_start_tick = (uint64_t)sqlite3_column_int64(stmt, 13);
        sqlite3_finalize(stmt);
    }

    std::vector<std::vector<MemoryEntry>> per_villager(MAX_VILLAGERS);
    {
        const char* sql =
            "SELECT villager_id, mem_id, tick, actor_id, importance, type, source_depth, "
            "origin_mem_id, text, retell_count FROM CheckpointMemory WHERE run_id = ? ORDER BY villager_id, seq ASC;";
        sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr);
        sqlite3_bind_text(stmt, 1, w.run_id.c_str(), -1, SQLITE_TRANSIENT);
        while (sqlite3_step(stmt) == SQLITE_ROW) {
            uint32_t vid = (uint32_t)sqlite3_column_int(stmt, 0);
            MemoryEntry m;
            m.mem_id = (uint32_t)sqlite3_column_int(stmt, 1);
            m.tick = (uint64_t)sqlite3_column_int64(stmt, 2);
            m.actor_id = (uint32_t)sqlite3_column_int(stmt, 3);
            m.importance = sqlite3_column_int(stmt, 4);
            m.type = (MemType)sqlite3_column_int(stmt, 5);
            m.source_depth = (uint8_t)sqlite3_column_int(stmt, 6);
            m.origin_mem_id = (uint32_t)sqlite3_column_int(stmt, 7);
            const char* txt = (const char*)sqlite3_column_text(stmt, 8);
            m.text = txt ? txt : "";
            m.retell_count = (uint16_t)sqlite3_column_int(stmt, 9);
            if (vid < MAX_VILLAGERS) per_villager[vid].push_back(std::move(m));
        }
        sqlite3_finalize(stmt);
    }
    for (uint32_t v = 0; v < MAX_VILLAGERS; ++v) w.stores[v].load_raw(std::move(per_villager[v]));

    w.async_queue.clear();
    {
        const char* sql =
            "SELECT villager_id, fold_tick, task_seq_id, kind, verb, target, mtype, "
            "source_depth, importance, birth_tick, mem_id, origin_mem_id, text "
            "FROM CheckpointAsyncQueue WHERE run_id = ? ORDER BY seq ASC;";
        sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr);
        sqlite3_bind_text(stmt, 1, w.run_id.c_str(), -1, SQLITE_TRANSIENT);
        while (sqlite3_step(stmt) == SQLITE_ROW) {
            DeferredTask t;
            t.villager_id = (VillagerID)sqlite3_column_int(stmt, 0);
            t.fold_tick = (uint64_t)sqlite3_column_int64(stmt, 1);
            t.task_seq_id = (uint64_t)sqlite3_column_int64(stmt, 2);
            t.kind = (TaskKind)sqlite3_column_int(stmt, 3);
            t.verb = (Verb)sqlite3_column_int(stmt, 4);
            t.target = (VillagerID)sqlite3_column_int(stmt, 5);
            t.mtype = (MemType)sqlite3_column_int(stmt, 6);
            t.source_depth = (uint8_t)sqlite3_column_int(stmt, 7);
            t.importance = sqlite3_column_int(stmt, 8);
            t.birth_tick = (uint64_t)sqlite3_column_int64(stmt, 9);
            t.mem_id = (uint32_t)sqlite3_column_int(stmt, 10);
            t.origin_mem_id = (uint32_t)sqlite3_column_int(stmt, 11);
            const char* txt = (const char*)sqlite3_column_text(stmt, 12);
            t.text = txt ? txt : "";
            w.async_queue.push_back(std::move(t));
        }
        sqlite3_finalize(stmt);
    }

    w.coinage_origin.clear(); w.coined_words.clear();
    {
        const char* sql = "SELECT term, coiner, coiner_genome, birth_tick FROM CheckpointCoinedWords WHERE run_id = ?;";
        sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr);
        sqlite3_bind_text(stmt, 1, w.run_id.c_str(), -1, SQLITE_TRANSIENT);
        while (sqlite3_step(stmt) == SQLITE_ROW) {
            std::string term = (const char*)sqlite3_column_text(stmt, 0);
            WorldState::CoinageOrigin o;
            o.coiner = (VillagerID)sqlite3_column_int(stmt, 1);
            o.coiner_genome = (uint16_t)sqlite3_column_int(stmt, 2);
            o.birth_tick = (uint64_t)sqlite3_column_int64(stmt, 3);
            w.coinage_origin[term] = o;
            w.coined_words.insert(term);
        }
        sqlite3_finalize(stmt);
    }

    w.term_adopters.clear();
    {
        const char* sql = "SELECT term, villager_id FROM CheckpointTermAdopters WHERE run_id = ?;";
        sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr);
        sqlite3_bind_text(stmt, 1, w.run_id.c_str(), -1, SQLITE_TRANSIENT);
        while (sqlite3_step(stmt) == SQLITE_ROW) {
            std::string term = (const char*)sqlite3_column_text(stmt, 0);
            VillagerID vid = (VillagerID)sqlite3_column_int(stmt, 1);
            w.term_adopters[term].insert(vid);
        }
        sqlite3_finalize(stmt);
    }

    return true;
}

int Database::count_rows(const std::string& table, const std::string& run_id) {
    std::string sql = "SELECT COUNT(*) FROM " + table + " WHERE run_id = ?;";
    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, nullptr) != SQLITE_OK) return -1;
    sqlite3_bind_text(stmt, 1, run_id.c_str(), -1, SQLITE_TRANSIENT);
    int n = (sqlite3_step(stmt) == SQLITE_ROW) ? sqlite3_column_int(stmt, 0) : -1;
    sqlite3_finalize(stmt);
    return n;
}

int Database::count_memories(const std::string& run_id) {
    const char* sql = "SELECT COUNT(*) FROM MemoryGraph WHERE run_id = ?;";
    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK) return -1;
    sqlite3_bind_text(stmt, 1, run_id.c_str(), -1, SQLITE_TRANSIENT);
    int n = (sqlite3_step(stmt) == SQLITE_ROW) ? sqlite3_column_int(stmt, 0) : -1;
    sqlite3_finalize(stmt);
    return n;
}

// Live-DB -> staging-file copy via SQLite's Online Backup API. Never called from a signal
// handler directly (not async-signal-safe) — the --serve loop calls this at a tick boundary
// in response to a flag a handler set. Overwrites dest_path with a fresh single-step copy.
bool Database::backup_to(const std::string& dest_path) {
    sqlite3* dest = nullptr;
    if (sqlite3_open(dest_path.c_str(), &dest) != SQLITE_OK) {
        if (dest) sqlite3_close(dest);
        return false;
    }
    sqlite3_backup* bk = sqlite3_backup_init(dest, "main", db, "main");
    bool ok = false;
    if (bk) {
        ok = (sqlite3_backup_step(bk, -1) == SQLITE_DONE);
        sqlite3_backup_finish(bk);
    }
    sqlite3_close(dest);
    return ok;
}

} // namespace tbv
