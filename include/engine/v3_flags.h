#pragma once
// V3 (Fix E Option 2 + Fix G) runtime flags and bias tables. See the vault note
// "10-Bit Village -- V3 Plan (Fix E + Fix G + Cutover)".
//
// Both flags default OFF in code, so every existing gate (--phase0_gate, --phase3,
// --checkpoint_gate, ...) keeps reproducing the Run 3 A-F canonical hashes untouched.
// A deployment turns them on with TBV_FIX_G=1 / TBV_FIX_E=1 in the process environment.
// Flags are read once at process start and must not change during a run: flipping one on a
// checkpointed world would desync it.
//
// All randomness here is hash-seeded jitter on (villager, tick, salt): never a pcg32 stream
// draw, so draw order and the EngineCheckpoint schema are untouched.
#include <cstdint>
#include <cstdlib>
#include <cstring>

namespace tbv {

struct V3Flags { bool fixG = false; bool fixE = false; };

inline V3Flags& v3_flags() { static V3Flags f; return f; }

inline bool v3_env_on(const char* name) {
    const char* v = std::getenv(name);
    return v && (std::strcmp(v, "1") == 0 || std::strcmp(v, "true") == 0 || std::strcmp(v, "on") == 0);
}

inline void v3_flags_from_env() {
    v3_flags().fixG = v3_env_on("TBV_FIX_G");
    v3_flags().fixE = v3_env_on("TBV_FIX_E");
}

// Fix G: max carried food units per villager (counts are 0..CAP, stored as uint8_t).
constexpr uint8_t FOOD_CARRY_CAP = 2;

// Fix E: genome bias tables, indexed by trait level 0..3. Percentages of a hash roll in [0,100).
constexpr int GIVE_BIAS_PCT[4]        = { 0, 3, 8, 15 };   // generosity: upgrade MOVE_TO/WAIT -> GIVE
constexpr int32_t GIVE_COMFORT_HUNGER = 40000;             // only when comfortably fed
constexpr int SUSPICION_DRIFT_THR[4]  = { 4, 3, 3, 2 };    // suspicion: field-match count that forces drift
constexpr int CURIOUS_TWIST_PCT[4]    = { 0, 0, 20, 40 };  // curiosity: inject a coined word anyway
constexpr int SHY_SKIP_PCT[4]         = { 30, 15, 0, 0 };  // sociability: skip a hearsay turn
constexpr int SPEAK_UPGRADE_PCT[4]    = { 0, 0, 5, 10 };   // sociability: upgrade MOVE_TO -> SPEAK

// Deterministic 0..99 roll from (villager, tick, salt). splitmix64 finalizer, no state.
inline int v3_roll(uint64_t villager, uint64_t tick, uint64_t salt) {
    uint64_t z = (villager << 40) ^ (tick * 0x9E3779B97F4A7C15ULL) ^ (salt * 0xBF58476D1CE4E5B9ULL);
    z += 0x9E3779B97F4A7C15ULL;
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ULL;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBULL;
    z = z ^ (z >> 31);
    return (int)(z % 100ULL);
}

} // namespace tbv
