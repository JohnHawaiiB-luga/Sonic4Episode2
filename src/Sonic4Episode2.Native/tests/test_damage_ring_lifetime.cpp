#include "damage_rings.h"

#include <array>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <limits>

namespace {

bool check(bool condition, const char* message) {
    if (condition) {
        return true;
    }
    std::fprintf(stderr, "%s\n", message);
    return false;
}

float float_from_bits(std::uint32_t bits) {
    float value = 0.0f;
    std::memcpy(&value, &bits, sizeof(value));
    return value;
}

std::uint32_t float_bits(float value) {
    std::uint32_t bits = 0u;
    std::memcpy(&bits, &value, sizeof(bits));
    return bits;
}

void initialize_ring(DamageRing& ring, std::uint32_t marker) {
    for (std::size_t index = 0u; index < 3u; ++index) {
        ring.position[index] = float_from_bits(0x3F000000u + marker * 0x100u + static_cast<std::uint32_t>(index));
        ring.scale[index] = float_from_bits(0x40000000u + marker * 0x100u + static_cast<std::uint32_t>(index));
    }
    for (std::size_t index = 0u; index < 2u; ++index) {
        ring.velocity[index] = float_from_bits(0x40400000u + marker * 0x100u + static_cast<std::uint32_t>(index));
    }
    ring.timer = static_cast<std::uint16_t>(0x1200u + marker);
    ring.parameter_a = static_cast<std::uint16_t>(0x2200u + marker);
    ring.parameter_b = static_cast<std::uint16_t>(0x3200u + marker);
    ring.auxiliary_a = 0xA0000000u + marker;
    ring.auxiliary_b = 0xB0000000u + marker;
    for (std::size_t index = 0u; index < 4u; ++index) {
        ring.modulation[index] = float_from_bits(0x40800000u + marker * 0x100u + static_cast<std::uint32_t>(index));
    }
    ring.previous = nullptr;
    ring.next = nullptr;
}

struct RingSnapshot {
    std::array<std::uint32_t, 3u> position;
    std::array<std::uint32_t, 2u> velocity;
    std::array<std::uint32_t, 3u> scale;
    std::uint16_t timer;
    std::uint16_t parameter_a;
    std::uint16_t parameter_b;
    std::uint32_t auxiliary_a;
    std::uint32_t auxiliary_b;
    std::array<std::uint32_t, 4u> modulation;
    DamageRing* previous;
    DamageRing* next;
};

RingSnapshot snapshot_ring(const DamageRing& ring) {
    RingSnapshot snapshot{};
    for (std::size_t index = 0u; index < 3u; ++index) {
        snapshot.position[index] = float_bits(ring.position[index]);
        snapshot.scale[index] = float_bits(ring.scale[index]);
    }
    for (std::size_t index = 0u; index < 2u; ++index) {
        snapshot.velocity[index] = float_bits(ring.velocity[index]);
    }
    snapshot.timer = ring.timer;
    snapshot.parameter_a = ring.parameter_a;
    snapshot.parameter_b = ring.parameter_b;
    snapshot.auxiliary_a = ring.auxiliary_a;
    snapshot.auxiliary_b = ring.auxiliary_b;
    for (std::size_t index = 0u; index < 4u; ++index) {
        snapshot.modulation[index] = float_bits(ring.modulation[index]);
    }
    snapshot.previous = ring.previous;
    snapshot.next = ring.next;
    return snapshot;
}

bool same_ring(const RingSnapshot& expected, const DamageRing& actual) {
    const RingSnapshot current = snapshot_ring(actual);
    return expected.position == current.position &&
           expected.velocity == current.velocity &&
           expected.scale == current.scale &&
           expected.timer == current.timer &&
           expected.parameter_a == current.parameter_a &&
           expected.parameter_b == current.parameter_b &&
           expected.auxiliary_a == current.auxiliary_a &&
           expected.auxiliary_b == current.auxiliary_b &&
           expected.modulation == current.modulation &&
           expected.previous == current.previous &&
           expected.next == current.next;
}

using SlotSnapshot = std::array<DamageRing*, kDamageRingSlotCount>;

SlotSnapshot snapshot_slots(const DamageRingSystem& system) {
    SlotSnapshot snapshot{};
    for (std::size_t index = 0u; index < kDamageRingSlotCount; ++index) {
        snapshot[index] = system.slots[index];
    }
    return snapshot;
}

bool same_slots(const SlotSnapshot& expected, const DamageRingSystem& actual) {
    return expected == snapshot_slots(actual);
}

void fill_slots(DamageRingSystem& system, DamageRing* value) {
    for (DamageRing*& slot : system.slots) {
        slot = value;
    }
}

void link_three(DamageRing& head, DamageRing& middle, DamageRing& tail) {
    head.previous = nullptr;
    head.next = &middle;
    middle.previous = &head;
    middle.next = &tail;
    tail.previous = &middle;
    tail.next = nullptr;
}

bool test_zero_timer_wraps_without_unlinking() {
    DamageRing ring{};
    DamageRing guard{};
    initialize_ring(ring, 1u);
    initialize_ring(guard, 2u);
    ring.timer = 0u;

    DamageRingSystem system{};
    fill_slots(system, &guard);
    system.slots[0] = &ring;
    system.allocation_cursor = 1u;
    system.damage_head = &ring;
    system.damage_tail = &ring;

    RingSnapshot expected_ring = snapshot_ring(ring);
    expected_ring.timer = std::numeric_limits<std::uint16_t>::max();
    const SlotSnapshot expected_slots = snapshot_slots(system);

    return check(!tick_damage_ring_lifetime(system, ring), "zero timer unexpectedly expired") &&
           check(same_ring(expected_ring, ring), "zero timer changed fields besides its wrapped lifetime") &&
           check(system.allocation_cursor == 1u, "zero timer changed allocation cursor") &&
           check(system.damage_head == &ring && system.damage_tail == &ring, "zero timer changed list endpoints") &&
           check(same_slots(expected_slots, system), "zero timer changed pool slots");
}

bool test_positive_timers_decrement_without_unlinking() {
    const std::array<std::uint16_t, 2u> timers = {
        2u,
        std::numeric_limits<std::uint16_t>::max(),
    };

    for (const std::uint16_t timer : timers) {
        DamageRing head{};
        DamageRing middle{};
        DamageRing tail{};
        DamageRing guard{};
        initialize_ring(head, 3u);
        initialize_ring(middle, 4u);
        initialize_ring(tail, 5u);
        initialize_ring(guard, 6u);
        link_three(head, middle, tail);
        middle.timer = timer;

        DamageRingSystem system{};
        fill_slots(system, &guard);
        system.slots[0] = &head;
        system.slots[1] = &middle;
        system.slots[2] = &tail;
        system.allocation_cursor = 3u;
        system.damage_head = &head;
        system.damage_tail = &tail;

        const RingSnapshot expected_head = snapshot_ring(head);
        RingSnapshot expected_middle = snapshot_ring(middle);
        expected_middle.timer = static_cast<std::uint16_t>(timer - 1u);
        const RingSnapshot expected_tail = snapshot_ring(tail);
        const SlotSnapshot expected_slots = snapshot_slots(system);

        if (!check(!tick_damage_ring_lifetime(system, middle), "positive timer unexpectedly expired") ||
            !check(same_ring(expected_head, head), "positive timer changed the predecessor") ||
            !check(same_ring(expected_middle, middle), "positive timer changed fields besides the lifetime") ||
            !check(same_ring(expected_tail, tail), "positive timer changed the successor") ||
            !check(system.allocation_cursor == 3u, "positive timer changed allocation cursor") ||
            !check(system.damage_head == &head && system.damage_tail == &tail, "positive timer changed list endpoints") ||
            !check(same_slots(expected_slots, system), "positive timer changed pool slots")) {
            return false;
        }
    }
    return true;
}

bool test_singleton_expiry_unlinks_the_only_ring() {
    DamageRing ring{};
    DamageRing guard{};
    initialize_ring(ring, 7u);
    initialize_ring(guard, 8u);
    ring.timer = 1u;

    DamageRingSystem system{};
    fill_slots(system, &guard);
    system.slots[0] = &ring;
    system.allocation_cursor = 1u;
    system.damage_head = &ring;
    system.damage_tail = &ring;

    RingSnapshot expected_ring = snapshot_ring(ring);
    expected_ring.timer = 0u;
    const SlotSnapshot expected_slots = snapshot_slots(system);

    return check(tick_damage_ring_lifetime(system, ring), "singleton timer did not expire") &&
           check(same_ring(expected_ring, ring), "singleton expiry reset ring fields or links") &&
           check(system.allocation_cursor == 0u, "singleton expiry did not release allocation cursor") &&
           check(system.damage_head == nullptr && system.damage_tail == nullptr, "singleton expiry left a list endpoint") &&
           check(same_slots(expected_slots, system), "singleton expiry changed unrelated pool slots");
}

bool test_head_expiry_updates_head_and_preserves_stale_links() {
    DamageRing head{};
    DamageRing middle{};
    DamageRing tail{};
    DamageRing guard{};
    initialize_ring(head, 9u);
    initialize_ring(middle, 10u);
    initialize_ring(tail, 11u);
    initialize_ring(guard, 12u);
    link_three(head, middle, tail);
    head.timer = 1u;

    DamageRingSystem system{};
    fill_slots(system, &guard);
    system.slots[0] = &head;
    system.slots[1] = &middle;
    system.slots[2] = &tail;
    system.allocation_cursor = 3u;
    system.damage_head = &head;
    system.damage_tail = &tail;

    RingSnapshot expected_head = snapshot_ring(head);
    expected_head.timer = 0u;
    RingSnapshot expected_middle = snapshot_ring(middle);
    expected_middle.previous = nullptr;
    const RingSnapshot expected_tail = snapshot_ring(tail);
    SlotSnapshot expected_slots = snapshot_slots(system);
    expected_slots[2] = &head;

    return check(tick_damage_ring_lifetime(system, head), "head timer did not expire") &&
           check(same_ring(expected_head, head), "head expiry reset its stale links or fields") &&
           check(same_ring(expected_middle, middle), "head expiry did not relink the successor") &&
           check(same_ring(expected_tail, tail), "head expiry changed the tail") &&
           check(system.allocation_cursor == 2u, "head expiry did not release allocation cursor") &&
           check(system.damage_head == &middle && system.damage_tail == &tail, "head expiry endpoints mismatch") &&
           check(same_slots(expected_slots, system), "head expiry pool slot mismatch");
}

bool test_middle_expiry_relinks_both_neighbors() {
    DamageRing head{};
    DamageRing middle{};
    DamageRing tail{};
    DamageRing guard{};
    initialize_ring(head, 13u);
    initialize_ring(middle, 14u);
    initialize_ring(tail, 15u);
    initialize_ring(guard, 16u);
    link_three(head, middle, tail);
    middle.timer = 1u;

    DamageRingSystem system{};
    fill_slots(system, &guard);
    system.slots[0] = &head;
    system.slots[1] = &middle;
    system.slots[2] = &tail;
    system.allocation_cursor = 3u;
    system.damage_head = &head;
    system.damage_tail = &tail;

    RingSnapshot expected_head = snapshot_ring(head);
    expected_head.next = &tail;
    RingSnapshot expected_middle = snapshot_ring(middle);
    expected_middle.timer = 0u;
    RingSnapshot expected_tail = snapshot_ring(tail);
    expected_tail.previous = &head;
    SlotSnapshot expected_slots = snapshot_slots(system);
    expected_slots[2] = &middle;

    return check(tick_damage_ring_lifetime(system, middle), "middle timer did not expire") &&
           check(same_ring(expected_head, head), "middle expiry did not relink predecessor") &&
           check(same_ring(expected_middle, middle), "middle expiry reset stale links or fields") &&
           check(same_ring(expected_tail, tail), "middle expiry did not relink successor") &&
           check(system.allocation_cursor == 2u, "middle expiry did not release allocation cursor") &&
           check(system.damage_head == &head && system.damage_tail == &tail, "middle expiry endpoints mismatch") &&
           check(same_slots(expected_slots, system), "middle expiry pool slot mismatch");
}

bool test_tail_expiry_updates_tail_and_preserves_stale_links() {
    DamageRing head{};
    DamageRing middle{};
    DamageRing tail{};
    DamageRing guard{};
    initialize_ring(head, 17u);
    initialize_ring(middle, 18u);
    initialize_ring(tail, 19u);
    initialize_ring(guard, 20u);
    link_three(head, middle, tail);
    tail.timer = 1u;

    DamageRingSystem system{};
    fill_slots(system, &guard);
    system.slots[0] = &head;
    system.slots[1] = &middle;
    system.slots[2] = &tail;
    system.allocation_cursor = 3u;
    system.damage_head = &head;
    system.damage_tail = &tail;

    const RingSnapshot expected_head = snapshot_ring(head);
    RingSnapshot expected_middle = snapshot_ring(middle);
    expected_middle.next = nullptr;
    RingSnapshot expected_tail = snapshot_ring(tail);
    expected_tail.timer = 0u;
    SlotSnapshot expected_slots = snapshot_slots(system);
    expected_slots[2] = &tail;

    return check(tick_damage_ring_lifetime(system, tail), "tail timer did not expire") &&
           check(same_ring(expected_head, head), "tail expiry changed the head") &&
           check(same_ring(expected_middle, middle), "tail expiry did not relink predecessor") &&
           check(same_ring(expected_tail, tail), "tail expiry reset stale links or fields") &&
           check(system.allocation_cursor == 2u, "tail expiry did not release allocation cursor") &&
           check(system.damage_head == &head && system.damage_tail == &middle, "tail expiry endpoints mismatch") &&
           check(same_slots(expected_slots, system), "tail expiry pool slot mismatch");
}

bool test_full_pool_expiry_recycles_at_last_slot() {
    std::array<DamageRing, kDamageRingSlotCount> rings{};
    DamageRingSystem system{};
    std::array<RingSnapshot, kDamageRingSlotCount> expected_rings{};

    for (std::size_t index = 0u; index < kDamageRingSlotCount; ++index) {
        initialize_ring(rings[index], static_cast<std::uint32_t>(21u + index));
        rings[index].previous = index == 0u ? nullptr : &rings[index - 1u];
        rings[index].next = index + 1u == kDamageRingSlotCount ? nullptr : &rings[index + 1u];
        system.slots[index] = &rings[index];
        expected_rings[index] = snapshot_ring(rings[index]);
    }

    constexpr std::size_t expired_index = kDamageRingSlotCount / 2u;
    rings[expired_index].timer = 1u;
    expected_rings[expired_index].timer = 0u;
    expected_rings[expired_index - 1u].next = &rings[expired_index + 1u];
    expected_rings[expired_index + 1u].previous = &rings[expired_index - 1u];
    system.allocation_cursor = static_cast<std::uint32_t>(kDamageRingSlotCount);
    system.damage_head = &rings.front();
    system.damage_tail = &rings.back();
    SlotSnapshot expected_slots = snapshot_slots(system);
    expected_slots[kDamageRingSlotCount - 1u] = &rings[expired_index];

    if (!check(tick_damage_ring_lifetime(system, rings[expired_index]), "full-pool timer did not expire") ||
        !check(system.allocation_cursor == kDamageRingSlotCount - 1u, "full-pool expiry cursor mismatch") ||
        !check(system.damage_head == &rings.front() && system.damage_tail == &rings.back(), "full-pool expiry endpoints mismatch") ||
        !check(same_slots(expected_slots, system), "full-pool expiry pool slots mismatch")) {
        return false;
    }

    for (std::size_t index = 0u; index < kDamageRingSlotCount; ++index) {
        if (!check(same_ring(expected_rings[index], rings[index]), "full-pool expiry changed an unexpected ring field")) {
            return false;
        }
    }
    return true;
}

bool test_consecutive_expiry_recycles_in_lifo_order() {
    DamageRing first{};
    DamageRing second{};
    DamageRing guard{};
    initialize_ring(first, 117u);
    initialize_ring(second, 118u);
    initialize_ring(guard, 119u);
    first.timer = 1u;
    second.timer = 1u;
    first.previous = nullptr;
    first.next = &second;
    second.previous = &first;
    second.next = nullptr;

    DamageRingSystem system{};
    fill_slots(system, &guard);
    system.slots[0] = &first;
    system.slots[1] = &second;
    system.allocation_cursor = 2u;
    system.damage_head = &first;
    system.damage_tail = &second;

    RingSnapshot expected_first = snapshot_ring(first);
    expected_first.timer = 0u;
    RingSnapshot expected_second_after_first = snapshot_ring(second);
    expected_second_after_first.previous = nullptr;
    SlotSnapshot expected_slots_after_first = snapshot_slots(system);
    expected_slots_after_first[1] = &first;

    if (!check(tick_damage_ring_lifetime(system, first), "first consecutive timer did not expire") ||
        !check(same_ring(expected_first, first), "first consecutive expiry reset stale links or fields") ||
        !check(same_ring(expected_second_after_first, second), "first consecutive expiry did not update the second ring") ||
        !check(system.allocation_cursor == 1u, "first consecutive expiry cursor mismatch") ||
        !check(system.damage_head == &second && system.damage_tail == &second, "first consecutive expiry endpoints mismatch") ||
        !check(same_slots(expected_slots_after_first, system), "first consecutive expiry pool slots mismatch")) {
        return false;
    }

    RingSnapshot expected_second = snapshot_ring(second);
    expected_second.timer = 0u;
    SlotSnapshot expected_slots_after_second = expected_slots_after_first;
    expected_slots_after_second[0] = &second;

    return check(tick_damage_ring_lifetime(system, second), "second consecutive timer did not expire") &&
           check(same_ring(expected_first, first), "second consecutive expiry changed first ring") &&
           check(same_ring(expected_second, second), "second consecutive expiry reset stale links or fields") &&
           check(system.allocation_cursor == 0u, "second consecutive expiry cursor mismatch") &&
           check(system.damage_head == nullptr && system.damage_tail == nullptr, "second consecutive expiry endpoints mismatch") &&
           check(same_slots(expected_slots_after_second, system), "second consecutive expiry pool slots mismatch");
}

bool test_expiry_reuses_ring_without_advancing_rng() {
    DamageRing first{};
    DamageRing recycled{};
    DamageRing guard{};
    initialize_ring(first, 120u);
    initialize_ring(recycled, 121u);
    initialize_ring(guard, 122u);

    DamageRingSystem system{};
    fill_slots(system, &guard);
    system.slots[0] = &first;
    system.slots[1] = &recycled;
    const DamageRingCreateArgs args{
        {1.0f, 2.0f, 3.0f},
        {4.0f, 5.0f},
        6u,
        7u,
    };
    std::uint32_t seed = 0u;

    if (!check(create_damage_ring(seed, &system, args) == &first, "first creation failed") ||
        !check(create_damage_ring(seed, &system, args) == &recycled, "second creation failed")) {
        return false;
    }

    recycled.timer = 1u;
    const std::uint32_t seed_before_tick = seed;
    RingSnapshot expected_first = snapshot_ring(first);
    expected_first.next = nullptr;
    RingSnapshot expected_recycled = snapshot_ring(recycled);
    expected_recycled.timer = 0u;

    if (!check(tick_damage_ring_lifetime(system, recycled), "reusable ring timer did not expire") ||
        !check(seed == seed_before_tick, "lifetime ticking advanced RNG state") ||
        !check(same_ring(expected_first, first), "recycling changed first ring fields") ||
        !check(same_ring(expected_recycled, recycled), "recycling reset stale links or fields") ||
        !check(system.allocation_cursor == 1u, "recycling cursor mismatch") ||
        !check(system.damage_head == &first && system.damage_tail == &first, "recycling endpoints mismatch") ||
        !check(system.slots[1] == &recycled, "recycling did not return ring to the next allocation slot")) {
        return false;
    }

    return check(create_damage_ring(seed, &system, args) == &recycled, "creation did not reuse recycled ring") &&
           check(seed != seed_before_tick, "creation after recycling did not advance RNG state") &&
           check(system.allocation_cursor == 2u, "reused creation cursor mismatch") &&
           check(system.damage_head == &first && system.damage_tail == &recycled, "reused creation endpoints mismatch") &&
           check(first.next == &recycled && recycled.previous == &first && recycled.next == nullptr,
                 "reused creation links mismatch");
}

}

int main() {
    return test_zero_timer_wraps_without_unlinking() &&
                   test_positive_timers_decrement_without_unlinking() &&
                   test_singleton_expiry_unlinks_the_only_ring() &&
                   test_head_expiry_updates_head_and_preserves_stale_links() &&
                   test_middle_expiry_relinks_both_neighbors() &&
                   test_tail_expiry_updates_tail_and_preserves_stale_links() &&
                   test_full_pool_expiry_recycles_at_last_slot() &&
                   test_consecutive_expiry_recycles_in_lifo_order() &&
                   test_expiry_reuses_ring_without_advancing_rng()
               ? 0
               : 1;
}
