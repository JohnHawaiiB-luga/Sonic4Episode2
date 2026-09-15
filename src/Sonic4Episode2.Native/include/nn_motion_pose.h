#pragma once

#include "nn_model_data.h"
#include "nn_motion_data.h"
#include "nn_quaternion.h"

#include <array>
#include <cstddef>
#include <cstdint>

struct NnMotionLocalPose {
    std::array<float, 3u> translation;
    NnQuaternion rotation;
    std::array<float, 3u> scale;
    std::uint32_t translation_flags;
    std::uint32_t rotation_flags;
    std::uint32_t scale_flags;
    std::size_t next_channel;
};

NnMotionLocalPose nn_motion_local_pose(
    const NnNodeData& node,
    std::int32_t node_id,
    const NnMotionData& motion,
    std::size_t first_channel,
    float frame,
    CameraPrecision precision);

NnMotionLocalPose nn_link_motion_pose(
    const NnMotionLocalPose& primary,
    const NnMotionLocalPose& secondary,
    float weight,
    CameraPrecision precision);
