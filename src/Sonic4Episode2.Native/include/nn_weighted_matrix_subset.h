#pragma once

#include "nn_normal_matrix.h"

#include <cstdint>
#include <vector>

struct NnWeightedMatrixSubset {
    std::vector<NnShaderMatrix> model_view;
    std::vector<NnNormalMatrix> normal;
};

NnWeightedMatrixSubset nn_weighted_matrix_subset(
    const std::vector<NnShaderMatrix>& palette,
    const std::vector<std::uint32_t>& subset,
    std::uint32_t vertex_format,
    CameraPrecision precision);
