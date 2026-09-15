#pragma once

#include "camera_math.h"

#include <cstddef>
#include <stdexcept>
#include <vector>

class AmeDrawError : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

std::vector<std::size_t> ame_primitive_sort_order(
    const std::vector<float>& depths,
    CameraPrecision precision);
