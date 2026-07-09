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
//
// §B6-1 (Run 2 Plan, QA-approved packet 102 item 4): `coined_words` (nullptr = old
// behaviour, keeps every pre-existing call site/gate compilable unchanged) is the village's
// own in-RAM set of invented terms (post-B5-filter, so it's clean) -- a proper noun that's a
// real village coinage passes through instead of degrading to "someone", so names like
// "Erebo" survive memory fade and keep circulating instead of being erased by the exact
// mechanism meant to force reconstruction of everything else.
inline std::string degrade(const MemoryEntry& m, uint64_t current_tick,
                            const std::set<std::string>* coined_words = nullptr) {
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
        if (num) { out += "some"; sstart = false; word.clear(); return; }
        if (is_proper(word) && !fn.count(word) && !sstart) {
            std::string lw = word;
            for (char& c : lw) c = (char)std::tolower((unsigned char)c);
            if (coined_words && coined_words->count(lw)) out += word;   // §B6-1: spare it
            else                                          out += "someone";
        } else {
            out += word;
        }
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

// Distortion vocabulary, picked by hash (deterministic). Injected only when a retelling
// stayed too close to its source — the genome-keyed seed makes the choice depend on the
// reteller's personality, so different villagers mutate the same rumour differently.
//
// §B6-2 (Run 2 Plan, QA-approved packet 102 item 4 — "a culture mutating itself via its own
// coinages IS the thesis"): draws from the village's OWN coined_words first (post-B5-filter,
// deterministic — plain std::set iterates in lexicographic order, platform-invariant, so
// `hash_seed % size` + std::advance is a reproducible pick, zero RNG-stream involvement,
// same rule as every other hash-seeded choice in this file). Falls back to the fixed list
// while the set is empty (early run, nothing coined yet) or absent (nullptr = old behaviour).
// This REPLACES the fixed-16-word mystical-adjective monoculture pump (re-injected 27k+
// times in Run 1) with the invented-language feedback loop the thesis actually describes —
// world.cpp's coinage comment updated accordingly, this is a deliberate contract change.
inline std::string distortion_word(uint64_t hash_seed, const std::set<std::string>* coined_words = nullptr) {
    if (coined_words && !coined_words->empty()) {
        size_t idx = hash_seed % coined_words->size();
        auto it = coined_words->begin();
        std::advance(it, (long)idx);
        return *it;
    }
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

// Count of alpha tokens (len>2, lowercase) present in `outbound` but absent from `inbound` —
// HearsayChain's content_word_delta (Step 4): how much NEW content this retelling introduced,
// vs. carrying the same words forward. Order-insensitive by design (a reordered sentence with
// no new words is not "new content").
inline int content_word_delta(const std::string& inbound, const std::string& outbound) {
    auto tokset = [](const std::string& s) {
        std::set<std::string> out;
        std::string cur;
        auto flush = [&] { if (cur.size() > 2) out.insert(cur); cur.clear(); };
        for (char c : s) {
            if (std::isalpha((unsigned char)c)) cur += (char)std::tolower((unsigned char)c);
            else flush();
        }
        flush();
        return out;
    };
    std::set<std::string> in = tokset(inbound), out = tokset(outbound);
    int delta = 0;
    for (const auto& w : out) if (!in.count(w)) delta++;
    return delta;
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
// `coined_words` (nullptr = old behaviour, e.g. pre-existing gates) feeds both B6-1 (degrade
// sparing) and B6-2 (distortion vocabulary) -- same set, single source of truth, so a memory's
// logged content_word_delta always reflects what the listener actually worked from.
inline RetellResult hearsay_hop(LlamaBridge& bridge, VillagerID listener,
                                const MemoryEntry& heard, const Genome& g,
                                uint64_t tick, uint64_t seed,
                                const std::set<std::string>* coined_words = nullptr) {
    std::string   degraded = degrade(heard, tick, coined_words);
    HearsayFields inbound  = extract_hearsay_fields(heard.text);   // the source, pre-degradation
    RetellResult  rr       = bridge.retell(listener, degraded, trait_prose(g), seed); // §B2

    if (field_match_count(inbound, rr.fields) >= 3) {             // too faithful -> force drift
        uint64_t dseed = splitmix64_avalanche(
            (uint64_t)tick ^ ((uint64_t)listener << 20) ^ ((uint64_t)g.pack() << 40));
        std::string dw = distortion_word(dseed, coined_words);    // §B6-2
        rr.out_text += " They say it is " + dw + ".";
        rr.fields.twist = dw;
        rr.distortion_injected = true;
    }
    return rr;
}

} // namespace tbv
