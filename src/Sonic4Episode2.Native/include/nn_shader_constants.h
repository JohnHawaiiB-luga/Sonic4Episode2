#pragma once

#include "camera_matrix.h"
#include "nn_material_profile.h"

#include <array>

using NnShaderMatrix = std::array<float, 16u>;

struct NnModelMatrixConstants {
    NnShaderMatrix model_view;
    NnShaderMatrix model_view_projection;
};

struct NnUnlitMaterialConstants {
    std::array<float, 4u> diffuse;
    float base_alpha;
};

NnUnlitMaterialConstants nn_unlit_material_constants(
    const NnMaterialData& material,
    const NnMaterialProfileContext& context);

NnShaderMatrix nn_unlit_texture_matrix(
    const NnMaterialStageData& stage,
    const NnMaterialProfileContext& context);

NnShaderMatrix nn_shader_matrix_columns(const NnShaderMatrix& matrix);

NnModelMatrixConstants nn_unlit_model_matrices(
    const NnShaderMatrix& model,
    const NnShaderMatrix& view,
    const NnShaderMatrix& projection,
    CameraPrecision precision,
    const CameraMatrixBackend& backend);
