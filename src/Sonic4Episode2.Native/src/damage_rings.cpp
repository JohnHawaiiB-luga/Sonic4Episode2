#include "damage_rings.h"

#include "mt_math_rand.h"
#include "nn_trig.h"

DamageRing* create_damage_ring(
    std::uint32_t& seed,
    DamageRingSystem* system,
    const DamageRingCreateArgs& args) {
    if (system == nullptr || system->allocation_cursor >= kDamageRingSlotCount) {
        return nullptr;
    }

    DamageRing* ring = system->slots[system->allocation_cursor++];
    if (ring == nullptr) {
        return nullptr;
    }

    for (std::size_t index = 0u; index < 3u; ++index) {
        ring->position[index] = args.position[index];
        ring->scale[index] = 1.0f;
    }
    for (std::size_t index = 0u; index < 2u; ++index) {
        ring->velocity[index] = args.velocity[index];
    }
    ring->timer = static_cast<std::uint16_t>((mt_math_rand(seed) & 31u) + 256u);
    ring->parameter_a = args.parameter_a;
    ring->parameter_b = args.parameter_b;
    ring->auxiliary_a = 0u;
    ring->auxiliary_b = 0u;
    for (float& value : ring->modulation) {
        value = 1.0f;
    }

    DamageRing* previous_tail = system->damage_tail;
    ring->previous = previous_tail;
    ring->next = nullptr;
    if (previous_tail == nullptr) {
        system->damage_head = ring;
    }
    else {
        previous_tail->next = ring;
    }
    system->damage_tail = ring;
    return ring;
}

bool tick_damage_ring_lifetime(DamageRingSystem& system, DamageRing& ring) {
    ring.timer = static_cast<std::uint16_t>(ring.timer - 1u);
    if (ring.timer != 0u) {
        return false;
    }

    DamageRing* const previous = ring.previous;
    DamageRing* const next = ring.next;
    if (previous == nullptr) {
        system.damage_head = next;
    }
    else {
        previous->next = next;
    }
    if (next == nullptr) {
        system.damage_tail = previous;
    }
    else {
        next->previous = previous;
    }

    --system.allocation_cursor;
    system.slots[system.allocation_cursor] = &ring;
    return true;
}

void advance_damage_ring_position(DamageRing& ring) {
    ring.position[0] = ring.position[0] + ring.velocity[0];
    if ((ring.parameter_b & 0x4u) != 0u) {
        ring.position[1] = ring.position[1] - ring.velocity[1];
    }
    else {
        ring.position[1] = ring.velocity[1] + ring.position[1];
    }
}

void accelerate_damage_ring(
    DamageRing& ring,
    DamageRingAcceleration& acceleration,
    std::uint16_t angle,
    bool reuse_acceleration) {
    if (!reuse_acceleration) {
        acceleration.horizontal = nn_sin(angle) * -0.0703125f;
        acceleration.vertical = nn_cos(angle) * 0.0703125f;
    }
    ring.velocity[0] = ring.velocity[0] + acceleration.horizontal;
    ring.velocity[1] = ring.velocity[1] + acceleration.vertical;
}
