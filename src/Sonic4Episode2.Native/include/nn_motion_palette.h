#pragma once

#include "camera_matrix.h"
#include "nn_model_data.h"
#include "nn_motion_pose.h"

#include <vector>

struct NnMotionNodeMatrices {
    std::vector<CameraMatrix> node_world;
    std::vector<CameraMatrix> palette;
};

NnMotionNodeMatrices nn_motion_node_matrices(
    const NnModelData& model,
    const std::vector<NnMotionLocalPose>& poses,
    const CameraMatrix& base,
    CameraPrecision precision,
    const CameraMatrixBackend& backend);
