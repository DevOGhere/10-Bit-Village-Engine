#pragma once
#include <cstdint>

namespace tbv {

// Global Typedefs
using Tick = uint64_t;
using VillagerID = uint32_t;

// Fixed-point macro/constants
constexpr int32_t FIXED_POINT_ONE = 100000;
constexpr int32_t FIXED_POINT_ZERO = 0;

// Enums
enum class MemType : uint8_t {
    EXPERIENCE = 0,
    HEARSAY = 1,
    DREAM = 2
};

} // namespace tbv
