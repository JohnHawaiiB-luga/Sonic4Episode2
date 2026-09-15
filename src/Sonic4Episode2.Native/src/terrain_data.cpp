#include "terrain_data.h"

#include <limits>

namespace {

constexpr std::size_t kTerrainHeaderSize = 4u;
constexpr std::size_t kHeightRecordSize = 4096u;
constexpr std::size_t kCompactRecordSize = 64u;
constexpr std::size_t kChipIndexSize = 2u;

[[noreturn]] void fail(const char* message) {
    throw StageDataError(message);
}

std::uint16_t read_le16(const std::uint8_t* data) {
    return static_cast<std::uint16_t>(
        static_cast<std::uint16_t>(data[0]) |
        (static_cast<std::uint16_t>(data[1]) << 8u));
}

std::size_t checked_product(std::size_t left, std::size_t right, const char* message) {
    if (left != 0u && right > std::numeric_limits<std::size_t>::max() / left) {
        fail(message);
    }
    return left * right;
}

std::size_t checked_sum(std::size_t left, std::size_t right, const char* message) {
    if (right > std::numeric_limits<std::size_t>::max() - left) {
        fail(message);
    }
    return left + right;
}

}

std::size_t terrain_record_size(TerrainRecordKind kind) {
    switch (kind) {
    case TerrainRecordKind::Height:
        return kHeightRecordSize;
    case TerrainRecordKind::Angle:
    case TerrainRecordKind::Attribute:
        return kCompactRecordSize;
    default:
        fail("terrain record kind is unsupported");
    }
}

TerrainTable parse_terrain_table(
    const std::uint8_t* data,
    std::size_t size,
    TerrainRecordKind kind) {
    const std::size_t record_size = terrain_record_size(kind);
    if (data == nullptr) {
        fail("terrain data is null");
    }
    if (size < kTerrainHeaderSize) {
        fail("terrain header is truncated");
    }

    const std::size_t chip_count = static_cast<std::size_t>(read_le16(data));
    const std::uint16_t record_count = read_le16(data + 2u);
    const std::size_t record_bytes = checked_product(
        static_cast<std::size_t>(record_count),
        record_size,
        "terrain record storage is too large");
    const std::size_t chip_bytes = checked_product(
        chip_count,
        kChipIndexSize,
        "terrain chip table is too large");
    const std::size_t index_table_offset = checked_sum(
        kTerrainHeaderSize,
        record_bytes,
        "terrain layout is too large");
    const std::size_t expected_size = checked_sum(
        index_table_offset,
        chip_bytes,
        "terrain layout is too large");
    if (size != expected_size) {
        fail("terrain length does not match its header");
    }

    for (std::size_t index = 0u; index < chip_count; ++index) {
        const std::size_t chip_offset = index_table_offset + index * kChipIndexSize;
        const std::uint16_t record_index = read_le16(data + chip_offset);
        if (record_index >= record_count) {
            fail("terrain chip record index is out of range");
        }
    }

    TerrainTable table{kind, record_count, {}, {}};
    table.records.assign(data + kTerrainHeaderSize, data + index_table_offset);
    table.chip_records.reserve(chip_count);
    for (std::size_t index = 0u; index < chip_count; ++index) {
        const std::size_t chip_offset = index_table_offset + index * kChipIndexSize;
        table.chip_records.push_back(read_le16(data + chip_offset));
    }
    return table;
}

std::optional<std::uint8_t> terrain_byte(
    const TerrainTable& table,
    std::size_t chip,
    std::size_t byte_index) {
    const std::size_t record_size = terrain_record_size(table.kind);
    const std::size_t expected_record_bytes = checked_product(
        static_cast<std::size_t>(table.record_count),
        record_size,
        "terrain record storage is too large");
    if (table.records.size() != expected_record_bytes ||
        chip >= table.chip_records.size() ||
        byte_index >= record_size) {
        return std::nullopt;
    }

    const std::size_t record_index = static_cast<std::size_t>(table.chip_records[chip]);
    if (record_index >= table.record_count) {
        return std::nullopt;
    }

    const std::size_t record_offset = record_index * record_size;
    if (record_offset > table.records.size() ||
        byte_index >= table.records.size() - record_offset) {
        return std::nullopt;
    }
    return table.records[record_offset + byte_index];
}
