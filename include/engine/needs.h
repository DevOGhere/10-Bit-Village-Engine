#pragma once
#include <cstdint>

namespace tbv {

// Needs (0 to 100,000) representing fixed-point values
struct Needs {
    int32_t hunger = 100000;
    int32_t social = 100000;
    int32_t safety = 100000;
};

} // namespace tbv
