#include "view_bounds.h"

#include <cstdint>

namespace {

constexpr float kScaleDeadbandLower = 1.0f - 0x1p-23f;
constexpr float kScaleDeadbandUpper = 1.0f + 0x1p-23f;

std::int16_t wrap16(std::int32_t value) {
    const std::uint16_t low = static_cast<std::uint16_t>(
        static_cast<std::uint32_t>(value) & 0xFFFFu);
    if (low <= 0x7FFFu) {
        return static_cast<std::int16_t>(low);
    }
    return static_cast<std::int16_t>(static_cast<std::int32_t>(low) - 0x10000);
}

std::int32_t wrap32(std::int64_t value) {
    const std::uint32_t low = static_cast<std::uint32_t>(
        static_cast<std::uint64_t>(value) & 0xFFFFFFFFull);
    if (low <= 0x7FFFFFFFu) {
        return static_cast<std::int32_t>(low);
    }
    return static_cast<std::int32_t>(static_cast<std::int64_t>(low) - 0x100000000ll);
}

std::int16_t effective_extent(std::int16_t extent, float scale) {
    if (scale >= kScaleDeadbandLower && scale <= kScaleDeadbandUpper) {
        return extent;
    }

    const double scaled = static_cast<double>(extent) * (2.0 - static_cast<double>(scale));
    return wrap16(static_cast<std::int32_t>(scaled));
}

std::int32_t truncate_to_int32(float value) {
    return static_cast<std::int32_t>(static_cast<double>(value));
}

}

bool is_outside_view(float x, float y, const ViewState& view, const ViewMargins& margins) {
    if (!view.enabled) {
        return false;
    }

    const std::int32_t truncated_x = truncate_to_int32(x);
    const std::int32_t truncated_y = truncate_to_int32(y);
    const std::int32_t origin_x = truncate_to_int32(view.origin[0]);
    const std::int32_t origin_y = truncate_to_int32(view.origin[1]);
    const std::int16_t width = effective_extent(view.width, view.scale[0]);
    const std::int16_t height = effective_extent(view.height, view.scale[1]);

    const std::int32_t low_x = wrap32(
        static_cast<std::int64_t>(origin_x) - margins.margin + margins.left);
    const std::int32_t high_x = wrap32(
        static_cast<std::int64_t>(origin_x) + width + margins.margin + margins.right);
    const std::int32_t low_y = wrap32(
        static_cast<std::int64_t>(origin_y) - margins.margin + margins.top);
    const std::int32_t high_y = wrap32(
        static_cast<std::int64_t>(origin_y) + height + margins.margin + margins.bottom);

    return truncated_x < low_x || truncated_x > high_x || truncated_y < low_y || truncated_y > high_y;
}
