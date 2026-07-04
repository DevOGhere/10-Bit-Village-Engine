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

// Tokens from `text` that are not in /usr/share/dict/words.
inline std::vector<std::string> coined_terms(const std::string& text) {
    std::vector<std::string> out;
    for (auto& tok : tokenize(text)) {
        if (!dictionary().count(tok)) out.push_back(tok);
    }
    return out;
}

} // namespace tbv
