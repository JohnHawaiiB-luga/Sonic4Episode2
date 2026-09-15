#pragma once

#include "nn_model_data.h"
#include "nn_shader_profile.h"

#include <cstdint>

struct NnMaterialProfileContext {
    std::uint32_t subobject_flags = 0u;
    std::uint32_t vertex_format = 0u;
    std::uint32_t draw_flags_low = 0u;
    std::uint32_t draw_flags_high = 0u;
    std::uint32_t texture_stage_limit = 0u;
};

NnShaderProfileKeyInput nn_unlit_material_profile(
    const NnMaterialData& material,
    const NnMaterialProfileContext& context);

NnShaderProfileKeyInput nn_lit_material_profile(
    const NnMaterialData& material,
    const NnMaterialProfileContext& context);
