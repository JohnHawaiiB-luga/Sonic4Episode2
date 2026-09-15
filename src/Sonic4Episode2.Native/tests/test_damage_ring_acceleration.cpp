#include "damage_rings.h"

#include <array>
#include <cstdint>
#include <cstdio>
#include <cstring>

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

void initialize_ring(DamageRing& ring, DamageRing* previous, DamageRing* next) {
    ring.position[0] = float_from_bits(0x3F800000u);
    ring.position[1] = float_from_bits(0x40000000u);
    ring.position[2] = float_from_bits(0xC0600000u);
    ring.velocity[0] = float_from_bits(0x3F000000u);
    ring.velocity[1] = float_from_bits(0xBF000000u);
    ring.scale[0] = float_from_bits(0x3F000000u);
    ring.scale[1] = float_from_bits(0x40000000u);
    ring.scale[2] = float_from_bits(0x40400000u);
    ring.timer = 0x1234u;
    ring.parameter_a = 0x5678u;
    ring.parameter_b = 0x9ABCu;
    ring.auxiliary_a = 0x10203040u;
    ring.auxiliary_b = 0x50607080u;
    ring.modulation[0] = float_from_bits(0x3F800000u);
    ring.modulation[1] = float_from_bits(0x40000000u);
    ring.modulation[2] = float_from_bits(0x40400000u);
    ring.modulation[3] = float_from_bits(0x40800000u);
    ring.previous = previous;
    ring.next = next;
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

RingSnapshot expected_velocity(const RingSnapshot& before, std::uint32_t horizontal, std::uint32_t vertical) {
    RingSnapshot expected = before;
    expected.velocity[0] = horizontal;
    expected.velocity[1] = vertical;
    return expected;
}

bool same_acceleration(const DamageRingAcceleration& acceleration, std::uint32_t horizontal, std::uint32_t vertical) {
    return float_bits(acceleration.horizontal) == horizontal && float_bits(acceleration.vertical) == vertical;
}

bool test_default_cached_deltas_apply_without_recalculation() {
    DamageRing previous{};
    DamageRing next{};
    DamageRing ring{};
    initialize_ring(ring, &previous, &next);
    ring.velocity[0] = float_from_bits(0x00000000u);
    ring.velocity[1] = float_from_bits(0x00000000u);
    const RingSnapshot before = snapshot_ring(ring);
    DamageRingAcceleration acceleration{};

    accelerate_damage_ring(ring, acceleration, 0x1234u, true);

    return check(same_acceleration(acceleration, 0x00000000u, 0x3D900000u), "default acceleration cache mismatch") &&
           check(same_ring(expected_velocity(before, 0x00000000u, 0x3D900000u), ring),
                 "default cached acceleration changed unrelated ring fields");
}

bool test_axis_deltas_and_velocity_updates() {
    struct AxisCase {
        std::uint16_t angle;
        std::uint32_t horizontal;
        std::uint32_t vertical;
        std::uint32_t velocity_x;
        std::uint32_t velocity_y;
    };

    const std::array<AxisCase, 4u> cases = {{
        {0u, 0x80000000u, 0x3D900000u, 0x00000000u, 0x3D900000u},
        {16384u, 0xBD900000u, 0x80000000u, 0xBD900000u, 0x00000000u},
        {32768u, 0x00000000u, 0xBD900000u, 0x00000000u, 0xBD900000u},
        {49152u, 0x3D900000u, 0x00000000u, 0x3D900000u, 0x00000000u},
    }};

    for (const AxisCase& expected : cases) {
        DamageRing previous{};
        DamageRing next{};
        DamageRing ring{};
        initialize_ring(ring, &previous, &next);
        ring.velocity[0] = float_from_bits(0x00000000u);
        ring.velocity[1] = float_from_bits(0x00000000u);
        const RingSnapshot before = snapshot_ring(ring);
        DamageRingAcceleration acceleration{5.0f, -6.0f};

        accelerate_damage_ring(ring, acceleration, expected.angle, false);

        if (!check(same_acceleration(acceleration, expected.horizontal, expected.vertical), "axis acceleration cache mismatch") ||
            !check(same_ring(expected_velocity(before, expected.velocity_x, expected.velocity_y), ring),
                   "axis acceleration changed unrelated ring fields")) {
            return false;
        }
    }
    return true;
}

bool test_shared_cache_reuse_ignores_new_angle() {
    DamageRing previous{};
    DamageRing next{};
    DamageRing first{};
    DamageRing second{};
    initialize_ring(first, &previous, &next);
    initialize_ring(second, &previous, &next);
    first.velocity[0] = float_from_bits(0x00000000u);
    first.velocity[1] = float_from_bits(0x00000000u);
    second.velocity[0] = float_from_bits(0x3D900000u);
    second.velocity[1] = float_from_bits(0x3D900000u);
    const RingSnapshot first_before = snapshot_ring(first);
    const RingSnapshot second_before = snapshot_ring(second);
    DamageRingAcceleration acceleration{};

    accelerate_damage_ring(first, acceleration, 16384u, false);
    if (!check(same_acceleration(acceleration, 0xBD900000u, 0x80000000u), "initial shared cache mismatch") ||
        !check(same_ring(expected_velocity(first_before, 0xBD900000u, 0x00000000u), first),
               "initial shared acceleration changed unrelated fields")) {
        return false;
    }

    accelerate_damage_ring(second, acceleration, 49152u, true);

    return check(same_acceleration(acceleration, 0xBD900000u, 0x80000000u), "reuse recalculated shared cache") &&
           check(same_ring(expected_velocity(second_before, 0x00000000u, 0x3D900000u), second),
                 "reuse did not apply cached deltas in velocity-plus-delta order");
}

bool test_signed_zero_velocity_updates() {
    DamageRing previous{};
    DamageRing next{};
    DamageRing signed_zero_ring{};
    initialize_ring(signed_zero_ring, &previous, &next);
    signed_zero_ring.velocity[0] = float_from_bits(0x80000000u);
    signed_zero_ring.velocity[1] = float_from_bits(0x00000000u);
    const RingSnapshot signed_zero_before = snapshot_ring(signed_zero_ring);
    DamageRingAcceleration signed_zero_acceleration{};

    accelerate_damage_ring(signed_zero_ring, signed_zero_acceleration, 32768u, false);

    return check(same_acceleration(signed_zero_acceleration, 0x00000000u, 0xBD900000u), "signed-zero cache mismatch") &&
           check(same_ring(expected_velocity(signed_zero_before, 0x00000000u, 0xBD900000u), signed_zero_ring),
                 "signed-zero velocity addition mismatch");
}

bool test_repeated_cached_updates() {
    DamageRing previous{};
    DamageRing next{};
    DamageRing repeated_ring{};
    initialize_ring(repeated_ring, &previous, &next);
    repeated_ring.velocity[0] = float_from_bits(0x00000000u);
    repeated_ring.velocity[1] = float_from_bits(0x00000000u);
    const RingSnapshot repeated_before = snapshot_ring(repeated_ring);
    DamageRingAcceleration repeated_acceleration{};

    for (std::size_t step = 0u; step < 4u; ++step) {
        accelerate_damage_ring(repeated_ring, repeated_acceleration, 16384u, step != 0u);
    }

    return check(same_acceleration(repeated_acceleration, 0xBD900000u, 0x80000000u),
                 "repeated acceleration cache mismatch") &&
           check(same_ring(expected_velocity(repeated_before, 0xBE900000u, 0x00000000u), repeated_ring),
                 "repeated acceleration result mismatch");
}

}

int main() {
    return test_default_cached_deltas_apply_without_recalculation() &&
                   test_axis_deltas_and_velocity_updates() &&
                   test_shared_cache_reuse_ignores_new_angle() &&
                   test_signed_zero_velocity_updates() &&
                   test_repeated_cached_updates()
               ? 0
               : 1;
}
