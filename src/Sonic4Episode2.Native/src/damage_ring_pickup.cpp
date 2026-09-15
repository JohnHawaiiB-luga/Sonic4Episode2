#include "damage_ring_pickup.h"

#include <cstdint>

namespace {

constexpr std::uint32_t kBlockedPlayerFlags = 0x01300400u;

std::int32_t signed_timer(std::uint16_t timer) {
    return timer <= 0x7FFFu
               ? static_cast<std::int32_t>(timer)
               : static_cast<std::int32_t>(timer) - 0x10000;
}

}

bool damage_ring_pickup_enabled(const RingPickupPermission& permission) {
    return (permission.stage_id < 28u && permission.game_mode == 1u) ||
           (permission.player_zero_present &&
            (permission.player_zero_flags & kBlockedPlayerFlags) == 0u);
}

std::int32_t find_static_ring_pickup(
    const StaticRingPickupState& ring,
    const RingPickupCandidate* candidates,
    std::uint8_t candidate_count) {
    const RingPickupBounds ring_bounds{ring.x, ring.y, -9, -9, 9, 9};
    for (std::uint32_t index = 0u; index < candidate_count; ++index) {
        const RingPickupCandidate& candidate = candidates[index];
        if (!candidate.present ||
            (candidate.player_flags & kBlockedPlayerFlags) != 0u ||
            (ring.manager_flags & 0x80u) != 0u ||
            !ring.pickup_enabled) {
            continue;
        }

        if ((ring.manager_flags & 0x20u) != 0u &&
            ((static_cast<std::uint32_t>(ring.ring_flags) >> 1u) & 1u) !=
                (candidate.plane_flags & 1u)) {
            continue;
        }

        if (damage_ring_pickup_overlaps(candidate.bounds, ring_bounds)) {
            return static_cast<std::int32_t>(index);
        }
    }

    return -1;
}

std::int32_t find_damage_ring_pickup(
    const DamageRingPickupState& ring,
    const RingPickupCandidate* candidates,
    std::uint8_t candidate_count) {
    if (ring.remaining_timer == 0u || signed_timer(ring.remaining_timer) > 216) {
        return -1;
    }
    return find_static_ring_pickup(
        {ring.x, ring.y, ring.ring_flags, ring.manager_flags, ring.pickup_enabled},
        candidates,
        candidate_count);
}
