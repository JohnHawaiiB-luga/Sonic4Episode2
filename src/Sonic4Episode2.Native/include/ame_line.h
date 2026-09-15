#pragma once

#include "ame_sprite.h"

struct AmeLineQuad {
    std::array<AmeSpriteVertex, 6u> vertices;
    float sort_z;
};

class AmeLineError : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

AmeLineQuad ame_line_quad(
    const AmeRuntimeLine& line,
    const CameraMatrix& world_view,
    const std::array<float, 3u>& camera_position,
    CameraPrecision precision);
