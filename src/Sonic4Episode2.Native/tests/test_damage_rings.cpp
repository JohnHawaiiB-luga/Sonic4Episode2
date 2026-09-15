#include "damage_rings.h"

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <type_traits>

static_assert(std::is_same_v<decltype(DamageRing::timer), std::uint16_t>);

namespace {

constexpr std::uint32_t kSentinelBits = 0xCDCDCDCDu;

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

void initialize_ring(DamageRing& ring) {
    for (float& value : ring.position) {
        value = float_from_bits(kSentinelBits);
    }
    for (float& value : ring.velocity) {
        value = float_from_bits(kSentinelBits);
    }
    for (float& value : ring.scale) {
        value = float_from_bits(kSentinelBits);
    }
    ring.timer = static_cast<std::uint16_t>(kSentinelBits);
    ring.parameter_a = 0xCDCDu;
    ring.parameter_b = 0xCDCDu;
    ring.auxiliary_a = kSentinelBits;
    ring.auxiliary_b = kSentinelBits;
    for (float& value : ring.modulation) {
        value = float_from_bits(kSentinelBits);
    }
    ring.previous = nullptr;
    ring.next = nullptr;
}

struct RingFields {
    std::uint32_t position[3];
    std::uint32_t velocity[2];
    std::uint32_t scale[3];
    std::uint16_t timer;
    std::uint16_t parameter_a;
    std::uint16_t parameter_b;
    std::uint32_t auxiliary_a;
    std::uint32_t auxiliary_b;
    std::uint32_t modulation[4];
    DamageRing* previous;
};

RingFields snapshot_fields(const DamageRing& ring) {
    RingFields snapshot{};
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
    return snapshot;
}

bool same_fields(const RingFields& expected, const DamageRing& actual) {
    for (std::size_t index = 0u; index < 3u; ++index) {
        if (expected.position[index] != float_bits(actual.position[index]) ||
            expected.scale[index] != float_bits(actual.scale[index])) {
            return false;
        }
    }
    for (std::size_t index = 0u; index < 2u; ++index) {
        if (expected.velocity[index] != float_bits(actual.velocity[index])) {
            return false;
        }
    }
    if (expected.timer != actual.timer ||
        expected.parameter_a != actual.parameter_a ||
        expected.parameter_b != actual.parameter_b ||
        expected.auxiliary_a != actual.auxiliary_a ||
        expected.auxiliary_b != actual.auxiliary_b ||
        expected.previous != actual.previous) {
        return false;
    }
    for (std::size_t index = 0u; index < 4u; ++index) {
        if (expected.modulation[index] != float_bits(actual.modulation[index])) {
            return false;
        }
    }
    return true;
}

DamageRingCreateArgs make_args() {
    DamageRingCreateArgs args{};
    args.position[0] = float_from_bits(0x3F800000u);
    args.position[1] = float_from_bits(0xC0000000u);
    args.position[2] = float_from_bits(0x40400000u);
    args.velocity[0] = float_from_bits(0x3E800000u);
    args.velocity[1] = float_from_bits(0xBF000000u);
    args.parameter_a = 1234u;
    args.parameter_b = 5678u;
    return args;
}

bool test_null_system() {
    std::uint32_t seed = 0x13572468u;
    const DamageRingCreateArgs args = make_args();
    return check(create_damage_ring(seed, nullptr, args) == nullptr, "null system returned a ring") &&
           check(seed == 0x13572468u, "null system advanced RNG state");
}

bool test_full_pool() {
    DamageRing ring{};
    initialize_ring(ring);
    DamageRingSystem system{};
    system.allocation_cursor = 96u;
    system.damage_head = &ring;
    system.damage_tail = &ring;
    const RingFields before = snapshot_fields(ring);
    std::uint32_t seed = 0x24681357u;
    const DamageRingCreateArgs args = make_args();
    return check(create_damage_ring(seed, &system, args) == nullptr, "full pool returned a ring") &&
           check(seed == 0x24681357u, "full pool advanced RNG state") &&
           check(system.allocation_cursor == 96u, "full pool changed cursor") &&
           check(system.damage_head == &ring && system.damage_tail == &ring, "full pool changed list endpoints") &&
           check(same_fields(before, ring) && ring.next == nullptr, "full pool changed ring state");
}

bool test_null_slot_consumes_cursor() {
    DamageRing existing{};
    initialize_ring(existing);
    DamageRingSystem system{};
    system.slots[0] = nullptr;
    system.damage_head = &existing;
    system.damage_tail = &existing;
    const RingFields before = snapshot_fields(existing);
    std::uint32_t seed = 0x10203040u;
    const DamageRingCreateArgs args = make_args();
    return check(create_damage_ring(seed, &system, args) == nullptr, "null slot returned a ring") &&
           check(seed == 0x10203040u, "null slot advanced RNG state") &&
           check(system.allocation_cursor == 1u, "null slot did not consume cursor") &&
           check(system.damage_head == &existing && system.damage_tail == &existing, "null slot changed list endpoints") &&
           check(same_fields(before, existing) && existing.next == nullptr, "null slot changed existing ring");
}

bool test_empty_append() {
    DamageRing ring{};
    initialize_ring(ring);
    DamageRingSystem system{};
    system.slots[0] = &ring;
    std::uint32_t seed = 0u;
    const DamageRingCreateArgs args = make_args();
    const DamageRing* returned = create_damage_ring(seed, &system, args);
    return check(returned == &ring, "empty append did not return selected ring") &&
           check(seed == 1013904223u, "empty append RNG state mismatch") &&
           check(system.allocation_cursor == 1u, "empty append cursor mismatch") &&
           check(system.damage_head == &ring && system.damage_tail == &ring, "empty append list endpoints mismatch") &&
           check(ring.previous == nullptr && ring.next == nullptr, "empty append links mismatch") &&
           check(float_bits(ring.position[0]) == 0x3F800000u &&
                     float_bits(ring.position[1]) == 0xC0000000u &&
                     float_bits(ring.position[2]) == 0x40400000u,
                 "empty append position mismatch") &&
           check(float_bits(ring.velocity[0]) == 0x3E800000u && float_bits(ring.velocity[1]) == 0xBF000000u,
                 "empty append velocity mismatch") &&
           check(float_bits(ring.scale[0]) == 0x3F800000u &&
                     float_bits(ring.scale[1]) == 0x3F800000u &&
                     float_bits(ring.scale[2]) == 0x3F800000u,
                 "empty append scale mismatch") &&
           check(ring.timer == 270u, "empty append timer mismatch") &&
           check(ring.parameter_a == 1234u && ring.parameter_b == 5678u, "empty append parameters mismatch") &&
           check(ring.auxiliary_a == 0u && ring.auxiliary_b == 0u, "empty append auxiliary fields mismatch") &&
           check(float_bits(ring.modulation[0]) == 0x3F800000u &&
                     float_bits(ring.modulation[1]) == 0x3F800000u &&
                     float_bits(ring.modulation[2]) == 0x3F800000u &&
                     float_bits(ring.modulation[3]) == 0x3F800000u,
                 "empty append modulation mismatch");
}

bool test_nonempty_append() {
    DamageRing existing{};
    DamageRing ring{};
    initialize_ring(existing);
    initialize_ring(ring);
    DamageRingSystem system{};
    system.slots[0] = &existing;
    system.slots[1] = &ring;
    system.allocation_cursor = 1u;
    system.damage_head = &existing;
    system.damage_tail = &existing;
    const RingFields before = snapshot_fields(existing);
    std::uint32_t seed = 1u;
    const DamageRingCreateArgs args = make_args();
    const DamageRing* returned = create_damage_ring(seed, &system, args);
    return check(returned == &ring, "nonempty append did not return selected ring") &&
           check(seed == 1015567748u, "nonempty append RNG state mismatch") &&
           check(system.allocation_cursor == 2u, "nonempty append cursor mismatch") &&
           check(system.damage_head == &existing && system.damage_tail == &ring, "nonempty append list endpoints mismatch") &&
           check(same_fields(before, existing), "nonempty append changed existing fields") &&
           check(existing.previous == nullptr && existing.next == &ring, "nonempty append predecessor links mismatch") &&
           check(ring.previous == &existing && ring.next == nullptr, "nonempty append new links mismatch") &&
           check(ring.timer == 264u, "nonempty append timer mismatch");
}

}

int main() {
    return test_null_system() &&
                   test_full_pool() &&
                   test_null_slot_consumes_cursor() &&
                   test_empty_append() &&
                   test_nonempty_append()
               ? 0
               : 1;
}
