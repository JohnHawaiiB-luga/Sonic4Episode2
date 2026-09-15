#pragma once

#include "nn_motion_a16.h"

#include <cstddef>
#include <cstdint>
#include <optional>

struct NnMotionFrameResult {
    float frame;
    bool active;
};

NnMotionFrameResult nn_map_motion_frame(
    std::uint32_t flags,
    float start,
    float end,
    float frame,
    CameraPrecision precision);

std::optional<std::int16_t> nn_evaluate_linear_a16(
    std::uint32_t flags,
    float start,
    float end,
    float frame,
    const NnMotionA16Key* keys,
    std::size_t count,
    CameraPrecision precision);
