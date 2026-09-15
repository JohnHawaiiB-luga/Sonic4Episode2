#pragma once

#include <array>
#include <cstdint>
#include <optional>

struct CameraViewInput {
    std::int32_t fov_angle;
    float aspect;
    float near_plane;
    float far_plane;
    std::array<float, 3u> eye;
    std::array<float, 3u> inline_target;
    std::optional<std::array<float, 3u>> linked_target;
    std::int32_t roll_angle;
};

struct CameraViewParameters {
    std::uint32_t leading_word;
    std::int32_t fov_angle;
    float aspect;
    float near_plane;
    float far_plane;
    std::array<float, 3u> eye;
    std::array<float, 3u> target;
    std::int32_t roll_angle;
};

CameraViewParameters make_camera_view_parameters(const CameraViewInput& input) noexcept;
