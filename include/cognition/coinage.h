#pragma once
// Coinage harvest (v1 Phase: emergent-harvest, not best-of-N — grammar's gone).
// A system/vendored wordlist is loaded once (function-local static) and used to
// flag free-generation tokens that aren't real words. No mutation of generation
// itself — coined terms simply ride hearsay/dream/action text naturally.
// /usr/share/dict/words exists on macOS dev boxes but NOT on Debian slim (the HF
// container base) — falls back to the vendored assets/dict_en.txt (CWD-relative,
// same convention as models/grammars) so the filter isn't a silent no-op at deploy.
#include <cctype>
#include <fstream>
#include <string>
#include <unordered_set>
#include <vector>

namespace tbv {

inline const std::unordered_set<std::string>& dictionary() {
    static const std::unordered_set<std::string> dict = [] {
        std::unordered_set<std::string> d;
        std::ifstream f("/usr/share/dict/words");
        if (!f.is_open()) f.open("assets/dict_en.txt");
        std::string w;
        while (std::getline(f, w)) {
            for (char& c : w) c = (char)tolower((unsigned char)c);
            d.insert(w);
        }
        return d;
    }();
    return dict;
}

// Alpha-only, lowercase, len>2 tokens — mirrors main.cpp's tok_set().
inline std::vector<std::string> tokenize(const std::string& text) {
    std::vector<std::string> out;
    std::string cur;
    auto flush = [&] {
        if (cur.size() > 2) out.push_back(cur);
        cur.clear();
    };
    for (char c : text) {
        if (std::isalpha((unsigned char)c)) cur += (char)tolower((unsigned char)c);
        else flush();
    }
    flush();
    return out;
}

// Run 2 Plan §B5 — the not-in-dictionary filter alone flags ~90% inflection/fragment noise
// (packet 098, reproduced exactly by analyze_run.py's baseline: 82.7% on Run 1's CoinedWords).
// Two extra gates before a token counts as a real coinage:
//   1. length >= COINAGE_MIN_LEN -- kills short fragments ("gle", "unsh", "doth").
//   2. inflection gate -- strip a common suffix; if the stem (or stem+"e", or an undoubled
//      final-consonant stem, e.g. "smudging"->"smudg"->"smudge"/"smudg") is a real dict word,
//      this was never a coinage, just a form the dictionary doesn't carry as-is.
constexpr size_t COINAGE_MIN_LEN = 5;

// Found empirically (2026-07-09, real 2000-tick local sustained run, Run 2 Plan Phase B
// final gate spot-check): 73/282 coinages from that run were plain -ies/-est word forms
// ("stories", "families", "smallest", "darkest"...) that the original suffix list below
// missed entirely -- 26% false-positive rate discovered before this ever shipped. "est" was
// a one-line fix (fits the exact same strip-and-check pattern as the other suffixes, e.g.
// "darkest"->"dark", "closest"->"clos"->"close"). "-ies" needed its own branch: it's not a
// suffix STRIP, the "y" itself was elided ("stories" -> "story", not "stor" or "storie").
inline bool is_inflection_or_fragment(const std::string& tok) {
    if (tok.size() < COINAGE_MIN_LEN) return true;
    const auto& dict = dictionary();
    if (tok.size() > 4 && tok.compare(tok.size() - 3, 3, "ies") == 0) {
        std::string stem = tok.substr(0, tok.size() - 3) + "y";
        if (dict.count(stem)) return true;
    }
    static const std::string suffixes[] = {"s", "es", "ed", "ing", "ly", "er", "ers", "est"};
    for (const auto& suf : suffixes) {
        if (tok.size() <= suf.size() || tok.compare(tok.size() - suf.size(), suf.size(), suf) != 0)
            continue;
        std::string stem = tok.substr(0, tok.size() - suf.size());
        if (dict.count(stem)) return true;
        if (dict.count(stem + "e")) return true;
        if (stem.size() >= 2 && stem[stem.size() - 1] == stem[stem.size() - 2]
            && dict.count(stem.substr(0, stem.size() - 1))) return true;
    }
    return false;
}

// Tokens from `text` that are not in /usr/share/dict/words AND survive the B5 filter above --
// this IS the set that ends up in WorldState.coined_words, so B6-2's distortion-vocabulary
// feedback loop (degradation.h) draws from clean terms, not dictionary-inflection noise.
inline std::vector<std::string> coined_terms(const std::string& text) {
    std::vector<std::string> out;
    for (auto& tok : tokenize(text)) {
        if (!dictionary().count(tok) && !is_inflection_or_fragment(tok)) out.push_back(tok);
    }
    return out;
}

} // namespace tbv
