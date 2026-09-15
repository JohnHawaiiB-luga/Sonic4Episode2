#pragma once

#include "camera_math.h"
#include "nn_material_profile.h"

#include <array>

struct NnLitMaterialConstants {
    std::array<float, 4u> ambient;
    std::array<float, 4u> diffuse;
    std::array<float, 4u> specular;
    std::array<float, 4u> emission;
    std::array<float, 4u> scene_color;
    float shininess;
    float base_alpha;
};

NnLitMaterialConstants nn_lit_material_constants(
    const NnMaterialData& material,
    const NnMaterialProfileContext& context,
    const std::array<float, 4u>& global_ambient,
    CameraPrecision precision);
