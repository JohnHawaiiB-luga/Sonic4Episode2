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

void initialize_ring(DamageRing& ring, DamageRing* previous, DamageRing* next) {
    ring.position[0] = float_from_bits(0x3F800000u);
    ring.position[1] = float_from_bits(0x40000000u);
    ring.position[2] = float_from_bits(0xC0600000u);
    ring.velocity[0] = float_from_bits(0x3E800000u);
    ring.velocity[1] = float_from_bits(0x3F000000u);
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

RingSnapshot expected_move(const RingSnapshot& before, std::uint32_t x, std::uint32_t y) {
    RingSnapshot expected = before;
    expected.position[0] = x;
    expected.position[1] = y;
    return expected;
}

bool test_vertical_branches_update_only_xy() {
    DamageRing previous{};
    DamageRing next{};
    DamageRing addition{};
    DamageRing subtraction{};
    initialize_ring(addition, &previous, &next);
    initialize_ring(subtraction, &previous, &next);

    addition.position[0] = float_from_bits(0x3FC00000u);
    addition.position[1] = float_from_bits(0x40000000u);
    addition.position[2] = float_from_bits(0x422A0000u);
    addition.velocity[0] = float_from_bits(0x3E800000u);
    addition.velocity[1] = float_from_bits(0x3F000000u);
    addition.parameter_b = 0u;
    subtraction = addition;
    subtraction.parameter_b = 0x4u;

    const RingSnapshot addition_before = snapshot_ring(addition);
    const RingSnapshot subtraction_before = snapshot_ring(subtraction);
    advance_damage_ring_position(addition);
    advance_damage_ring_position(subtraction);

    return check(same_ring(expected_move(addition_before, 0x3FE00000u, 0x40200000u), addition),
                 "vertical addition changed unexpected fields or used the wrong result") &&
           check(same_ring(expected_move(subtraction_before, 0x3FE00000u, 0x3FC00000u), subtraction),
                 "vertical subtraction changed unexpected fields or used the wrong result") &&
           check(float_bits(addition.position[2]) == 0x422A0000u && float_bits(subtraction.position[2]) == 0x422A0000u,
                 "motion changed nonzero z position");
}

bool test_only_bit_four_selects_vertical_branch() {
    for (std::uint32_t raw_flags = 0u; raw_flags <= std::numeric_limits<std::uint16_t>::max(); ++raw_flags) {
        DamageRing previous{};
        DamageRing next{};
        DamageRing ring{};
        initialize_ring(ring, &previous, &next);
        ring.position[0] = float_from_bits(0x3FC00000u);
        ring.position[1] = float_from_bits(0x40000000u);
        ring.velocity[0] = float_from_bits(0x3E800000u);
        ring.velocity[1] = float_from_bits(0x3F000000u);
        ring.parameter_b = static_cast<std::uint16_t>(raw_flags);
        const RingSnapshot before = snapshot_ring(ring);
        const std::uint32_t expected_y = (raw_flags & 0x4u) == 0u ? 0x40200000u : 0x3FC00000u;

        advance_damage_ring_position(ring);

        if (!same_ring(expected_move(before, 0x3FE00000u, expected_y), ring)) {
            std::fprintf(stderr, "flag 0x%04X selected an incorrect motion result\n", raw_flags);
            return false;
        }
    }
    return true;
}

bool test_signed_zero_results_are_preserved() {
    DamageRing previous{};
    DamageRing next{};
    DamageRing addition{};
    DamageRing subtraction{};
    initialize_ring(addition, &previous, &next);
    initialize_ring(subtraction, &previous, &next);

    addition.position[0] = float_from_bits(0x00000000u);
    addition.velocity[0] = float_from_bits(0x80000000u);
    addition.position[1] = float_from_bits(0x80000000u);
    addition.velocity[1] = float_from_bits(0x80000000u);
    addition.parameter_b = 0u;
    subtraction.position[0] = float_from_bits(0x80000000u);
    subtraction.velocity[0] = float_from_bits(0x80000000u);
    subtraction.position[1] = float_from_bits(0x80000000u);
    subtraction.velocity[1] = float_from_bits(0x00000000u);
    subtraction.parameter_b = 0x4u;

    const RingSnapshot addition_before = snapshot_ring(addition);
    const RingSnapshot subtraction_before = snapshot_ring(subtraction);
    advance_damage_ring_position(addition);
    advance_damage_ring_position(subtraction);

    return check(same_ring(expected_move(addition_before, 0x00000000u, 0x80000000u), addition),
                 "addition branch lost signed-zero results") &&
           check(same_ring(expected_move(subtraction_before, 0x80000000u, 0x80000000u), subtraction),
                 "subtraction branch lost signed-zero results");
}

bool test_fractional_repeated_motion() {
    DamageRing previous{};
    DamageRing next{};
    DamageRing addition{};
    DamageRing subtraction{};
    initialize_ring(addition, &previous, &next);
    initialize_ring(subtraction, &previous, &next);

    addition.position[0] = float_from_bits(0x3E000000u);
    addition.position[1] = float_from_bits(0xBF000000u);
    addition.position[2] = float_from_bits(0x40D00000u);
    addition.velocity[0] = float_from_bits(0x3E800000u);
    addition.velocity[1] = float_from_bits(0x3E000000u);
    addition.parameter_b = 0u;
    subtraction = addition;
    subtraction.parameter_b = 0x4u;

    const RingSnapshot addition_before = snapshot_ring(addition);
    const RingSnapshot subtraction_before = snapshot_ring(subtraction);
    for (std::size_t step = 0u; step < 4u; ++step) {
        advance_damage_ring_position(addition);
        advance_damage_ring_position(subtraction);
    }

    return check(same_ring(expected_move(addition_before, 0x3F900000u, 0x00000000u), addition),
                 "fractional repeated addition result mismatch") &&
           check(same_ring(expected_move(subtraction_before, 0x3F900000u, 0xBF800000u), subtraction),
                 "fractional repeated subtraction result mismatch");
}

bool test_subnormal_motion_preserves_gradual_underflow() {
    DamageRing previous{};
    DamageRing next{};
    DamageRing addition{};
    DamageRing subtraction{};
    initialize_ring(addition, &previous, &next);
    initialize_ring(subtraction, &previous, &next);

    addition.position[0] = float_from_bits(0x00000001u);
    addition.position[1] = float_from_bits(0x00000001u);
    addition.velocity[0] = float_from_bits(0x00000001u);
    addition.velocity[1] = float_from_bits(0x00000001u);
    addition.parameter_b = 0u;
    subtraction.position[0] = float_from_bits(0x00000002u);
    subtraction.position[1] = float_from_bits(0x00000002u);
    subtraction.velocity[0] = float_from_bits(0x00000001u);
    subtraction.velocity[1] = float_from_bits(0x00000001u);
    subtraction.parameter_b = 0x4u;

    const RingSnapshot addition_before = snapshot_ring(addition);
    const RingSnapshot subtraction_before = snapshot_ring(subtraction);
    advance_damage_ring_position(addition);
    advance_damage_ring_position(subtraction);

    return check(same_ring(expected_move(addition_before, 0x00000002u, 0x00000002u), addition),
                 "subnormal addition did not preserve gradual underflow") &&
           check(same_ring(expected_move(subtraction_before, 0x00000003u, 0x00000001u), subtraction),
                 "subnormal subtraction did not preserve gradual underflow");
}

}

int main() {
    return test_vertical_branches_update_only_xy() &&
                   test_only_bit_four_selects_vertical_branch() &&
                   test_signed_zero_results_are_preserved() &&
                   test_fractional_repeated_motion() &&
                   test_subnormal_motion_preserves_gradual_underflow()
               ? 0
               : 1;
}
