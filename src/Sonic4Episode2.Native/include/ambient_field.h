#pragma once

#include "camera_matrix.h"
#include "stage_data.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

struct AmbientFieldPoint {
    std::array<float, 2u> center;
    std::array<float, 4u> bounds;
    std::uint8_t palette_index;
    std::uint8_t disabled_edges;
    float fade;
};

using AmbientFieldPalettes = std::array<std::array<float, 4u>, 8u>;

AmbientFieldPalettes parse_pc_stage_ambient_palettes(
    const std::uint8_t* data, std::size_t size, std::uint32_t stage_index);

std::vector<AmbientFieldPoint> make_ambient_field_points(const StageEventPlacements& events);

std::array<float, 4u> ambient_field_ring_color(
    const std::vector<AmbientFieldPoint>& points,
    const AmbientFieldPalettes& palettes,
    const std::array<float, 2u>& position,
    CameraPrecision precision);

