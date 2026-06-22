#pragma once
// Hearsay-degradation fix (MSG_062 §2) — the cure for hearsay stagnation, where 360M
// echoes a rumour verbatim instead of mutating it into myth. Three deterministic stages:
//   1. degrade()      — pre-LLM salience mask: faded memories lose proper nouns/numbers so
//                       the reteller must RECONSTRUCT (genome-biased) rather than parrot.
//   2. retell()       — LLM two-segment: free reconstruction + field extraction (in the bridge).
//   3. forced-novelty — post-LLM: if the retelling still matches the source in >=3/4 fields,
//                       inject a genome-keyed distortion word. Guarantees drift even when the
//                       model won't provide it.
// The distortion seed is a HASH (splitmix64), never an RNG-stream draw — same rule as the
// LOCK 2 importance jitter; a stream pull here would desync per-villager dispatch order.
//
// hearsay_hop() is the SINGLE source of truth for one hop, shared by the engine (world.cpp)
// and the --hearsay_fix gate, so the gate can never pass on logic the engine doesn't run
// (the vacuous-gate trap this project hit at G1/Q1).
#include <string>
#include <vector>
#include <set>
#include <algorithm>
#include <cctype>
#include "cognition/cognitive_store.h"   // MemoryEntry, CognitiveStore::effective_importance
#include "cognition/resolver.h"          // HearsayFields, extract_hearsay_fields
#include "cognition/llama_bridge.h"      // LlamaBridge, RetellResult
#include "engine/genome.h"               // Genome
#include "engine/world.h"                // splitmix64_avalanche, VillagerID

namespace tbv {

// Below this effective-importance a memory is "faded" and gets masked. Tunable —
// QA red-team target, like the prior (disproven) stagnation thresholds.
constexpr int32_t DEGRADE_THRESHOLD = 700;

// Salience-proportional detail mask. Vivid (fresh/important) memories pass through intact;
// faded ones have proper nouns -> "someone" and numbers -> "some", forcing reconstruction.
// Heuristic + deterministic. Note: a proper noun at sentence-start is NOT masked (can't be
// told from a capitalized function word without NER) — forced-novelty backstops that miss.
inline std::string degrade(const MemoryEntry& m, uint64_t current_tick) {
    if (CognitiveStore::effective_importance(m, current_tick) >= DEGRADE_THRESHOLD)
        return m.text;

    static const std::set<std::string> fn = {
        "The","A","An","I","You","Your","It","They","We","He","She","His","Her",
        "This","That","There","Their"};
    auto is_proper = [](const std::string& w){
        return w.size() > 1 && std::isupper((unsigned char)w[0])
            && std::any_of(w.begin()+1, w.end(), [](char c){ return std::islower((unsigned char)c); }); };

    std::string out, word;
    bool sstart = true;                       // is the next word sentence-initial?
    auto flush = [&](){
        if (word.empty()) return;
        bool num = std::all_of(word.begin(), word.end(),
                               [](char c){ return std::isdigit((unsigned char)c); });
        if (num)                                              out += "some";
        else if (is_proper(word) && !fn.count(word) && !sstart) out += "someone";
        else                                                 out += word;
        sstart = false;
        word.clear();
    };
    for (char c : m.text) {
        if (std::isalnum((unsigned char)c)) word += c;
        else { flush(); out += c; if (c=='.'||c=='!'||c=='?') sstart = true; }
    }
    flush();
    return out;
}

// Small fixed distortion vocabulary, picked by hash (deterministic). Injected only when a
// retelling stayed too close to its source — the genome-keyed seed makes the choice depend
// on the reteller's personality, so different villagers mutate the same rumour differently.
inline std::string distortion_word(uint64_t hash_seed) {
    static const char* words[] = {
        "glowing","cursed","whispering","ancient","hollow","burning","frozen","twin",
        "weeping","golden","shattered","singing","crimson","buried","watchful","nameless"};
    return words[hash_seed % (sizeof(words)/sizeof(words[0]))];
}

// How many of {actor, action, location, twist} are identical (non-empty) between two fields.
inline int field_match_count(const HearsayFields& a, const HearsayFields& b) {
    int n = 0;
    if (!a.actor.empty()    && a.actor    == b.actor)    n++;
    if (!a.action.empty()   && a.action   == b.action)   n++;
    if (!a.location.empty() && a.location == b.location) n++;
    if (!a.twist.empty()    && a.twist    == b.twist)    n++;
    return n;
}

// Classic edit distance (row-rolled DP). Used by the gate to assert no adjacent hop froze
// verbatim — the exact failure the prior Levenshtein-endpoints-only design missed.
inline int levenshtein(const std::string& a, const std::string& b) {
    std::vector<int> prev(b.size() + 1), cur(b.size() + 1);
    for (size_t j = 0; j <= b.size(); ++j) prev[j] = (int)j;
    for (size_t i = 1; i <= a.size(); ++i) {
        cur[0] = (int)i;
        for (size_t j = 1; j <= b.size(); ++j) {
            int cost = (a[i-1] == b[j-1]) ? 0 : 1;
            cur[j] = std::min({ prev[j] + 1, cur[j-1] + 1, prev[j-1] + cost });
        }
        std::swap(prev, cur);
    }
    return prev[b.size()];
}

// One full hearsay hop: degrade -> two-segment retell -> forced-novelty fallback.
inline RetellResult hearsay_hop(LlamaBridge& bridge, VillagerID listener,
                                const MemoryEntry& heard, const Genome& g,
                                uint64_t tick, uint64_t seed) {
    std::string   degraded = degrade(heard, tick);
    HearsayFields inbound  = extract_hearsay_fields(heard.text);   // the source, pre-degradation
    RetellResult  rr       = bridge.retell(listener, degraded, seed);

    if (field_match_count(inbound, rr.fields) >= 3) {             // too faithful -> force drift
        uint64_t dseed = splitmix64_avalanche(
            (uint64_t)tick ^ ((uint64_t)listener << 20) ^ ((uint64_t)g.pack() << 40));
        std::string dw = distortion_word(dseed);
        rr.out_text += " They say it is " + dw + ".";
        rr.fields.twist = dw;
        rr.distortion_injected = true;
    }
    return rr;
}

} // namespace tbv
