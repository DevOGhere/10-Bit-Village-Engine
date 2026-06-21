#include "infra/db.h"
#include <sqlite3.h>
#include <stdexcept>
#include <iostream>

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
        "   text TEXT"
        ");"
        "CREATE INDEX IF NOT EXISTS idx_mem_villager_tick ON MemoryGraph(villager_id, tick);"
        "CREATE TABLE IF NOT EXISTS CoinedWords ("
        "   run_id TEXT,"
        "   term TEXT,"
        "   coiner INTEGER,"
        "   birth_tick INTEGER,"
        "   PRIMARY KEY(run_id, term)"
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

void Database::persist_memory(const std::string& run_id, VillagerID villager_id, const MemoryEntry& m) {
    const char* sql =
        "INSERT INTO MemoryGraph (run_id, mem_id, villager_id, tick, actor_id, importance, type, source_depth, text) "
        "VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?);";
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
    sqlite3_bind_text(stmt, 9, m.text.c_str(), -1, SQLITE_TRANSIENT);
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

int Database::count_memories(const std::string& run_id) {
    const char* sql = "SELECT COUNT(*) FROM MemoryGraph WHERE run_id = ?;";
    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK) return -1;
    sqlite3_bind_text(stmt, 1, run_id.c_str(), -1, SQLITE_TRANSIENT);
    int n = (sqlite3_step(stmt) == SQLITE_ROW) ? sqlite3_column_int(stmt, 0) : -1;
    sqlite3_finalize(stmt);
    return n;
}

} // namespace tbv
