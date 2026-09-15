#include "stage_placement.h"

#include <cstddef>
#include <cstdint>
#include <limits>
#include <unordered_set>

namespace {

constexpr std::uint16_t kModelIdMask = 0x0FFFu;
constexpr std::uint16_t kMaximumModelCount = 4096u;

[[noreturn]] void fail(const char* message) {
    throw StageDataError(message);
}

std::size_t checked_cell_count(
    const StageGridData& grid,
    std::uint8_t expected_cell_bytes,
    const char* cell_bytes_message,
    const char* dimensions_message,
    const char* length_message) {
    if (grid.cell_bytes != expected_cell_bytes) {
        fail(cell_bytes_message);
    }
    if (grid.width == 0u || grid.height == 0u) {
        fail(dimensions_message);
    }

    const std::size_t width = static_cast<std::size_t>(grid.width);
    const std::size_t height = static_cast<std::size_t>(grid.height);
    if (height > std::numeric_limits<std::size_t>::max() / width) {
        fail(length_message);
    }
    const std::size_t cell_count = width * height;
    if (grid.cells.size() != cell_count) {
        fail(length_message);
    }
    return cell_count;
}

std::size_t grid_index(const StageGridData& grid, std::uint16_t x, std::uint16_t y) {
    return static_cast<std::size_t>(y) * static_cast<std::size_t>(grid.width) +
           static_cast<std::size_t>(x);
}

std::int32_t signed_nibble(std::uint16_t value) {
    std::int32_t result = static_cast<std::int32_t>(value & 0x000Fu);
    if (result >= 8) {
        result -= 16;
    }
    return result;
}

void require_coordinate_in_grid(
    std::int32_t x,
    std::int32_t y,
    const StageGridData& grid,
    const char* message) {
    if (x < 0 || y < 0 ||
        x >= static_cast<std::int32_t>(grid.width) ||
        y >= static_cast<std::int32_t>(grid.height)) {
        fail(message);
    }
}

}

std::vector<StageMapPlacement> resolve_stage_map(
    const StageGridData& mp,
    const StageGridData& md,
    std::uint16_t model_count,
    const StageMapRange& range) {
    const std::size_t mp_cell_count = checked_cell_count(
        mp,
        2u,
        "map placement MP cells must be words",
        "map placement MP dimensions are zero",
        "map placement MP storage length does not match dimensions");
    const std::size_t md_cell_count = checked_cell_count(
        md,
        1u,
        "map placement MD cells must be bytes",
        "map placement MD dimensions are zero",
        "map placement MD storage length does not match dimensions");
    if (mp.width != md.width || mp.height != md.height || mp_cell_count != md_cell_count) {
        fail("map placement MP and MD dimensions do not match");
    }
    if (model_count == 0u || model_count > kMaximumModelCount) {
        fail("map placement model count is unsupported");
    }

    if (range.min_x > range.max_x || range.min_y > range.max_y) {
        return {};
    }
    require_coordinate_in_grid(
        range.min_x,
        range.min_y,
        mp,
        "map placement range is outside the grid");
    require_coordinate_in_grid(
        range.max_x,
        range.max_y,
        mp,
        "map placement range is outside the grid");

    std::vector<StageMapPlacement> placements;
    std::unordered_set<std::size_t> resolved_anchors;
    for (std::int32_t x = range.min_x; x <= range.max_x; ++x) {
        for (std::int32_t y = range.min_y; y <= range.max_y; ++y) {
            std::uint16_t anchor_x = static_cast<std::uint16_t>(x);
            std::uint16_t anchor_y = static_cast<std::uint16_t>(y);
            std::size_t anchor_index = grid_index(mp, anchor_x, anchor_y);
            std::uint16_t anchor_cell = mp.cells[anchor_index];
            std::uint16_t model_id = static_cast<std::uint16_t>(anchor_cell & kModelIdMask);

            if (model_id == 0u) {
                const std::uint16_t redirect = md.cells[anchor_index];
                if (redirect > std::numeric_limits<std::uint8_t>::max()) {
                    fail("map placement MD value exceeds one byte");
                }

                const std::int32_t dx = signed_nibble(redirect);
                const std::int32_t dy = signed_nibble(static_cast<std::uint16_t>(redirect >> 4u));
                if (dx == 0 && dy == 0) {
                    continue;
                }

                const std::int32_t resolved_x = x + dx;
                const std::int32_t resolved_y = y + dy;
                require_coordinate_in_grid(
                    resolved_x,
                    resolved_y,
                    mp,
                    "map placement MD redirect is outside the grid");

                anchor_x = static_cast<std::uint16_t>(resolved_x);
                anchor_y = static_cast<std::uint16_t>(resolved_y);
                anchor_index = grid_index(mp, anchor_x, anchor_y);
                anchor_cell = mp.cells[anchor_index];
                model_id = static_cast<std::uint16_t>(anchor_cell & kModelIdMask);
            }

            if (model_id == 0u || model_id >= model_count) {
                continue;
            }
            if (!resolved_anchors.insert(anchor_index).second) {
                continue;
            }

            placements.push_back({
                anchor_x,
                anchor_y,
                model_id,
                static_cast<std::uint16_t>(((anchor_cell >> 12u) & 0x0003u) * 0x4000u),
                (anchor_cell & 0x4000u) != 0u,
                (anchor_cell & 0x8000u) != 0u,
            });
        }
    }
    return placements;
}
