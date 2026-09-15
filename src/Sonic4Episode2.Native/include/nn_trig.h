#pragma once

#include "camera_math.h"

#include <cstdint>

struct NnSinCos {
    float sine;
    float cosine;
};

float nn_sin(std::uint16_t angle);
float nn_cos(std::uint16_t angle);
NnSinCos nn_sin_cos(std::uint16_t angle, CameraPrecision precision);
