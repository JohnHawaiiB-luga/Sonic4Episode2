#pragma once

#include "camera_math.h"

#include <array>

struct NormalCameraFollowState {
    std::array<float, 3u> internal_position{};
    std::array<float, 3u> speed{};
    std::array<float, 3u> allowance{};
};

std::array<float, 3u> update_normal_camera_follow(
    NormalCameraFollowState& state,
    const std::array<float, 3u>& subject_position,
    bool move_flag_10,
    float time_scale,
    CameraPrecision precision);
