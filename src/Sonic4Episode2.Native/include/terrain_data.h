#pragma once

#include "stage_data.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

enum class TerrainRecordKind {
    Height,
    Angle,
    Attribute,
};

struct TerrainTable {
    TerrainRecordKind kind;
    std::uint16_t record_count;
    std::vector<std::uint8_t> records;
    std::vector<std::uint16_t> chip_records;
};

std::size_t terrain_record_size(TerrainRecordKind kind);

TerrainTable parse_terrain_table(
    const std::uint8_t* data,
    std::size_t size,
    TerrainRecordKind kind);

std::optional<std::uint8_t> terrain_byte(
    const TerrainTable& table,
    std::size_t chip,
    std::size_t byte_index);
