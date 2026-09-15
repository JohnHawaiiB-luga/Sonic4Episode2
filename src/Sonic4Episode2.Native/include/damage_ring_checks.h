#pragma once

#include <cstdint>

struct RingPickupBounds {
    float x;
    float y;
    std::int16_t left;
    std::int16_t top;
    std::int16_t right;
    std::int16_t bottom;
};

// Preconditions: origins are finite with truncated values in int32_t; FP exceptions are masked, rounding is nearest, and gradual underflow is enabled.
bool damage_ring_pickup_overlaps(const RingPickupBounds& a, const RingPickupBounds& b);

bool damage_ring_passes_draw_timer(std::uint16_t timer);
