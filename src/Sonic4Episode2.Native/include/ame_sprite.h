#pragma once

#include "ame_runtime.h"
#include "camera_matrix.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <stdexcept>

struct AmeSpriteVertex {
    std::array<float, 3u> position;
    std::uint32_t color_rgba;
    std::array<float, 2u> texcoord;
};
static_assert(sizeof(AmeSpriteVertex) == 24u);
static_assert(offsetof(AmeSpriteVertex, color_rgba) == 12u);
static_assert(offsetof(AmeSpriteVertex, texcoord) == 16u);

struct AmeSpriteQuad {
    std::array<AmeSpriteVertex, 6u> vertices;
};

class AmeSpriteError : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

AmeSpriteQuad ame_sprite_quad(
    const AmeRuntimeSprite& sprite,
    const CameraMatrix& world_view,
    CameraPrecision precision);

std::uint32_t ame_sprite_d3d_color(std::uint32_t color_rgba) noexcept;
