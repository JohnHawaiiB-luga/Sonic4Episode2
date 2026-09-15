#pragma once

#include "camera_math.h"
#include "camera_view.h"

#include <array>
#include <cstdint>

struct NormalCameraMapBounds {
    std::int32_t left;
    std::int32_t top;
    std::int32_t right;
    std::int32_t bottom;
};

CameraViewInput make_normal_camera_view(
    const std::array<float, 3u>& internal_position,
    std::int32_t viewport_width,
    std::int32_t viewport_height,
    const NormalCameraMapBounds& map_bounds,
    CameraPrecision precision);
