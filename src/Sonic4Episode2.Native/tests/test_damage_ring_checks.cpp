#include "damage_ring_checks.h"

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

bool check_inputs_unchanged(
    const BoundsSnapshot& a_before,
    const BoundsSnapshot& b_before,
    const RingPickupBounds& a,
    const RingPickupBounds& b) {
    return check(
        same_bounds(a_before, a) && same_bounds(b_before, b),
        "pickup overlap mutated a semantic input");
}

bool test_touching_edges_and_separation() {
    RingPickupBounds area = make_bounds(0.0f, 0.0f, 0, 0, 10, 10);
    RingPickupBounds edge = make_bounds(10.0f, 5.0f, 0, 0, 1, 1);
    RingPickupBounds corner = make_bounds(10.0f, 10.0f, 0, 0, 1, 1);
    RingPickupBounds separated_x = make_bounds(11.0f, 5.0f, 0, 0, 1, 1);
    RingPickupBounds separated_y = make_bounds(5.0f, 11.0f, 0, 0, 1, 1);
    const BoundsSnapshot area_before = snapshot_bounds(area);
    const BoundsSnapshot edge_before = snapshot_bounds(edge);
    const BoundsSnapshot corner_before = snapshot_bounds(corner);
    const BoundsSnapshot separated_x_before = snapshot_bounds(separated_x);
    const BoundsSnapshot separated_y_before = snapshot_bounds(separated_y);
    bool passed = true;

    passed = check(damage_ring_pickup_overlaps(area, edge), "touching vertical edge did not overlap") && passed;
    passed = check(damage_ring_pickup_overlaps(area, corner), "touching corner did not overlap") && passed;
    passed = check(!damage_ring_pickup_overlaps(area, separated_x), "separated x edge overlapped") && passed;
    passed = check(!damage_ring_pickup_overlaps(area, separated_y), "separated y edge overlapped") && passed;
    return check_inputs_unchanged(area_before, edge_before, area, edge) &&
           check_inputs_unchanged(area_before, corner_before, area, corner) &&
           check_inputs_unchanged(area_before, separated_x_before, area, separated_x) &&
           check_inputs_unchanged(area_before, separated_y_before, area, separated_y) &&
           passed;
}

bool test_containment_zero_extents_and_symmetry() {
    RingPickupBounds enclosing = make_bounds(0.0f, 0.0f, 0, 0, 20, 20);
    RingPickupBounds contained = make_bounds(5.0f, 5.0f, 0, 0, 5, 5);
    RingPickupBounds point = make_bounds(0.0f, 0.0f, 0, 0, 0, 0);
    RingPickupBounds point_container = make_bounds(-1.0f, -1.0f, 0, 0, 2, 2);
    RingPickupBounds distinct_point = make_bounds(1.0f, 0.0f, 0, 0, 0, 0);
    const BoundsSnapshot enclosing_before = snapshot_bounds(enclosing);
    const BoundsSnapshot contained_before = snapshot_bounds(contained);
    const BoundsSnapshot point_before = snapshot_bounds(point);
    const BoundsSnapshot point_container_before = snapshot_bounds(point_container);
    const BoundsSnapshot distinct_point_before = snapshot_bounds(distinct_point);
    bool passed = true;

    passed = check(damage_ring_pickup_overlaps(enclosing, contained), "contained bounds did not overlap") && passed;
    passed = check(damage_ring_pickup_overlaps(contained, enclosing), "overlap was not symmetric for containment") && passed;
    passed = check(damage_ring_pickup_overlaps(point, point_container), "zero extent point was not contained") && passed;
    passed = check(damage_ring_pickup_overlaps(point_container, point), "zero extent containment was not symmetric") && passed;
    passed = check(!damage_ring_pickup_overlaps(point, distinct_point), "separate zero extent points overlapped") && passed;
    return check_inputs_unchanged(enclosing_before, contained_before, enclosing, contained) &&
           check_inputs_unchanged(point_before, point_container_before, point, point_container) &&
           check_inputs_unchanged(point_before, distinct_point_before, point, distinct_point) &&
           passed;
}

