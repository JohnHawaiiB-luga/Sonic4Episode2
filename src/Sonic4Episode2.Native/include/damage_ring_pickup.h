#pragma once

#include "damage_ring_checks.h"

#include <cstdint>

struct RingPickupPermission {
    std::uint16_t stage_id;
    std::uint32_t game_mode;
    bool player_zero_present;
    std::uint32_t player_zero_flags;
};

bool damage_ring_pickup_enabled(const RingPickupPermission& permission);

struct RingPickupCandidate {
    bool present;
    std::uint32_t player_flags;
    std::uint32_t plane_flags;
    RingPickupBounds bounds;
};

struct DamageRingPickupState {
    float x;
    float y;
    std::uint16_t remaining_timer;
    std::uint16_t ring_flags;
    std::uint32_t manager_flags;
    bool pickup_enabled;
};

struct StaticRingPickupState {
    float x;
    float y;
    std::uint16_t ring_flags;
    std::uint32_t manager_flags;
    bool pickup_enabled;
};

std::int32_t find_static_ring_pickup(
    const StaticRingPickupState& ring,
    const RingPickupCandidate* candidates,
    std::uint8_t candidate_count);

// Preconditions: candidate_count is 0..2 with that many readable candidates; ring XY and candidate bounds satisfy damage_ring_pickup_overlaps preconditions.
std::int32_t find_damage_ring_pickup(
    const DamageRingPickupState& ring,
    const RingPickupCandidate* candidates,
    std::uint8_t candidate_count);
