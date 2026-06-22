#pragma once
// Hybrid C&B action resolver (header-only, deterministic, no LLM).
// LLM free-text intent -> extract verb; if valid-in-env it wins (B / agency);
// else fall back to highest biological urge (C). Survival floor overrides at lethal extremes.
#include <string>
#include <vector>
#include <set>
#include <cctype>
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

// ── Hearsay structured extraction (Segment 2 of the two-segment retelling, MSG_062 §2) ──
// The free retelling prose is reduced to a {actor, action, location, twist} struct so the
// engine can track belief lineage + measure drift. Heuristic + deterministic, same spirit
// as extract_verb (shape/keyword based, NOT real NER). Fuzzy by nature — exactness doesn't
// matter, reproducibility does. QA red-team target (like extract_verb's negation guard).
struct HearsayFields { std::string actor, action, location, twist; };

inline HearsayFields extract_hearsay_fields(const std::string& text) {
    HearsayFields f;
    std::vector<std::string> words;
    { std::string w;
      for (char c : text) {
          if (std::isalnum((unsigned char)c)) w += c;
          else if (!w.empty()) { words.push_back(w); w.clear(); } }
      if (!w.empty()) words.push_back(w); }

    auto lc = [](std::string s){ for (auto& c : s) c = (char)std::tolower((unsigned char)c); return s; };
    auto is_proper = [](const std::string& w){
        return w.size() > 1 && std::isupper((unsigned char)w[0])
            && std::any_of(w.begin()+1, w.end(), [](char c){ return std::islower((unsigned char)c); }); };
    static const std::set<std::string> fn = {
        "The","A","An","I","You","Your","It","They","We","He","She","His","Her","This","That","There","Their"};

    // actor = first proper-noun token that isn't a sentence-initial function word
    for (const auto& w : words)
        if (is_proper(w) && !fn.count(w)) { f.actor = lc(w); break; }

    // action = first verb-ish token
    static const std::set<std::string> verbs = {
        "eat","ate","give","gave","offer","offered","share","shared","run","ran","flee","fled",
        "walk","walked","move","moved","go","went","say","said","speak","spoke","tell","told",
        "ask","asked","whisper","whispered","warn","warned","hide","hid","watch","watched","see",
        "saw","find","found","fear","feared","glow","glowed","appear","appeared","vanish","vanished"};
    for (const auto& w : words) { std::string l = lc(w); if (verbs.count(l)) { f.action = l; break; } }

    // location = token after a locative preposition, else a 2nd distinct proper noun
    static const std::set<std::string> loc_prep = {
        "near","at","in","behind","beyond","by","inside","under","beside","atop","within"};
    for (size_t i = 0; i + 1 < words.size(); ++i)
        if (loc_prep.count(lc(words[i]))) { f.location = lc(words[i+1]); break; }
    if (f.location.empty())
        for (const auto& w : words)
            if (is_proper(w) && !fn.count(w) && lc(w) != f.actor) { f.location = lc(w); break; }

    // twist = the longest remaining content word (a salient detail carrier)
    std::string best;
    for (const auto& w : words) { std::string l = lc(w);
        if (l.size() > best.size() && l != f.actor && l != f.action && l != f.location && !fn.count(w))
            best = l; }
    f.twist = best;
    return f;
}

struct Resolved { Verb verb; const char* source; }; // source: FLOOR | LLM | URGE

inline Resolved resolve(const std::string& intent, const Needs& n, const Env& e) {
    if (n.hunger <= HUNGER_CRIT && (e.holding_food || e.food_in_reach)) return { Verb::EAT, "FLOOR" };
    Verb v = extract_verb(intent);
    if (v != Verb::NONE && valid_in_env(v, e)) return { v, "LLM" };
    return { biological_urge(n, e), "URGE" };
}

} // namespace tbv
