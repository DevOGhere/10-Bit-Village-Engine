#pragma once
#include <cstdint>

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

} // namespace tbv
