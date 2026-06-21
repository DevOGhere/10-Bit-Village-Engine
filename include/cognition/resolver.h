#pragma once
// Hybrid C&B action resolver (header-only, deterministic, no LLM).
// LLM free-text intent -> extract verb; if valid-in-env it wins (B / agency);
// else fall back to highest biological urge (C). Survival floor overrides at lethal extremes.
#include <string>
#include <algorithm>
#include <initializer_list>
#include "engine/needs.h"

namespace tbv {

enum class Verb { EAT, MOVE_TO, GIVE, SPEAK, WAIT, NONE };
inline const char* verb_name(Verb v) {
    switch (v) { case Verb::EAT:return "EAT"; case Verb::MOVE_TO:return "MOVE_TO";
        case Verb::GIVE:return "GIVE"; case Verb::SPEAK:return "SPEAK";
        case Verb::WAIT:return "WAIT"; default:return "NONE"; }
}

struct Env { bool holding_food = false, food_in_reach = false, neighbour_present = false; };

// A villager's grounded situation rendered for the LLM + the engine facts the resolver needs.
struct PerceptionContext { std::string situation; Needs needs; Env env; };

constexpr int32_t HUNGER_CRIT = 5000; // 5% of fixed-point 100000 -> survival floor

inline Verb extract_verb(std::string t) {
    std::transform(t.begin(), t.end(), t.begin(), ::tolower);
    
    auto has_any = [&](std::initializer_list<const char*> ks){
        for (auto k : ks) if (t.find(k) != std::string::npos) return true; return false; };

    // Negation guard (QA-proposed, Builder-refined). A 1-sentence intent containing a
    // strong negative is treated as "not clearly proposing an action" -> return NONE so the
    // biological URGE takes over. This is conservative BY DESIGN: the LLM wins only when it
    // clearly proposes a valid action; negated/ambiguous phrasing defers to the drive.
    // ("nothing" dropped — over-broad; "do nothing"->WAIT is a legitimate action.)
    if (has_any({" not ", "don't", "do not", "never", "cannot", "can't"})) {
        return Verb::NONE;
    }

    if (has_any({"eat","bite","chew","devour","swallow","taste"}))                          return Verb::EAT;
    if (has_any({"give","offer","hand ","share","pass the","trade"}))                        return Verb::GIVE;
    if (has_any({"run","flee","head to","walk","move","go to","approach","step","head toward","dash"})) return Verb::MOVE_TO;
    if (has_any({"say","speak","talk","ask","tell","greet","whisper","call out"}))           return Verb::SPEAK;
    if (has_any({"sit","wait","stay","freeze","still","rest","watch","remain"}))             return Verb::WAIT;
    return Verb::NONE;
}

inline Verb biological_urge(const Needs& n, const Env& e) {
    int32_t m = std::min({n.hunger, n.social, n.safety});
    if (m == n.safety) return Verb::MOVE_TO;                                  // unsafe -> flee
    if (m == n.hunger) return (e.holding_food || e.food_in_reach) ? Verb::EAT : Verb::MOVE_TO;
    return e.neighbour_present ? Verb::SPEAK : Verb::MOVE_TO;                  // lonely
}

inline bool valid_in_env(Verb v, const Env& e) {
    switch (v) {
        case Verb::EAT:   return e.holding_food || e.food_in_reach;
        case Verb::GIVE:  return e.neighbour_present && e.holding_food;
        case Verb::SPEAK: return e.neighbour_present;
        case Verb::MOVE_TO: case Verb::WAIT: return true;
        default: return false;
    }
}

struct Resolved { Verb verb; const char* source; }; // source: FLOOR | LLM | URGE

inline Resolved resolve(const std::string& intent, const Needs& n, const Env& e) {
    if (n.hunger <= HUNGER_CRIT && (e.holding_food || e.food_in_reach)) return { Verb::EAT, "FLOOR" };
    Verb v = extract_verb(intent);
    if (v != Verb::NONE && valid_in_env(v, e)) return { v, "LLM" };
    return { biological_urge(n, e), "URGE" };
}

} // namespace tbv