bool test_inverted_bounds_remain_unordered() {
    RingPickupBounds inverted = make_bounds(0.0f, 0.0f, 10, 0, 0, 10);
    RingPickupBounds contains_inverted_start = make_bounds(0.0f, 0.0f, 0, 0, 20, 10);
    RingPickupBounds same_start_inverted = make_bounds(0.0f, 0.0f, 10, 0, 7, 10);
    const BoundsSnapshot inverted_before = snapshot_bounds(inverted);
    const BoundsSnapshot contains_before = snapshot_bounds(contains_inverted_start);
    const BoundsSnapshot same_start_before = snapshot_bounds(same_start_inverted);
    bool passed = true;

    passed = check(
                 damage_ring_pickup_overlaps(inverted, contains_inverted_start),
                 "inverted start contained by a normal interval did not overlap") &&
             passed;
    passed = check(
                 damage_ring_pickup_overlaps(contains_inverted_start, inverted),
                 "inverted overlap was not symmetric") &&
             passed;
    passed = check(
                 !damage_ring_pickup_overlaps(inverted, same_start_inverted),
                 "equal-start inverted intervals were normalized into overlap") &&
             passed;
    return check_inputs_unchanged(inverted_before, contains_before, inverted, contains_inverted_start) &&
           check_inputs_unchanged(inverted_before, same_start_before, inverted, same_start_inverted) &&
           passed;
}

bool test_fractional_origins_truncate_toward_zero() {
    RingPickupBounds a = make_bounds(-0.9f, 0.9f, 0, 0, 0, 0);
    RingPickupBounds b = make_bounds(0.9f, -0.9f, 0, 0, 0, 0);
    RingPickupBounds separated = make_bounds(1.1f, -0.9f, 0, 0, 0, 0);
    const BoundsSnapshot a_before = snapshot_bounds(a);
    const BoundsSnapshot b_before = snapshot_bounds(b);
    const BoundsSnapshot separated_before = snapshot_bounds(separated);

    return check(damage_ring_pickup_overlaps(a, b), "positive and negative fractional origins did not truncate toward zero") &&
           check(!damage_ring_pickup_overlaps(a, separated), "fractional origin was rounded into overlap") &&
           check_inputs_unchanged(a_before, b_before, a, b) &&
           check_inputs_unchanged(a_before, separated_before, a, separated);
}

bool test_signed_32_bit_wrapping_changes_containment() {
    RingPickupBounds a = make_bounds(-2147483648.0f, 0.0f, -196, 0, 4, 0);
    RingPickupBounds b = make_bounds(-2147483648.0f, 0.0f, 0, 0, 196, 0);
    const BoundsSnapshot a_before = snapshot_bounds(a);
    const BoundsSnapshot b_before = snapshot_bounds(b);

    return check(!damage_ring_pickup_overlaps(a, b), "wrapped signed-32 interval used unwrapped containment") &&
           check(!damage_ring_pickup_overlaps(b, a), "wrapped containment was not symmetric") &&
           check_inputs_unchanged(a_before, b_before, a, b);
}

bool test_draw_timer_gate() {
    struct TimerCase {
        std::uint16_t timer;
        bool expected;
    };

    const std::array<TimerCase, 11u> cases = {{
        {0u, false},
        {1u, false},
        {2u, true},
        {32u, false},
        {33u, true},
        {32767u, true},
        {32768u, false},
        {32769u, false},
        {32770u, true},
        {65534u, true},
        {65535u, true},
    }};

    for (const TimerCase& item : cases) {
        if (!check(
                damage_ring_passes_draw_timer(item.timer) == item.expected,
                "draw timer gate result mismatch")) {
            return false;
        }
    }
    return true;
}

}

int main() {
    return test_touching_edges_and_separation() &&
                   test_containment_zero_extents_and_symmetry() &&
                   test_inverted_bounds_remain_unordered() &&
                   test_fractional_origins_truncate_toward_zero() &&
                   test_signed_32_bit_wrapping_changes_containment() &&
                   test_draw_timer_gate()
               ? 0
               : 1;
}
