#pragma once

#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <vector>

struct StageArchiveEntry {
    std::string name;
    std::size_t offset;
    std::size_t length;
    std::uint32_t flags = 0u;
};

struct StageGridData {
    std::uint16_t width;
    std::uint16_t height;
    std::uint8_t cell_bytes;
    std::vector<std::uint16_t> cells;
};

struct StageRingPlacement {
    std::int32_t x;
    std::int32_t y;
};

struct StageRingPlacements {
    std::uint16_t block_width;
    std::uint16_t block_height;
    std::vector<StageRingPlacement> rings;
};

struct StageEventPlacement {
    std::int32_t x;
    std::int32_t y;
    std::uint16_t object_id;
    std::uint16_t flags;
    std::int8_t left;
    std::int8_t top;
    std::uint8_t width;
    std::uint8_t height;
    std::uint16_t parameter;
};

struct StageEventPlacements {
    std::uint16_t block_width;
    std::uint16_t block_height;
    std::vector<StageEventPlacement> events;
};

class StageDataError : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

std::vector<StageArchiveEntry> parse_pc_stage_archive(
    const std::uint8_t* data,
    std::size_t size);

StageGridData parse_stage_grid(
    const std::uint8_t* data,
    std::size_t size,
    std::uint8_t cell_bytes);

StageRingPlacements parse_stage_ring_placements(
    const std::uint8_t* data,
    std::size_t size);

StageEventPlacements parse_stage_event_placements(
    const std::uint8_t* data,
    std::size_t size);
