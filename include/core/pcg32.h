#pragma once
#include <cstdint>

namespace tbv {

// Minimal PCG32 implementation for deterministic random streams.
struct pcg32_state {
    uint64_t state = 0;
    uint64_t inc = 0;
};

// Generates a random 32-bit integer
inline uint32_t pcg32_random_r(pcg32_state* rng) {
    uint64_t oldstate = rng->state;
    // Advance internal state
    rng->state = oldstate * 6364136223846793005ULL + (rng->inc | 1);
    // Calculate output function (XSH RR), uses old state for max ILP
    uint32_t xorshifted = static_cast<uint32_t>(((oldstate >> 18u) ^ oldstate) >> 27u);
    uint32_t rot = static_cast<uint32_t>(oldstate >> 59u);
    return (xorshifted >> rot) | (xorshifted << ((-rot) & 31));
}

// Seeds the PRNG state
inline void pcg32_srandom_r(pcg32_state* rng, uint64_t initstate, uint64_t initseq) {
    rng->state = 0U;
    rng->inc = (initseq << 1u) | 1u;
    pcg32_random_r(rng);
    rng->state += initstate;
    pcg32_random_r(rng);
}

// Draw a random int32_t in range [0, bound) using unbiased rejection sampling
inline int32_t pcg32_random_bounded_r(pcg32_state* rng, int32_t bound) {
    uint32_t ubound = static_cast<uint32_t>(bound);
    uint32_t threshold = -ubound % ubound;
    for (;;) {
        uint32_t r = pcg32_random_r(rng);
        if (r >= threshold)
            return static_cast<int32_t>(r % ubound);
    }
}

} // namespace tbv
