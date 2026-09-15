#pragma once

#include "camera_matrix.h"
#include "nn_model_data.h"

#include <array>
#include <cstdint>

struct NnPerspectiveClipContext {
    float near_plane;
    float far_plane;
    std::array<float, 2u> top;
    std::array<float, 2u> bottom;
    std::array<float, 2u> right;
    std::array<float, 2u> left;
};

std::uint32_t nn_clip_box(
    const std::array<float, 3u>& center,
    const std::array<float, 3u>& half_extents,
    const CameraMatrix& node_world,
    const NnPerspectiveClipContext& context,
    CameraPrecision precision);

std::uint32_t nn_clip_sphere(
    const std::array<float, 3u>& center,
    float radius,
    const CameraMatrix& node_world,
    const NnPerspectiveClipContext& context,
    CameraPrecision precision);

std::uint32_t nn_clip_box_node_status(
    const NnNodeData& node,
    const CameraMatrix& node_world,
    const NnPerspectiveClipContext& context,
    std::uint32_t initial_status,
    std::uint32_t flags,
    CameraPrecision precision);
