#pragma once

#include "terrain_data.h"

#include <cstdint>
#include <optional>

enum class TerrainCollisionPrecision {
    Single,
    Double,
};

struct TerrainCollisionBounds {
    std::int32_t left;
    std::int32_t top;
    std::int32_t right_exclusive;
    std::int32_t bottom_exclusive;
};

struct TerrainCollisionQuery {
    float x;
    float y;
    std::uint16_t flags;
    std::uint8_t vector;
    std::optional<std::uint16_t> direction;
    std::optional<std::uint32_t> attribute;
};

struct TerrainCollisionResult {
    float distance;
    std::optional<std::uint16_t> direction;
    std::optional<std::uint32_t> attribute;
};

class TerrainCollision {
public:
    TerrainCollision(
        StageGridData first_grid,
        StageGridData second_grid,
        TerrainTable height_table,
        TerrainTable angle_table,
        TerrainTable attribute_table,
        TerrainCollisionBounds bounds);

    TerrainCollisionResult fast_query(
        const TerrainCollisionQuery& query,
        TerrainCollisionPrecision precision) const;

    TerrainCollisionResult normal_query(
        const TerrainCollisionQuery& query,
        TerrainCollisionPrecision precision) const;

private:
    StageGridData first_grid_;
    StageGridData second_grid_;
    TerrainTable height_table_;
    TerrainTable angle_table_;
    TerrainTable attribute_table_;
    TerrainCollisionBounds bounds_;
};
