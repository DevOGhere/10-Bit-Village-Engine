// resolver.cpp — Hybrid C&B action resolver (TAILS). Pure C++, deterministic, no LLM.
// LLM free-text intent -> extract verb; if valid-in-env it wins (B), else fall back to
// highest biological urge (C). Tested on the 3 captured vector free-texts + edge cases.
#include <string>
#include <array>
#include <iostream>
#include <algorithm>

enum Verb { EAT, MOVE_TO, GIVE, SPEAK, WAIT, NONE };
static const char* VN[] = {"EAT","MOVE_TO","GIVE","SPEAK","WAIT","NONE"};

struct Needs { int hunger, social, safety; }; // fixed-point %, 100=full/safe, 0=critical
struct Env { bool holding_food, food_in_reach, neighbour_present; };

// B: extract verb from free text by keyword (priority order). Returns NONE if none found.
static Verb extract_verb(std::string t) {
    std::transform(t.begin(), t.end(), t.begin(), ::tolower);
    auto has = [&](std::initializer_list<const char*> ks){ for (auto k:ks) if (t.find(k)!=std::string::npos) return true; return false; };
    if (has({"eat","bite","chew","devour","swallow","taste"}))               return EAT;
    if (has({"give","offer","hand ","share","pass the","trade"}))            return GIVE;
    if (has({"run","flee","head to","walk","move","go to","approach","step","head toward","dash"})) return MOVE_TO;
    if (has({"say","speak","talk","ask","tell","greet","whisper","call out"})) return SPEAK;
    if (has({"sit","wait","stay","freeze","still","rest","watch","remain"}))  return WAIT;
    return NONE;
}

// C: deterministic biological urge from needs (lowest need drives the reflex).
static Verb biological_urge(const Needs& n, const Env& e) {
    int m = std::min({n.hunger, n.social, n.safety});
    if (m == n.safety)  return MOVE_TO;                 // unsafe -> flee
    if (m == n.hunger)  return (e.holding_food||e.food_in_reach)? EAT : MOVE_TO; // hungry -> eat/seek
    return e.neighbour_present? SPEAK : MOVE_TO;        // lonely -> talk/seek company
}

// is an extracted verb actually valid to execute in this env?
static bool valid_in_env(Verb v, const Env& e) {
    switch (v) {
        case EAT:  return e.holding_food || e.food_in_reach;
        case GIVE: return e.neighbour_present && (e.holding_food);
        case SPEAK:return e.neighbour_present;
        case MOVE_TO: case WAIT: return true;
        default: return false;
    }
}

struct Out { Verb v; const char* src; };
static Out resolve(const std::string& intent, const Needs& n, const Env& e) {
    // survival floor first (lethal extreme overrides everything)
    if (n.hunger <= 5 && (e.holding_food||e.food_in_reach)) return {EAT, "FLOOR"};
    Verb v = extract_verb(intent);
    if (v != NONE && valid_in_env(v, e)) return {v, "LLM"};
    return {biological_urge(n,e), "URGE"};
}

int main() {
    struct T { const char* name; std::string intent; Needs n; Env e; };
    std::array<T,6> ts = {{
      {"A Glutton free-text (wanders)", "take a deep breath and prepare to head to the ancient hut finding sustenance",
        {3,60,70}, {true,true,false}},                 // hunger critical(3) + holding food
      {"B Socialite free-text (glances)", "turn my head and turn my eyes to the side to sneak a glimpse",
        {70,5,80}, {true,false,true}},                 // social critical, neighbour present
      {"C Paranoid free-text (freezes)", "sit up straight eyes fixed on the stranger nonchalant demeanor",
        {60,40,8}, {false,false,true}},                // safety low
      {"D clear give", "I offer Borin the loaf of bread", {70,30,80}, {true,false,true}},
      {"E gibberish", "the damp earth and decaying leaves and faint tang", {50,50,50}, {false,false,false}},
      {"F starving empty-handed", "I gaze at the clouds", {2,50,60}, {false,false,false}},
    }};
    for (auto& t : ts) {
        Out o = resolve(t.intent, t.n, t.e);
        std::cout << "[" << t.name << "]  -> " << VN[o.v] << "  (" << o.src << ")\n";
    }
    return 0;
}
