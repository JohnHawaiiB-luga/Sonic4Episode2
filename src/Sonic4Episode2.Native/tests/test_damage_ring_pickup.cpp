#include "damage_ring_pickup.h"

#include <array>
#include <cstdint>
#include <cstdio>
#include <cstring>

namespace {

constexpr std::uint32_t kBlockedPlayerFlags = 0x01300400u;

bool check(bool condition, const char* message) {
    if (condition) {
        return true;
    }
    std::fprintf(stderr, "%s\n", message);
    return false;
}

std::uint32_t float_bits(float value) {
    std::uint32_t bits = 0u;
    std::memcpy(&bits, &value, sizeof(bits));
    return bits;
}

RingPickupBounds make_bounds(
    float x,
    float y,
    std::int16_t left,
    std::int16_t top,
    std::int16_t right,
    std::int16_t bottom) {
    return {x, y, left, top, right, bottom};
}

RingPickupPermission make_permission(
    std::uint16_t stage_id,
    std::uint32_t game_mode,
    bool player_zero_present,
    std::uint32_t player_zero_flags) {
    return {stage_id, game_mode, player_zero_present, player_zero_flags};
}

RingPickupCandidate make_candidate(
    bool present,
    std::uint32_t player_flags,
    std::uint32_t plane_flags,
    RingPickupBounds bounds) {
    return {present, player_flags, plane_flags, bounds};
}

DamageRingPickupState make_ring(
    float x,
    float y,
    std::uint16_t remaining_timer,
    std::uint16_t ring_flags,
    std::uint32_t manager_flags,
    bool pickup_enabled) {
    return {x, y, remaining_timer, ring_flags, manager_flags, pickup_enabled};
}

struct BoundsSnapshot {
    std::uint32_t x;
    std::uint32_t y;
    std::int16_t left;
    std::int16_t top;
    std::int16_t right;
    std::int16_t bottom;
};

BoundsSnapshot snapshot_bounds(const RingPickupBounds& bounds) {
    return {
        float_bits(bounds.x),
        float_bits(bounds.y),
        bounds.left,
        bounds.top,
        bounds.right,
        bounds.bottom,
    };
}

bool same_bounds(const BoundsSnapshot& expected, const RingPickupBounds& actual) {
    const BoundsSnapshot current = snapshot_bounds(actual);
    return expected.x == current.x &&
           expected.y == current.y &&
           expected.left == current.left &&
           expected.top == current.top &&
           expected.right == current.right &&
           expected.bottom == current.bottom;
}

struct CandidateSnapshot {
    bool present;
    std::uint32_t player_flags;
    std::uint32_t plane_flags;
    BoundsSnapshot bounds;
};

CandidateSnapshot snapshot_candidate(const RingPickupCandidate& candidate) {
    return {
        candidate.present,
        candidate.player_flags,
        candidate.plane_flags,
        snapshot_bounds(candidate.bounds),
    };
}

bool same_candidate(const CandidateSnapshot& expected, const RingPickupCandidate& actual) {
    return expected.present == actual.present &&
           expected.player_flags == actual.player_flags &&
           expected.plane_flags == actual.plane_flags &&
           same_bounds(expected.bounds, actual.bounds);
}

struct PickupStateSnapshot {
    std::uint32_t x;
    std::uint32_t y;
    std::uint16_t remaining_timer;
    std::uint16_t ring_flags;
    std::uint32_t manager_flags;
    bool pickup_enabled;
};

PickupStateSnapshot snapshot_ring(const DamageRingPickupState& ring) {
    return {
        float_bits(ring.x),
        float_bits(ring.y),
        ring.remaining_timer,
        ring.ring_flags,
        ring.manager_flags,
        ring.pickup_enabled,
    };
}

bool same_ring(const PickupStateSnapshot& expected, const DamageRingPickupState& actual) {
    const PickupStateSnapshot current = snapshot_ring(actual);
    return expected.x == current.x &&
           expected.y == current.y &&
           expected.remaining_timer == current.remaining_timer &&
           expected.ring_flags == current.ring_flags &&
           expected.manager_flags == current.manager_flags &&
           expected.pickup_enabled == current.pickup_enabled;
}

bool check_scan(
    std::int32_t expected_result,
    DamageRingPickupState& ring,
    std::array<RingPickupCandidate, 2u>& candidates,
    std::uint8_t candidate_count,
    const char* message) {
    const PickupStateSnapshot ring_before = snapshot_ring(ring);
    const std::array<CandidateSnapshot, 2u> candidates_before = {{
        snapshot_candidate(candidates[0]),
        snapshot_candidate(candidates[1]),
    }};
    const std::int32_t actual_result = find_damage_ring_pickup(
        ring,
        candidates.data(),
        candidate_count);

    return check(actual_result == expected_result, message) &&
           check(same_ring(ring_before, ring), "pickup scan mutated ring state") &&
           check(
               same_candidate(candidates_before[0], candidates[0]) &&
                   same_candidate(candidates_before[1], candidates[1]),
               "pickup scan mutated a candidate");
}

bool test_permission_paths_and_mask_bits() {
    struct PermissionCase {
        RingPickupPermission permission;
        bool expected;
    };

    const std::array<PermissionCase, 11u> cases = {{
        {make_permission(27u, 1u, false, kBlockedPlayerFlags), true},
        {make_permission(28u, 1u, false, 0u), false},
        {make_permission(65535u, 1u, false, 0u), false},
        {make_permission(27u, 0u, false, 0u), false},
        {make_permission(27u, 2u, false, 0u), false},
        {make_permission(28u, 0u, true, 0u), true},
        {make_permission(65535u, 0u, true, 0x80000001u), true},
        {make_permission(28u, 0u, true, 0x00000400u), false},
        {make_permission(28u, 0u, true, 0x00100000u), false},
        {make_permission(28u, 0u, true, 0x00200000u), false},
        {make_permission(28u, 0u, true, 0x01000000u), false},
    }};

    for (const PermissionCase& item : cases) {
        RingPickupPermission permission = item.permission;
        const RingPickupPermission before = permission;
        if (!check(
                damage_ring_pickup_enabled(permission) == item.expected,
                "pickup permission result mismatch") ||
            !check(
                permission.stage_id == before.stage_id &&
                    permission.game_mode == before.game_mode &&
                    permission.player_zero_present == before.player_zero_present &&
                    permission.player_zero_flags == before.player_zero_flags,
                "pickup permission mutated its input")) {
            return false;
        }
    }
    return true;
}

bool test_timer_delay_and_wrapped_values() {
    struct TimerCase {
        std::uint16_t timer;
        std::int32_t expected;
    };

    const std::array<TimerCase, 6u> cases = {{
        {0u, -1},
        {216u, 0},
        {217u, -1},
        {32767u, -1},
        {32768u, 0},
        {65535u, 0},
    }};

    for (const TimerCase& item : cases) {
        DamageRingPickupState ring = make_ring(0.0f, 0.0f, item.timer, 0u, 0u, true);
        std::array<RingPickupCandidate, 2u> candidates{};
        candidates[0] = make_candidate(true, 0u, 0u, make_bounds(0.0f, 0.0f, 0, 0, 0, 0));
        if (!check_scan(item.expected, ring, candidates, 1u, "timer delay selected the wrong pickup result")) {
            return false;
        }
    }
    return true;
}

bool test_empty_count_allows_null_candidates() {
    DamageRingPickupState ring = make_ring(0.0f, 0.0f, 1u, 0u, 0u, true);
    const PickupStateSnapshot before = snapshot_ring(ring);

    return check(find_damage_ring_pickup(ring, nullptr, 0u) == -1, "empty candidate count found a pickup") &&
           check(same_ring(before, ring), "empty candidate count mutated ring state");
}

bool test_first_eligible_candidate_selection() {
    DamageRingPickupState ring = make_ring(0.0f, 0.0f, 1u, 0u, 0u, true);
    const RingPickupCandidate eligible =
        make_candidate(true, 0u, 0u, make_bounds(0.0f, 0.0f, 0, 0, 0, 0));
    std::array<RingPickupCandidate, 2u> candidates{};

    candidates[0] = make_candidate(false, 0u, 0u, make_bounds(0.0f, 0.0f, 0, 0, 0, 0));
    candidates[1] = eligible;
    if (!check_scan(1, ring, candidates, 2u, "absent first candidate did not select second slot")) {
        return false;
    }

    candidates[0] = make_candidate(true, 0x00000400u, 0u, make_bounds(0.0f, 0.0f, 0, 0, 0, 0));
    candidates[1] = eligible;
    if (!check_scan(1, ring, candidates, 2u, "blocked first candidate did not select second slot")) {
        return false;
    }

    candidates[0] = eligible;
    candidates[1] = eligible;
    return check_scan(0, ring, candidates, 2u, "first of two eligible candidates was not selected");
}

bool test_plane_manager_disable_and_enable_gate() {
    const RingPickupCandidate overlap =
        make_candidate(true, 0u, 0u, make_bounds(0.0f, 0.0f, 0, 0, 0, 0));
    std::array<RingPickupCandidate, 2u> candidates{};
    candidates[0] = overlap;
    candidates[1] = overlap;

    DamageRingPickupState plane_ring = make_ring(0.0f, 0.0f, 1u, 0u, 0x20u, true);
    candidates[0].plane_flags = 1u;
    candidates[1].plane_flags = 0u;
    if (!check_scan(1, plane_ring, candidates, 2u, "plane filter did not select the matching second candidate")) {
        return false;
    }

    DamageRingPickupState disabled_manager = make_ring(0.0f, 0.0f, 1u, 0u, 0x80u, true);
    candidates[0] = overlap;
    candidates[1] = overlap;
    if (!check_scan(-1, disabled_manager, candidates, 1u, "disabled manager selected a pickup")) {
        return false;
    }

    DamageRingPickupState disabled_gate = make_ring(0.0f, 0.0f, 1u, 0u, 0u, false);
    return check_scan(-1, disabled_gate, candidates, 1u, "disabled pickup gate selected a pickup");
}

bool test_static_ring_collection() {
    const auto player = make_candidate(true, 0u, 0u, make_bounds(9.0f, -9.0f, 0, 0, 0, 0));
    const StaticRingPickupState ring{0.0f, 0.0f, 0u, 0u, true};
    const StaticRingPickupState disabled{0.0f, 0.0f, 0u, 0x80u, true};
    return find_static_ring_pickup(ring, &player, 1u) == 0 &&
           find_static_ring_pickup(disabled, &player, 1u) == -1 &&
           find_static_ring_pickup(ring, nullptr, 0u) == -1 &&
           find_damage_ring_pickup(make_ring(0.0f, 0.0f, 255u, 0u, 0u, true), &player, 1u) == -1;
}

bool test_ring_bounds_touching_fractional_and_inverted_cases() {
    RingPickupCandidate point_at_boundary =
        make_candidate(true, 0u, 0u, make_bounds(9.0f, -9.0f, 0, 0, 0, 0));
    RingPickupCandidate separated =
        make_candidate(true, 0u, 0u, make_bounds(10.0f, 0.0f, 0, 0, 0, 0));
    RingPickupCandidate fraction_at_boundary =
        make_candidate(true, 0u, 0u, make_bounds(9.9f, -0.9f, 0, 0, 0, 0));
    RingPickupCandidate fraction_separated =
        make_candidate(true, 0u, 0u, make_bounds(10.1f, -0.9f, 0, 0, 0, 0));
    RingPickupCandidate inverted =
        make_candidate(true, 0u, 0u, make_bounds(0.0f, 0.0f, 5, 0, -5, 0));
    std::array<RingPickupCandidate, 2u> candidates{};

    DamageRingPickupState ring = make_ring(0.0f, 0.0f, 1u, 0u, 0u, true);
    candidates[0] = point_at_boundary;
    if (!check_scan(0, ring, candidates, 1u, "ring +9/-9 inclusive boundary did not select pickup")) {
        return false;
    }

    candidates[0] = separated;
    if (!check_scan(-1, ring, candidates, 1u, "ring boundary separation selected pickup")) {
        return false;
    }

    ring = make_ring(-0.9f, 0.9f, 1u, 0u, 0u, true);
    candidates[0] = fraction_at_boundary;
    if (!check_scan(0, ring, candidates, 1u, "signed fractional pickup bounds did not truncate toward zero")) {
        return false;
    }

    candidates[0] = fraction_separated;
    if (!check_scan(-1, ring, candidates, 1u, "fractional pickup bounds were rounded into overlap")) {
        return false;
    }

    ring = make_ring(0.0f, 0.0f, 1u, 0u, 0u, true);
    candidates[0] = inverted;
    return check_scan(0, ring, candidates, 1u, "inverted candidate start inside ring bounds was normalized away");
}

}

int main() {
    return test_permission_paths_and_mask_bits() &&
                   test_timer_delay_and_wrapped_values() &&
                   test_empty_count_allows_null_candidates() &&
                   test_first_eligible_candidate_selection() &&
                   test_plane_manager_disable_and_enable_gate() &&
                   test_ring_bounds_touching_fractional_and_inverted_cases() &&
                   test_static_ring_collection()
               ? 0
               : 1;
}
