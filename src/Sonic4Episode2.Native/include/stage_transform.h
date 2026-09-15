#pragma once

#include "stage_placement.h"

#include <array>

enum class StageTransformPrecision {
    Single,
    Double,
};

std::array<float, 16u> make_pc_stage_transform(
    const StageMapPlacement& placement,
    const std::array<float, 3u>& pivot,
    const std::array<float, 3u>& offsets,
    bool lower_depth,
    StageTransformPrecision precision);
