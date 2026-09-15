#include "damage_ring_collision.h"

#include <cstdint>

namespace {

void reflect_velocity(float& velocity) {
    const float reduced = static_cast<float>(
        static_cast<double>(velocity) - static_cast<double>(velocity) * 0.25);
    velocity = -reduced;
}

void transition_collision_flags(DamageRing& ring) {
    if ((ring.parameter_b & 0x80u) != 0u) {
        ring.parameter_b = static_cast<std::uint16_t>(
            (static_cast<std::uint32_t>(ring.parameter_b) & ~0x80u) | 0x40u);
    }
}

}

void resolve_damage_ring_collision(DamageRing& ring, DamageRingCollisionQuery query, void* context) {
    DamageRingCollisionProbe probe{
        ring.position[0],
        ring.position[1],
        static_cast<std::uint16_t>((ring.parameter_b & 0x2u) != 0u ? 1u : 0u),
        DamageRingCollisionDirection::PositiveY,
    };

    if (ring.velocity[1] > 0.0f) {
        probe.y = static_cast<float>(static_cast<double>(probe.y) + 9.0);
        probe.direction = DamageRingCollisionDirection::PositiveY;
        const float offset = query(probe, context);
        if (offset < 0.0f) {
            if ((ring.parameter_b & 0x4u) == 0u) {
                ring.position[1] = static_cast<float>(
                    static_cast<double>(ring.position[1]) + static_cast<double>(offset));
            }
            else {
                ring.position[1] = static_cast<float>(
                    static_cast<double>(ring.position[1]) - static_cast<double>(offset));
            }
            reflect_velocity(ring.velocity[1]);
            transition_collision_flags(ring);
        }
    }
    else if (ring.velocity[1] < 0.0f) {
        probe.y = static_cast<float>(static_cast<double>(probe.y) - 9.0);
        probe.direction = DamageRingCollisionDirection::NegativeY;
        const float offset = query(probe, context);
        if (offset < 0.0f) {
            if ((ring.parameter_b & 0x4u) == 0u) {
                ring.position[1] = static_cast<float>(
                    static_cast<double>(ring.position[1]) - static_cast<double>(offset));
            }
            else {
                ring.position[1] = static_cast<float>(
                    static_cast<double>(ring.position[1]) + static_cast<double>(offset));
            }
            reflect_velocity(ring.velocity[1]);
            transition_collision_flags(ring);
        }
    }

    probe.y = ring.position[1];
    if (ring.velocity[0] > 0.0f) {
        probe.x = static_cast<float>(static_cast<double>(probe.x) + 9.0);
        probe.direction = DamageRingCollisionDirection::PositiveX;
        const float offset = query(probe, context);
        if (offset < 0.0f) {
            ring.position[0] = static_cast<float>(
                static_cast<double>(ring.position[0]) + static_cast<double>(offset));
            reflect_velocity(ring.velocity[0]);
            transition_collision_flags(ring);
        }
    }
    else if (ring.velocity[0] < 0.0f) {
        probe.x = static_cast<float>(static_cast<double>(probe.x) - 9.0);
        probe.direction = DamageRingCollisionDirection::NegativeX;
        const float offset = query(probe, context);
        if (offset < 0.0f) {
            ring.position[0] = static_cast<float>(
                static_cast<double>(ring.position[0]) - static_cast<double>(offset));
            reflect_velocity(ring.velocity[0]);
            transition_collision_flags(ring);
        }
    }
}
