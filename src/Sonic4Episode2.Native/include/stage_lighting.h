#pragma once

#include "camera_matrix.h"

#include <array>
#include <cstddef>
#include <cstdint>

struct StageParallelLight {
    std::array<std::uint32_t, 4u> color_bits{};
    std::uint32_t intensity_bits = 0u;
    std::array<std::uint32_t, 3u> direction_bits{};
};

struct StageLighting {
    std::uint32_t header_word = 0u;
    std::array<std::uint32_t, 3u> ambient_bits{};
    std::array<StageParallelLight, 8u> lights{};
};

StageLighting parse_pc_stage_lighting(const std::uint8_t* data, std::size_t size);

StageLighting normalize_stage_lighting(const StageLighting& lighting, CameraPrecision precision);
