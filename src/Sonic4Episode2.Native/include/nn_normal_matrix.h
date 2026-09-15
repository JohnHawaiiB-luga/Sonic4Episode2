#pragma once

#include "nn_shader_constants.h"

#include <array>

using NnNormalMatrix = std::array<float, 9u>;

NnNormalMatrix nn_normal_matrix(const NnShaderMatrix& model_view, CameraPrecision precision);
