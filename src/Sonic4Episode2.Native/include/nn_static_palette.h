#pragma once

#include "camera_matrix.h"
#include "nn_model_data.h"

#include <vector>

struct NnStaticNodeMatrices {
    std::vector<CameraMatrix> node_world;
    std::vector<CameraMatrix> palette;
};

NnStaticNodeMatrices nn_static_node_matrices(
    const NnModelData& model,
    const CameraMatrix& base,
    CameraPrecision precision,
    const CameraMatrixBackend& backend);

std::vector<CameraMatrix> nn_static_matrix_palette(
    const NnModelData& model,
    const CameraMatrix& base,
    CameraPrecision precision,
    const CameraMatrixBackend& backend);
