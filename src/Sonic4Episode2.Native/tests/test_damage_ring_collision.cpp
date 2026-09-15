#include "damage_ring_collision.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>

namespace {

constexpr std::size_t kMaximumQueries = 2u;

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
    ring.position[0] = float_from_bits(0x3FA00000u);
    ring.position[1] = float_from_bits(0xC0200000u);
    ring.position[2] = float_from_bits(0x422A0000u);
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

struct QueryLog {
    std::array<DamageRingCollisionProbe, kMaximumQueries> probes{};
    std::array<float, kMaximumQueries> responses{};
    std::size_t count = 0u;
};

float scripted_query(const DamageRingCollisionProbe& probe, void* context) {
    QueryLog& log = *static_cast<QueryLog*>(context);
    if (log.count >= log.probes.size()) {
        ++log.count;
        return 0.0f;
    }

    const std::size_t index = log.count++;
    log.probes[index] = probe;
    return log.responses[index];
}

struct ExpectedProbe {
    std::uint32_t x;
    std::uint32_t y;
    std::uint16_t flags;
    DamageRingCollisionDirection direction;
};

ExpectedProbe expected_probe(
    float x,
    float y,
    std::uint16_t flags,
    DamageRingCollisionDirection direction) {
    return {float_bits(x), float_bits(y), flags, direction};
}

template <std::size_t Count>
bool same_queries(const QueryLog& actual, const std::array<ExpectedProbe, Count>& expected) {
    if (actual.count != Count) {
        return false;
    }

    for (std::size_t index = 0u; index < Count; ++index) {
        const DamageRingCollisionProbe& actual_probe = actual.probes[index];
        if (float_bits(actual_probe.x) != expected[index].x ||
            float_bits(actual_probe.y) != expected[index].y ||
            actual_probe.flags != expected[index].flags ||
            actual_probe.direction != expected[index].direction) {
            return false;
        }
    }
    return true;
}

bool test_positive_y_then_x_hits_transition_flags() {
    DamageRing previous{};
    DamageRing next{};
    DamageRing ring{};
    initialize_ring(ring, &previous, &next);
    ring.position[0] = 10.0f;
    ring.position[1] = 20.0f;
    ring.velocity[0] = 4.0f;
    ring.velocity[1] = 8.0f;
    ring.parameter_b = 0xA082u;
    const RingSnapshot before = snapshot_ring(ring);
    QueryLog queries{};
    queries.responses = {{-2.0f, -3.0f}};

    resolve_damage_ring_collision(ring, scripted_query, &queries);

    RingSnapshot expected = before;
    expected.position[0] = float_bits(7.0f);
    expected.position[1] = float_bits(18.0f);
    expected.velocity[0] = float_bits(-3.0f);
    expected.velocity[1] = float_bits(-6.0f);
    expected.parameter_b = 0xA042u;
    const std::array<ExpectedProbe, 2u> expected_queries = {{
        expected_probe(10.0f, 29.0f, 1u, DamageRingCollisionDirection::PositiveY),
        expected_probe(19.0f, 18.0f, 1u, DamageRingCollisionDirection::PositiveX),
    }};

    return check(same_ring(expected, ring), "positive collision response changed ring state incorrectly") &&
           check(same_queries(queries, expected_queries), "positive collision query sequence mismatch");
}

bool test_y_gravity_flag_selects_each_correction_direction() {
    struct GravityCase {
        float velocity;
        std::uint16_t flags;
        float expected_y;
        float expected_velocity;
        float probe_y;
        DamageRingCollisionDirection direction;
    };

    const std::array<GravityCase, 4u> cases = {{
        {8.0f, 0x2200u, 18.0f, -6.0f, 29.0f, DamageRingCollisionDirection::PositiveY},
        {8.0f, 0x2204u, 22.0f, -6.0f, 29.0f, DamageRingCollisionDirection::PositiveY},
        {-8.0f, 0x2200u, 22.0f, 6.0f, 11.0f, DamageRingCollisionDirection::NegativeY},
        {-8.0f, 0x2204u, 18.0f, 6.0f, 11.0f, DamageRingCollisionDirection::NegativeY},
    }};

    for (const GravityCase& item : cases) {
        DamageRing previous{};
        DamageRing next{};
        DamageRing ring{};
        initialize_ring(ring, &previous, &next);
        ring.position[0] = 10.0f;
        ring.position[1] = 20.0f;
        ring.velocity[0] = 0.0f;
        ring.velocity[1] = item.velocity;
        ring.parameter_b = item.flags;
        const RingSnapshot before = snapshot_ring(ring);
        QueryLog queries{};
        queries.responses[0] = -2.0f;

        resolve_damage_ring_collision(ring, scripted_query, &queries);

        RingSnapshot expected = before;
        expected.position[1] = float_bits(item.expected_y);
        expected.velocity[1] = float_bits(item.expected_velocity);
        const std::array<ExpectedProbe, 1u> expected_queries = {{
            expected_probe(10.0f, item.probe_y, 0u, item.direction),
        }};
        if (!check(same_ring(expected, ring), "gravity-sign collision response changed ring state incorrectly") ||
            !check(same_queries(queries, expected_queries), "gravity-sign collision query sequence mismatch")) {
            return false;
        }
    }
    return true;
}

bool test_x_hit_transitions_flags_after_y_miss() {
    DamageRing previous{};
    DamageRing next{};
    DamageRing ring{};
    initialize_ring(ring, &previous, &next);
    ring.position[0] = 10.0f;
    ring.position[1] = 20.0f;
    ring.velocity[0] = -4.0f;
    ring.velocity[1] = 8.0f;
    ring.parameter_b = 0xD082u;
    const RingSnapshot before = snapshot_ring(ring);
    QueryLog queries{};
    queries.responses = {{0.0f, -3.0f}};

    resolve_damage_ring_collision(ring, scripted_query, &queries);

    RingSnapshot expected = before;
    expected.position[0] = float_bits(13.0f);
    expected.velocity[0] = float_bits(3.0f);
    expected.parameter_b = 0xD042u;
    const std::array<ExpectedProbe, 2u> expected_queries = {{
        expected_probe(10.0f, 29.0f, 1u, DamageRingCollisionDirection::PositiveY),
        expected_probe(1.0f, 20.0f, 1u, DamageRingCollisionDirection::NegativeX),
    }};

    return check(same_ring(expected, ring), "x-only collision response changed ring state incorrectly") &&
           check(same_queries(queries, expected_queries), "x-only collision query sequence mismatch");
}

bool test_zero_and_positive_offsets_leave_ring_unchanged() {
    DamageRing previous{};
    DamageRing next{};
    DamageRing ring{};
    initialize_ring(ring, &previous, &next);
    ring.position[0] = 4.0f;
    ring.position[1] = 6.0f;
    ring.velocity[0] = -4.0f;
    ring.velocity[1] = 8.0f;
    ring.parameter_b = 0x8082u;
    const RingSnapshot before = snapshot_ring(ring);
    QueryLog queries{};
    queries.responses = {{0.0f, 2.0f}};
    const std::array<ExpectedProbe, 2u> expected_queries = {{
        expected_probe(4.0f, 15.0f, 1u, DamageRingCollisionDirection::PositiveY),
        expected_probe(-5.0f, 6.0f, 1u, DamageRingCollisionDirection::NegativeX),
    }};

    resolve_damage_ring_collision(ring, scripted_query, &queries);

    return check(same_ring(before, ring), "nonnegative collision offset changed ring state") &&
           check(same_queries(queries, expected_queries), "nonnegative collision query sequence mismatch");
}

bool test_signed_zero_velocities_make_no_queries() {
    DamageRing previous{};
    DamageRing next{};
    DamageRing ring{};
    initialize_ring(ring, &previous, &next);
    ring.velocity[0] = float_from_bits(0x80000000u);
    ring.velocity[1] = float_from_bits(0x00000000u);
    const RingSnapshot before = snapshot_ring(ring);
    QueryLog queries{};
    const std::array<ExpectedProbe, 0u> expected_queries{};

    resolve_damage_ring_collision(ring, scripted_query, &queries);

    return check(same_ring(before, ring), "signed zero velocity changed ring state") &&
           check(same_queries(queries, expected_queries), "signed zero velocity made a collision query");
}

bool test_subnormal_velocity_uses_gradual_underflow() {
    DamageRing previous{};
    DamageRing next{};
    DamageRing ring{};
    initialize_ring(ring, &previous, &next);
    ring.position[0] = 2.0f;
    ring.position[1] = 1.0f;
    ring.velocity[0] = float_from_bits(0x00000000u);
    ring.velocity[1] = float_from_bits(0x00000001u);
    ring.parameter_b = 0x2100u;
    const RingSnapshot before = snapshot_ring(ring);
    QueryLog queries{};
    queries.responses[0] = -0.5f;
    const std::array<ExpectedProbe, 1u> expected_queries = {{
        expected_probe(2.0f, 10.0f, 0u, DamageRingCollisionDirection::PositiveY),
    }};

    resolve_damage_ring_collision(ring, scripted_query, &queries);

    RingSnapshot expected = before;
    expected.position[1] = float_bits(0.5f);
    expected.velocity[1] = 0x80000001u;
    return check(same_ring(expected, ring), "subnormal collision response did not preserve gradual underflow") &&
           check(same_queries(queries, expected_queries), "subnormal collision query sequence mismatch");
}

bool test_overflowed_y_is_observed_by_later_x_query() {
    const float maximum_finite = float_from_bits(0x7F7FFFFFu);
    DamageRing previous{};
    DamageRing next{};
    DamageRing ring{};
    initialize_ring(ring, &previous, &next);
    ring.position[0] = 1.0f;
    ring.position[1] = maximum_finite;
    ring.velocity[0] = 1.0f;
    ring.velocity[1] = 1.0f;
    ring.parameter_b = 0x0006u;
    const RingSnapshot before = snapshot_ring(ring);
    QueryLog queries{};
    queries.responses = {{-maximum_finite, 0.0f}};
    const std::array<ExpectedProbe, 2u> expected_queries = {{
        expected_probe(1.0f, maximum_finite, 1u, DamageRingCollisionDirection::PositiveY),
        expected_probe(10.0f, float_from_bits(0x7F800000u), 1u, DamageRingCollisionDirection::PositiveX),
    }};

    resolve_damage_ring_collision(ring, scripted_query, &queries);

    RingSnapshot expected = before;
    expected.position[1] = 0x7F800000u;
    expected.velocity[1] = float_bits(-0.75f);
    return check(same_ring(expected, ring), "overflowed y collision response changed ring state incorrectly") &&
           check(same_queries(queries, expected_queries), "overflowed y was not refreshed into the x query");
}

}

int main() {
    return test_positive_y_then_x_hits_transition_flags() &&
                   test_y_gravity_flag_selects_each_correction_direction() &&
                   test_x_hit_transitions_flags_after_y_miss() &&
                   test_zero_and_positive_offsets_leave_ring_unchanged() &&
                   test_signed_zero_velocities_make_no_queries() &&
                   test_subnormal_velocity_uses_gradual_underflow() &&
                   test_overflowed_y_is_observed_by_later_x_query()
               ? 0
               : 1;
}
