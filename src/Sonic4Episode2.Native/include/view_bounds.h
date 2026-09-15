#pragma once

#include <cstdint>

struct ViewState {
    float origin[2];
    float scale[2];
    std::int16_t width;
    std::int16_t height;
    bool enabled;
};

struct ViewMargins {
    std::int16_t margin;
    std::int16_t left;
    std::int16_t top;
    std::int16_t right;
    std::int16_t bottom;
};

// Preconditions: round-to-nearest binary64 intermediates and gradual underflow;
// finite coordinates, origins, and scales; truncated values and scaled extents fit int32_t.
bool is_outside_view(float x, float y, const ViewState& view, const ViewMargins& margins);
