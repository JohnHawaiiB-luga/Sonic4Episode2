#include "stage_data.h"

#include <limits>
#include <utility>

namespace {

constexpr std::size_t kArchiveHeaderSize = 32u;
constexpr std::size_t kArchiveEntrySize = 16u;
constexpr std::size_t kArchiveNameSize = 32u;
constexpr std::size_t kGridHeaderSize = 4u;
constexpr std::size_t kPlacementBlockOffsetSize = 4u;
constexpr std::size_t kRingRecordSize = 2u;
constexpr std::size_t kEventRecordSize = 12u;
constexpr std::int32_t kStageBlockPitch = 256;

struct StagePlacementMessages {
    const char* data_is_null;
    const char* header_is_truncated;
    const char* block_count_is_too_large;
    const char* offset_table_is_too_large;
    const char* offset_table_is_out_of_range;
    const char* block_offset_is_negative;
    const char* block_offset_is_out_of_range;
    const char* record_data_is_too_large;
    const char* records_are_truncated;
    const char* record_count_is_too_large;
};

struct StagePlacementGrid {
    std::uint16_t width;
    std::uint16_t height;
    std::size_t block_count;
    std::size_t record_count;
};

constexpr StagePlacementMessages kRingPlacementMessages = {
    "stage ring data is null",
    "stage ring header is truncated",
    "stage ring block count is too large",
    "stage ring offset table is too large",
    "stage ring offset table is out of range",
    "stage ring block offset is negative",
    "stage ring block offset is out of range",
    "stage ring record data is too large",
    "stage ring records are truncated",
    "stage ring record count is too large",
};

constexpr StagePlacementMessages kEventPlacementMessages = {
    "stage event data is null",
    "stage event header is truncated",
    "stage event block count is too large",
    "stage event offset table is too large",
    "stage event offset table is out of range",
    "stage event block offset is negative",
    "stage event block offset is out of range",
    "stage event record data is too large",
    "stage event records are truncated",
    "stage event record count is too large",
};

[[noreturn]] void fail(const char* message) {
    throw StageDataError(message);
}

std::uint16_t read_le16(const std::uint8_t* data) {
    return static_cast<std::uint16_t>(
        static_cast<std::uint16_t>(data[0]) |
        (static_cast<std::uint16_t>(data[1]) << 8u));
}

std::uint32_t read_le32(const std::uint8_t* data) {
    return static_cast<std::uint32_t>(
        static_cast<std::uint32_t>(data[0]) |
        (static_cast<std::uint32_t>(data[1]) << 8u) |
        (static_cast<std::uint32_t>(data[2]) << 16u) |
        (static_cast<std::uint32_t>(data[3]) << 24u));
}

std::size_t checked_size(std::uint32_t value, const char* message) {
    const std::size_t result = static_cast<std::size_t>(value);
    if (static_cast<std::uint32_t>(result) != value) {
        fail(message);
    }
    return result;
}

std::size_t read_nonnegative_i32(const std::uint8_t* data, const char* message) {
    const std::uint32_t raw = read_le32(data);
    if ((raw & 0x80000000u) != 0u) {
        fail(message);
    }
    return checked_size(raw, message);
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

bool fits_range(std::size_t offset, std::size_t length, std::size_t size) {
    return offset <= size && length <= size - offset;
}

void require_range(
    std::size_t offset,
    std::size_t length,
    std::size_t size,
    const char* message) {
    if (!fits_range(offset, length, size)) {
        fail(message);
    }
}

std::int8_t read_i8(const std::uint8_t* data) {
    if (data[0] < 0x80u) {
        return static_cast<std::int8_t>(data[0]);
    }
    return static_cast<std::int8_t>(
        static_cast<std::int16_t>(data[0]) - static_cast<std::int16_t>(0x100u));
}

StagePlacementGrid parse_stage_placement_grid(
    const std::uint8_t* data,
    std::size_t size,
    std::size_t record_size,
    const StagePlacementMessages& messages) {
    if (data == nullptr) {
        fail(messages.data_is_null);
    }
    if (size < kGridHeaderSize) {
        fail(messages.header_is_truncated);
    }

    const std::uint16_t width = read_le16(data);
    const std::uint16_t height = read_le16(data + 2u);
    const std::size_t block_count = checked_product(
        static_cast<std::size_t>(width),
        static_cast<std::size_t>(height),
        messages.block_count_is_too_large);
    const std::size_t offset_table_size = checked_product(
        block_count,
        kPlacementBlockOffsetSize,
        messages.offset_table_is_too_large);
    require_range(
        kGridHeaderSize,
        offset_table_size,
        size,
        messages.offset_table_is_out_of_range);

    std::size_t total_record_count = 0u;
    const std::size_t maximum_record_count = size / record_size;
    for (std::size_t index = 0u; index < block_count; ++index) {
        const std::uint8_t* offset_record =
            data + kGridHeaderSize + index * kPlacementBlockOffsetSize;
        const std::size_t block_offset =
            read_nonnegative_i32(offset_record, messages.block_offset_is_negative);
        require_range(
            block_offset,
            sizeof(std::uint16_t),
            size,
            messages.block_offset_is_out_of_range);

        const std::size_t record_count = read_le16(data + block_offset);
        const std::size_t record_data_size = checked_product(
            record_count,
            record_size,
            messages.record_data_is_too_large);
        const std::size_t record_data_offset = checked_sum(
            block_offset,
            sizeof(std::uint16_t),
            messages.records_are_truncated);
        require_range(
            record_data_offset,
            record_data_size,
            size,
            messages.records_are_truncated);
        total_record_count = checked_sum(
            total_record_count,
            record_count,
            messages.record_count_is_too_large);
        if (total_record_count > maximum_record_count) {
            fail(messages.record_count_is_too_large);
        }
    }

    return {width, height, block_count, total_record_count};
}

std::string archive_entry_name(
    const std::uint8_t* data,
    std::size_t name_table_offset,
    std::size_t index) {
    if (name_table_offset == 0u) {
        return std::to_string(index);
    }

    const std::uint8_t* slot = data + name_table_offset + index * kArchiveNameSize;
    std::size_t name_length = 0u;
    while (name_length < kArchiveNameSize && slot[name_length] != 0u) {
        ++name_length;
    }

    if (name_length == 0u) {
        return std::to_string(index);
    }

    return std::string(reinterpret_cast<const char*>(slot), name_length);
}

}

std::vector<StageArchiveEntry> parse_pc_stage_archive(
    const std::uint8_t* data,
    std::size_t size) {
    if (data == nullptr) {
        fail("stage archive data is null");
    }
    if (size < kArchiveHeaderSize) {
        fail("stage archive header is truncated");
    }
    if (data[0] != static_cast<std::uint8_t>('#') ||
        data[1] != static_cast<std::uint8_t>('A') ||
        data[2] != static_cast<std::uint8_t>('M') ||
        data[3] != static_cast<std::uint8_t>('B')) {
        fail("stage archive magic is unsupported");
    }
    if (read_le32(data + 4u) != kArchiveHeaderSize) {
        fail("stage archive header variant is unsupported");
    }

    const std::size_t entry_count = read_nonnegative_i32(data + 16u, "stage archive entry count is negative");
    const std::size_t entry_table_offset =
        read_nonnegative_i32(data + 20u, "stage archive entry table offset is negative");
    const std::size_t name_table_offset =
        read_nonnegative_i32(data + 28u, "stage archive name table offset is negative");
    const std::size_t entry_table_size =
        checked_product(entry_count, kArchiveEntrySize, "stage archive entry table is too large");
    const std::size_t name_table_size =
        checked_product(entry_count, kArchiveNameSize, "stage archive name table is too large");

    require_range(
        entry_table_offset,
        entry_table_size,
        size,
        "stage archive entry table is out of range");
    if (entry_table_size != 0u && entry_table_offset < kArchiveHeaderSize) {
        fail("stage archive entry table overlaps its header");
    }

    if (name_table_offset != 0u) {
        require_range(
            name_table_offset,
            name_table_size,
            size,
            "stage archive name table is out of range");
        if (name_table_size != 0u && name_table_offset < kArchiveHeaderSize) {
            fail("stage archive name table overlaps its header");
        }
    }

    for (std::size_t index = 0u; index < entry_count; ++index) {
        const std::uint8_t* record = data + entry_table_offset + index * kArchiveEntrySize;
        const std::size_t payload_offset =
            read_nonnegative_i32(record, "stage archive payload offset is negative");
        const std::size_t payload_length =
            read_nonnegative_i32(record + 4u, "stage archive payload length is negative");

        require_range(
            payload_offset,
            payload_length,
            size,
            "stage archive payload is out of range");
        if (payload_length != 0u && payload_offset < kArchiveHeaderSize) {
            fail("stage archive payload overlaps its header");
        }
    }

    std::vector<StageArchiveEntry> entries;
    entries.reserve(entry_count);
    for (std::size_t index = 0u; index < entry_count; ++index) {
        const std::uint8_t* record = data + entry_table_offset + index * kArchiveEntrySize;
        const std::size_t payload_offset =
            read_nonnegative_i32(record, "stage archive payload offset is negative");
        const std::size_t payload_length =
            read_nonnegative_i32(record + 4u, "stage archive payload length is negative");
        entries.push_back({
            archive_entry_name(data, name_table_offset, index),
            payload_offset,
            payload_length,
            read_le32(record + 12u),
        });
    }

    return entries;
}

StageGridData parse_stage_grid(
    const std::uint8_t* data,
    std::size_t size,
    std::uint8_t cell_bytes) {
    if (data == nullptr) {
        fail("stage grid data is null");
    }
    if (cell_bytes != 1u && cell_bytes != 2u) {
        fail("stage grid cell width is unsupported");
    }
    if (size < kGridHeaderSize) {
        fail("stage grid header is truncated");
    }

    const std::uint16_t width = read_le16(data);
    const std::uint16_t height = read_le16(data + 2u);
    if (width == 0u || height == 0u) {
        fail("stage grid dimensions are zero");
    }

    const std::size_t cell_count = checked_product(
        static_cast<std::size_t>(width),
        static_cast<std::size_t>(height),
        "stage grid cell count is too large");
    const std::size_t cell_data_size = checked_product(
        cell_count,
        static_cast<std::size_t>(cell_bytes),
        "stage grid byte count is too large");
    if (cell_data_size > std::numeric_limits<std::size_t>::max() - kGridHeaderSize) {
        fail("stage grid byte count is too large");
    }
    const std::size_t expected_size = kGridHeaderSize + cell_data_size;
    if (size != expected_size) {
        fail("stage grid length does not match dimensions");
    }

    StageGridData grid{width, height, cell_bytes, {}};
    grid.cells.reserve(cell_count);
    const std::uint8_t* cell_data = data + kGridHeaderSize;
    for (std::size_t index = 0u; index < cell_count; ++index) {
        if (cell_bytes == 1u) {
            grid.cells.push_back(static_cast<std::uint16_t>(cell_data[index]));
        } else {
            const std::size_t cell_offset = index * 2u;
            grid.cells.push_back(read_le16(cell_data + cell_offset));
        }
    }

    return grid;
}

StageRingPlacements parse_stage_ring_placements(
    const std::uint8_t* data,
    std::size_t size) {
    const StagePlacementGrid grid = parse_stage_placement_grid(
        data,
        size,
        kRingRecordSize,
        kRingPlacementMessages);

    StageRingPlacements placements{grid.width, grid.height, {}};
    if (grid.record_count > placements.rings.max_size()) {
        fail(kRingPlacementMessages.record_count_is_too_large);
    }
    placements.rings.reserve(grid.record_count);
    for (std::size_t index = 0u; index < grid.block_count; ++index) {
        const std::uint8_t* offset_record =
            data + kGridHeaderSize + index * kPlacementBlockOffsetSize;
        const std::size_t block_offset =
            read_nonnegative_i32(offset_record, kRingPlacementMessages.block_offset_is_negative);
        const std::size_t record_count = read_le16(data + block_offset);
        const std::int32_t block_x = static_cast<std::int32_t>(index % grid.width) * kStageBlockPitch;
        const std::int32_t block_y = static_cast<std::int32_t>(index / grid.width) * kStageBlockPitch;
        const std::uint8_t* record_data = data + block_offset + sizeof(std::uint16_t);
        for (std::size_t record_index = 0u; record_index < record_count; ++record_index) {
            const std::uint8_t* record = record_data + record_index * kRingRecordSize;
            placements.rings.push_back({block_x + record[0], block_y + record[1]});
        }
    }

    return placements;
}

StageEventPlacements parse_stage_event_placements(
    const std::uint8_t* data,
    std::size_t size) {
    const StagePlacementGrid grid = parse_stage_placement_grid(
        data,
        size,
        kEventRecordSize,
        kEventPlacementMessages);

    StageEventPlacements placements{grid.width, grid.height, {}};
    if (grid.record_count > placements.events.max_size()) {
        fail(kEventPlacementMessages.record_count_is_too_large);
    }
    placements.events.reserve(grid.record_count);
    for (std::size_t index = 0u; index < grid.block_count; ++index) {
        const std::uint8_t* offset_record =
            data + kGridHeaderSize + index * kPlacementBlockOffsetSize;
        const std::size_t block_offset =
            read_nonnegative_i32(offset_record, kEventPlacementMessages.block_offset_is_negative);
        const std::size_t record_count = read_le16(data + block_offset);
        const std::int32_t block_x = static_cast<std::int32_t>(index % grid.width) * kStageBlockPitch;
        const std::int32_t block_y = static_cast<std::int32_t>(index / grid.width) * kStageBlockPitch;
        const std::uint8_t* record_data = data + block_offset + sizeof(std::uint16_t);
        for (std::size_t record_index = 0u; record_index < record_count; ++record_index) {
            const std::uint8_t* record = record_data + record_index * kEventRecordSize;
            placements.events.push_back({
                block_x + static_cast<std::int32_t>(record[0]),
                block_y + static_cast<std::int32_t>(record[1]),
                read_le16(record + 2u),
                read_le16(record + 4u),
                read_i8(record + 6u),
                read_i8(record + 7u),
                record[8u],
                record[9u],
                read_le16(record + 10u),
            });
        }
    }

    return placements;
}
