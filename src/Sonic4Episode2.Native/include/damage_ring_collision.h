#pragma once

#include "damage_rings.h"

#include <cstdint>

enum class DamageRingCollisionDirection : std::uint16_t {
    PositiveX = 0u,
    NegativeX = 1u,
    PositiveY = 2u,
    NegativeY = 3u,
};

struct DamageRingCollisionProbe {
    float x;
    float y;
    std::uint16_t flags;
    DamageRingCollisionDirection direction;
};

using DamageRingCollisionQuery = float (*)(const DamageRingCollisionProbe&, void*);

// Preconditions: non-null query; finite initial X/Y, velocities, and query results;
// round-to-nearest binary64 arithmetic, gradual underflow, and masked FP exceptions.
// Query must not modify ring or FP controls. Corrected coordinates may overflow.
void resolve_damage_ring_collision(DamageRing& ring, DamageRingCollisionQuery query, void* context);
