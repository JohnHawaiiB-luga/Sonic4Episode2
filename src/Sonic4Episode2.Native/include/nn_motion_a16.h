#pragma once

#include "camera_math.h"

#include <cstddef>
#include <cstdint>

struct NnMotionA16Key {
    std::int16_t frame;
    std::int16_t value;
};

std::int16_t nn_sample_linear_a16(
    const NnMotionA16Key* keys,
    std::size_t count,
    float frame,
    CameraPrecision precision);
