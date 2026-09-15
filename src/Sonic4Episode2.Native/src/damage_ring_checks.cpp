#include "damage_ring_checks.h"

#include <cstdint>

namespace {

std::int32_t wrap32(std::int64_t value) {
    const std::uint32_t low = static_cast<std::uint32_t>(
        static_cast<std::uint64_t>(value) & 0xFFFFFFFFull);
    if (low <= 0x7FFFFFFFu) {
        return static_cast<std::int32_t>(low);
    }
    return static_cast<std::int32_t>(static_cast<std::int64_t>(low) - 0x100000000ll);
}

std::int32_t truncate_to_int32(float value) {
    return static_cast<std::int32_t>(static_cast<double>(value));
}

bool axis_overlaps(
    float a_origin,
    std::int16_t a_low,
    std::int16_t a_high,
    float b_origin,
    std::int16_t b_low,
    std::int16_t b_high) {
    const std::int32_t a_start = wrap32(
        static_cast<std::int64_t>(truncate_to_int32(a_origin)) + a_low);
    const std::int32_t b_start = wrap32(
        static_cast<std::int64_t>(truncate_to_int32(b_origin)) + b_low);
    const std::int32_t a_extent =
        static_cast<std::int32_t>(a_high) - static_cast<std::int32_t>(a_low);
    const std::int32_t b_extent =
        static_cast<std::int32_t>(b_high) - static_cast<std::int32_t>(b_low);
    const std::int32_t a_end = wrap32(static_cast<std::int64_t>(a_start) + a_extent);
    const std::int32_t b_end = wrap32(static_cast<std::int64_t>(b_start) + b_extent);

    return (a_start <= b_start && a_end >= b_start) ||
           (b_start <= a_start && b_end >= a_start);
}

}

bool damage_ring_pickup_overlaps(const RingPickupBounds& a, const RingPickupBounds& b) {
    return axis_overlaps(a.x, a.left, a.right, b.x, b.left, b.right) &&
           axis_overlaps(a.y, a.top, a.bottom, b.y, b.top, b.bottom);
}

bool damage_ring_passes_draw_timer(std::uint16_t timer) {
    const std::int32_t signed_timer = timer <= 0x7FFFu
                                          ? static_cast<std::int32_t>(timer)
                                          : static_cast<std::int32_t>(timer) - 0x10000;
    return signed_timer > 32 || (timer & 0x2u) != 0u;
}
