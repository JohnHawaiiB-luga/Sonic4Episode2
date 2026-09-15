#pragma once

#include "nn_trig.h"

#include <array>
#include <cstdint>

using NnQuaternion = std::array<float, 4u>;

NnQuaternion nn_quaternion_multiply(
    const NnQuaternion& left,
    const NnQuaternion& right,
    CameraPrecision precision);

NnQuaternion nn_quaternion_xyz(
    const std::array<std::int32_t, 3u>& rotation,
    CameraPrecision precision);

NnQuaternion nn_quaternion_slerp(
    const NnQuaternion& primary,
    const NnQuaternion& secondary,
    float weight,
    CameraPrecision precision);
