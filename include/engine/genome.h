#pragma once
#include <cstdint>
#include <sstream>
#include <string>

namespace tbv {

struct Genome {
    uint8_t aggression;  // [0-3]
    uint8_t generosity;  // [0-3]
    uint8_t curiosity;   // [0-3]
    uint8_t sociability; // [0-3]
    uint8_t suspicion;   // [0-3]

    // Explicit serialization to avoid undefined struct padding leaks
    // Packs 5 traits * 2 bits = 10 bits total into a uint16_t
    uint16_t pack() const {
        return (aggression & 3) |
               ((generosity & 3) << 2) |
               ((curiosity & 3) << 4) |
               ((sociability & 3) << 6) |
               ((suspicion & 3) << 8);
    }
};

// Run 2 Plan §B2 — single source of truth for the genome-flavour prose, extracted
// verbatim from build_perception (world.cpp) so retell()/dream() can carry the same
// persona build_perception's ACTION path always had. Output must stay byte-identical
// to the old inline block for identical genomes -- build_perception's callers depend
// on that (Phase 0/1/2/3 hashes never logged this text, but its CONTENT must not
// silently change what ACTION prompts looked like).
inline std::string trait_prose(const Genome& g) {
    std::ostringstream s;
    s << "You are a thronglet living in the village. ";
    if (g.suspicion >= 2)   s << "You are wary, quick to suspect hidden motives. ";
    if (g.curiosity >= 2)   s << "You are endlessly curious about the world. ";
    s << (g.sociability >= 2 ? "You crave the company of others. " : "You prefer to keep to yourself. ");
    if (g.generosity >= 2)  s << "You are openhanded and giving. ";
    if (g.aggression >= 2)  s << "You have a fierce, short temper. ";
    return s.str();
}

} // namespace tbv
