#pragma once

#include "stage_transform.h"

#include <array>
#include <cstdint>
#include <optional>
#include <vector>

struct StageSceneModel {
    std::uint32_t archive_flags = 0u;
    std::optional<std::array<std::uint32_t, 3u>> first_node_translation_bits;
};

struct StageSceneInstance {
    StageMapPlacement placement;
    std::array<float, 16u> transform;
};

std::vector<StageSceneInstance> assemble_pc_stage_instances(
    const StageGridData& mp,
    const StageGridData& md,
    const std::vector<StageSceneModel>& models,
    const StageMapRange& range,
    const std::array<float, 3u>& offsets,
    StageTransformPrecision precision);
