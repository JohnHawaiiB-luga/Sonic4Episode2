#pragma once

#include "nn_shader_constants.h"
#include "stage_lighting.h"

#include <array>
#include <cstdint>

struct NnParallelLightConstants {
    std::array<float, 4u> diffuse{};
    std::array<float, 4u> position{};
    std::array<float, 4u> specular{};
};

struct NnParallelLightingConstants {
    std::array<NnParallelLightConstants, 4u> lights{};
    std::uint32_t count = 0u;
};

NnParallelLightingConstants nn_parallel_lighting_constants(
    const std::array<StageParallelLight, 8u>& lights,
    std::uint32_t enable_mask,
    const NnShaderMatrix& light_matrix,
    CameraPrecision precision);

NnParallelLightingConstants nn_parallel_lighting_constants(
    const std::array<StageParallelLight, 8u>& lights,
    std::uint32_t enable_mask,
    const NnShaderMatrix& light_matrix,
    CameraPrecision precision,
    const std::array<float, 4u>& light_tint);
