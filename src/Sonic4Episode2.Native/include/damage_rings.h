#pragma once

#include <cstddef>
#include <cstdint>

constexpr std::size_t kDamageRingSlotCount = 96u;

struct DamageRing {
    float position[3];
    float velocity[2];
    float scale[3];
    std::uint16_t timer;
    std::uint16_t parameter_a;
    std::uint16_t parameter_b;
    std::uint32_t auxiliary_a;
    std::uint32_t auxiliary_b;
    float modulation[4];
    DamageRing* previous;
    DamageRing* next;
};

struct DamageRingSystem {
    DamageRing* slots[kDamageRingSlotCount];
    std::uint32_t allocation_cursor;
    DamageRing* damage_head;
    DamageRing* damage_tail;
};

struct DamageRingCreateArgs {
    float position[3];
    float velocity[2];
    std::uint16_t parameter_a;
    std::uint16_t parameter_b;
};

DamageRing* create_damage_ring(
    std::uint32_t& seed,
    DamageRingSystem* system,
    const DamageRingCreateArgs& args);

bool tick_damage_ring_lifetime(DamageRingSystem& system, DamageRing& ring);

void advance_damage_ring_position(DamageRing& ring);

struct DamageRingAcceleration {
    float horizontal = 0.0f;
    float vertical = 0.0703125f;
};

void accelerate_damage_ring(
    DamageRing& ring,
    DamageRingAcceleration& acceleration,
    std::uint16_t angle,
    bool reuse_acceleration);
