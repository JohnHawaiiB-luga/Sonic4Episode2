#pragma once

#include "stage_data.h"

#include <cstdint>
#include <vector>

struct StageMapRange {
    std::int32_t min_x;
    std::int32_t max_x;
    std::int32_t min_y;
    std::int32_t max_y;
};

struct StageMapPlacement {
    std::uint16_t anchor_x;
    std::uint16_t anchor_y;
    std::uint16_t model_id;
    std::uint16_t rotation;
    bool flip_x;
    bool flip_y;
};

std::vector<StageMapPlacement> resolve_stage_map(
    const StageGridData& mp,
    const StageGridData& md,
    std::uint16_t model_count,
    const StageMapRange& range);
