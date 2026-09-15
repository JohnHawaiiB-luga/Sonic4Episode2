#pragma once

#include "nn_motion_frame.h"

#include <cstddef>
#include <cstdint>
#include <optional>

struct NnMotionFloatKey {
    float frame;
    float value;
};

std::int16_t nn_sample_constant_a16(
    const NnMotionA16Key* keys,
    std::size_t count,
    float frame,
    CameraPrecision precision);

float nn_sample_linear_float(
    const NnMotionFloatKey* keys,
    std::size_t count,
    float frame,
    CameraPrecision precision);

float nn_sample_constant_float(
    const NnMotionFloatKey* keys,
    std::size_t count,
    float frame,
    CameraPrecision precision);

std::optional<std::int16_t> nn_evaluate_constant_a16(
    std::uint32_t flags,
    float start,
    float end,
    float frame,
    const NnMotionA16Key* keys,
    std::size_t count,
    CameraPrecision precision);

std::optional<float> nn_evaluate_scalar_float(
    std::uint32_t flags,
    float start,
    float end,
    float frame,
    const NnMotionFloatKey* keys,
    std::size_t count,
    CameraPrecision precision);
