#include "stage_placement.h"

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <utility>
#include <vector>

namespace {

bool check(bool condition, const char* message) {
    if (condition) {
        return true;
    }
    std::fprintf(stderr, "%s\n", message);
    return false;
}

template <typename Callable>
bool expects_stage_data_error(Callable&& callable, const char* message) {
    try {
        std::forward<Callable>(callable)();
    } catch (const StageDataError&) {
        return true;
    } catch (...) {
        std::fprintf(stderr, "%s (wrong exception type)\n", message);
        return false;
    }

    std::fprintf(stderr, "%s\n", message);
    return false;
}

StageGridData make_grid(
    std::uint16_t width,
    std::uint16_t height,
    std::uint8_t cell_bytes) {
    return StageGridData{
        width,
        height,
        cell_bytes,
        std::vector<std::uint16_t>(
            static_cast<std::size_t>(width) * static_cast<std::size_t>(height),
            0u),
    };
}

std::size_t cell_index(const StageGridData& grid, std::uint16_t x, std::uint16_t y) {
    return static_cast<std::size_t>(y) * static_cast<std::size_t>(grid.width) +
           static_cast<std::size_t>(x);
}

std::uint16_t& cell_at(StageGridData& grid, std::uint16_t x, std::uint16_t y) {
    return grid.cells[cell_index(grid, x, y)];
}

bool check_placement(
    const StageMapPlacement& placement,
    std::uint16_t anchor_x,
    std::uint16_t anchor_y,
    std::uint16_t model_id,
    std::uint16_t rotation,
    bool flip_x,
    bool flip_y,
    const char* message) {
    return check(
        placement.anchor_x == anchor_x &&
            placement.anchor_y == anchor_y &&
            placement.model_id == model_id &&
            placement.rotation == rotation &&
            placement.flip_x == flip_x &&
            placement.flip_y == flip_y,
        message);
}

bool test_direct_cells_preserve_flags_and_x_major_order() {
    StageGridData mp = make_grid(4u, 4u, 2u);
    StageGridData md = make_grid(4u, 4u, 1u);
    for (std::uint16_t y = 0u; y < mp.height; ++y) {
        for (std::uint16_t x = 0u; x < mp.width; ++x) {
            const std::uint16_t flags = static_cast<std::uint16_t>(
                cell_index(mp, x, y) << 12u);
            cell_at(mp, x, y) = static_cast<std::uint16_t>(flags | (cell_index(mp, x, y) + 1u));
        }
    }
    const StageGridData mp_before = mp;
    const StageGridData md_before = md;

    const std::vector<StageMapPlacement> placements = resolve_stage_map(
        mp,
        md,
        32u,
        {0, 3, 0, 3});
    if (!check(placements.size() == 16u, "direct placement count mismatch")) {
        return false;
    }

    std::size_t output_index = 0u;
    for (std::uint16_t x = 0u; x < mp.width; ++x) {
        for (std::uint16_t y = 0u; y < mp.height; ++y) {
            const std::size_t source_index = cell_index(mp, x, y);
            const std::uint16_t flags = static_cast<std::uint16_t>(source_index << 12u);
            if (!check_placement(
                    placements[output_index],
                    x,
                    y,
                    static_cast<std::uint16_t>(source_index + 1u),
                    static_cast<std::uint16_t>((flags >> 12u) * 0x4000u),
                    (flags & 0x4000u) != 0u,
                    (flags & 0x8000u) != 0u,
                    "direct placement flags or x-major order mismatch")) {
                return false;
            }
            ++output_index;
        }
    }

    return check(mp.cells == mp_before.cells && md.cells == md_before.cells,
                 "direct resolution mutated its input grids");
}

bool test_all_signed_nibble_redirects_resolve_outside_the_range() {
    constexpr std::uint16_t kWidth = 32u;
    constexpr std::uint16_t kHeight = 32u;
    constexpr std::uint16_t kSourceX = 16u;
    constexpr std::uint16_t kSourceY = 16u;
    constexpr std::uint16_t kTargetCell = 0x6005u;

    for (std::uint16_t raw = 0u; raw <= 0x00FFu; ++raw) {
        StageGridData mp = make_grid(kWidth, kHeight, 2u);
        StageGridData md = make_grid(kWidth, kHeight, 1u);
        cell_at(md, kSourceX, kSourceY) = raw;

        std::int32_t dx = static_cast<std::int32_t>(raw & 0x000Fu);
        std::int32_t dy = static_cast<std::int32_t>((raw >> 4u) & 0x000Fu);
        if (dx >= 8) {
            dx -= 16;
        }
        if (dy >= 8) {
            dy -= 16;
        }
        const std::uint16_t target_x = static_cast<std::uint16_t>(
            static_cast<std::int32_t>(kSourceX) + dx);
        const std::uint16_t target_y = static_cast<std::uint16_t>(
            static_cast<std::int32_t>(kSourceY) + dy);
        if (raw != 0u) {
            cell_at(mp, target_x, target_y) = kTargetCell;
        }

        const std::vector<StageMapPlacement> placements = resolve_stage_map(
            mp,
            md,
            6u,
            {kSourceX, kSourceX, kSourceY, kSourceY});
        if (raw == 0u) {
            if (!check(placements.empty(), "zero MD redirect emitted a placement")) {
                return false;
            }
            continue;
        }

        if (!check(placements.size() == 1u, "signed MD redirect placement count mismatch") ||
            !check_placement(
                placements[0],
                target_x,
                target_y,
                5u,
                0x8000u,
                true,
                false,
                "signed MD redirect placement mismatch")) {
            return false;
        }
    }

    return true;
}

bool test_zero_and_one_hop_redirects_skip() {
    StageGridData mp = make_grid(5u, 5u, 2u);
    StageGridData md = make_grid(5u, 5u, 1u);
    cell_at(md, 1u, 1u) = 0u;
    if (!check(
            resolve_stage_map(mp, md, 8u, {1, 1, 1, 1}).empty(),
            "zero redirect did not skip")) {
        return false;
    }

    cell_at(md, 1u, 1u) = 0x0011u;
    cell_at(md, 2u, 2u) = 0x0011u;
    cell_at(mp, 3u, 3u) = 7u;
    return check(
        resolve_stage_map(mp, md, 8u, {1, 1, 1, 1}).empty(),
        "MD redirect recursively followed a zero target cell");
}

bool test_model_count_boundaries() {
    StageGridData mp = make_grid(4u, 1u, 2u);
    StageGridData md = make_grid(4u, 1u, 1u);
    cell_at(mp, 0u, 0u) = 0u;
    cell_at(mp, 1u, 0u) = 4u;
    cell_at(mp, 2u, 0u) = 5u;
    cell_at(mp, 3u, 0u) = 0x0FFFu;

    const std::vector<StageMapPlacement> count_five = resolve_stage_map(
        mp,
        md,
        5u,
        {0, 3, 0, 0});
    if (!check(count_five.size() == 1u, "model-count boundary output count mismatch") ||
        !check_placement(
            count_five[0], 1u, 0u, 4u, 0u, false, false,
            "model-count final valid ID mismatch")) {
        return false;
    }

    const std::vector<StageMapPlacement> count_4096 = resolve_stage_map(
        mp,
        md,
        4096u,
        {3, 3, 0, 0});
    if (!check(count_4096.size() == 1u, "maximum supported model count rejected ID 4095") ||
        !check_placement(
            count_4096[0], 3u, 0u, 0x0FFFu, 0u, false, false,
            "ID 4095 placement mismatch")) {
        return false;
    }

    return check(
        resolve_stage_map(mp, md, 4095u, {3, 3, 0, 0}).empty(),
        "ID equal to model count was accepted");
}

bool test_duplicate_anchors_and_distinct_matching_models() {
    StageGridData mp = make_grid(3u, 1u, 2u);
    StageGridData md = make_grid(3u, 1u, 1u);
    cell_at(md, 0u, 0u) = 0x0001u;
    cell_at(mp, 1u, 0u) = 3u;
    cell_at(mp, 2u, 0u) = 3u;

    const std::vector<StageMapPlacement> placements = resolve_stage_map(
        mp,
        md,
        4u,
        {0, 2, 0, 0});
    return check(placements.size() == 2u, "duplicate anchor or distinct-model count mismatch") &&
           check_placement(
               placements[0], 1u, 0u, 3u, 0u, false, false,
               "MD-resolved anchor was not retained first") &&
           check_placement(
               placements[1], 2u, 0u, 3u, 0u, false, false,
               "same model ID at a distinct anchor was deduplicated");
}

bool test_anchor_flags_override_high_source_flags_and_direct_ignores_md() {
    StageGridData mp = make_grid(3u, 1u, 2u);
    StageGridData md = make_grid(3u, 1u, 1u);
    cell_at(mp, 0u, 0u) = 0xF000u;
    cell_at(md, 0u, 0u) = 0x0001u;
    cell_at(mp, 1u, 0u) = 0x5002u;
    cell_at(mp, 2u, 0u) = 4u;
    cell_at(md, 2u, 0u) = 0x0100u;

    const std::vector<StageMapPlacement> placements = resolve_stage_map(
        mp,
        md,
        5u,
        {0, 2, 0, 0});
    return check(placements.size() == 2u, "anchor-flags/direct-MD output count mismatch") &&
           check_placement(
               placements[0], 1u, 0u, 2u, 0x4000u, true, false,
               "anchor flags were replaced by source flags") &&
           check_placement(
               placements[1], 2u, 0u, 4u, 0u, false, false,
               "direct cell incorrectly consumed its MD byte");
}

bool test_rejects_malformed_inputs_and_out_of_grid_redirects() {
    StageGridData valid_mp = make_grid(2u, 2u, 2u);
    StageGridData valid_md = make_grid(2u, 2u, 1u);
    const StageMapRange valid_range = {0, 1, 0, 1};

    StageGridData bad_mp_bytes = valid_mp;
    bad_mp_bytes.cell_bytes = 1u;
    StageGridData bad_md_bytes = valid_md;
    bad_md_bytes.cell_bytes = 2u;
    StageGridData bad_width = valid_mp;
    bad_width.width = 0u;
    StageGridData mismatched_dimensions = valid_md;
    mismatched_dimensions.width = 1u;
    StageGridData short_mp = valid_mp;
    short_mp.cells.pop_back();
    StageGridData short_md = valid_md;
    short_md.cells.pop_back();

    if (!expects_stage_data_error(
            [&]() { resolve_stage_map(bad_mp_bytes, valid_md, 1u, valid_range); },
            "non-word MP grid was accepted") ||
        !expects_stage_data_error(
            [&]() { resolve_stage_map(valid_mp, bad_md_bytes, 1u, valid_range); },
            "non-byte MD grid was accepted") ||
        !expects_stage_data_error(
            [&]() { resolve_stage_map(bad_width, valid_md, 1u, valid_range); },
            "zero grid dimension was accepted") ||
        !expects_stage_data_error(
            [&]() { resolve_stage_map(valid_mp, mismatched_dimensions, 1u, valid_range); },
            "mismatched grid dimensions were accepted") ||
        !expects_stage_data_error(
            [&]() { resolve_stage_map(short_mp, valid_md, 1u, valid_range); },
            "short MP storage was accepted") ||
        !expects_stage_data_error(
            [&]() { resolve_stage_map(valid_mp, short_md, 1u, valid_range); },
            "short MD storage was accepted") ||
        !expects_stage_data_error(
            [&]() { resolve_stage_map(valid_mp, valid_md, 0u, valid_range); },
            "zero model count was accepted") ||
        !expects_stage_data_error(
            [&]() { resolve_stage_map(valid_mp, valid_md, 4097u, valid_range); },
            "model count above the supported range was accepted") ||
        !expects_stage_data_error(
            [&]() { resolve_stage_map(valid_mp, valid_md, 1u, {-1, 0, 0, 0}); },
            "negative range coordinate was accepted") ||
        !expects_stage_data_error(
            [&]() { resolve_stage_map(valid_mp, valid_md, 1u, {0, 2, 0, 0}); },
            "range coordinate beyond the grid was accepted")) {
        return false;
    }

    if (!check(
            resolve_stage_map(valid_mp, valid_md, 1u, {1, 0, 0, 1}).empty() &&
                resolve_stage_map(valid_mp, valid_md, 1u, {0, 1, 1, 0}).empty(),
            "reversed range did not return empty")) {
        return false;
    }

    StageGridData malformed_before_range = valid_mp;
    malformed_before_range.cells.pop_back();
    if (!expects_stage_data_error(
            [&]() {
                resolve_stage_map(
                    malformed_before_range,
                    valid_md,
                    1u,
                    {1, 0, 0, 1});
            },
            "reversed range bypassed structural validation")) {
        return false;
    }

    StageGridData high_md = valid_md;
    high_md.cells[0] = 0x0100u;
    if (!expects_stage_data_error(
            [&]() { resolve_stage_map(valid_mp, high_md, 1u, {0, 0, 0, 0}); },
            "consumed MD value above one byte was accepted")) {
        return false;
    }

    const auto rejects_edge_redirect = [](std::uint16_t x, std::uint16_t y, std::uint16_t redirect) {
        StageGridData edge_mp = make_grid(2u, 2u, 2u);
        StageGridData edge_md = make_grid(2u, 2u, 1u);
        cell_at(edge_md, x, y) = redirect;
        return expects_stage_data_error(
            [&]() {
                resolve_stage_map(
                    edge_mp,
                    edge_md,
                    1u,
                    {x, x, y, y});
            },
            "out-of-grid MD redirect was accepted");
    };
    return rejects_edge_redirect(0u, 0u, 0x000Fu) &&
           rejects_edge_redirect(0u, 0u, 0x00F0u) &&
           rejects_edge_redirect(1u, 1u, 0x0001u) &&
           rejects_edge_redirect(1u, 1u, 0x0010u);
}

}

int main() {
    if (!test_direct_cells_preserve_flags_and_x_major_order() ||
        !test_all_signed_nibble_redirects_resolve_outside_the_range() ||
        !test_zero_and_one_hop_redirects_skip() ||
        !test_model_count_boundaries() ||
        !test_duplicate_anchors_and_distinct_matching_models() ||
        !test_anchor_flags_override_high_source_flags_and_direct_ignores_md() ||
        !test_rejects_malformed_inputs_and_out_of_grid_redirects()) {
        return 1;
    }

    std::printf("stage placement tests: 7 groups, 256 signed redirects\n");
    return 0;
}
