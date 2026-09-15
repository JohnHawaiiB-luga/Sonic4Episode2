#include "view_bounds.h"

#include <cmath>
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

std::uint32_t float_bits(float value) {
    std::uint32_t bits = 0u;
    std::memcpy(&bits, &value, sizeof(bits));
    return bits;
}

ViewState make_view(
    float origin_x,
    float origin_y,
    float scale_x,
    float scale_y,
    std::int16_t width,
    std::int16_t height,
    bool enabled) {
    return {{origin_x, origin_y}, {scale_x, scale_y}, width, height, enabled};
}

ViewMargins make_margins(
    std::int16_t margin,
    std::int16_t left,
    std::int16_t top,
    std::int16_t right,
    std::int16_t bottom) {
    return {margin, left, top, right, bottom};
}

struct ViewSnapshot {
    std::uint32_t origin_x;
    std::uint32_t origin_y;
    std::uint32_t scale_x;
    std::uint32_t scale_y;
    std::int16_t width;
    std::int16_t height;
    bool enabled;
};

ViewSnapshot snapshot_view(const ViewState& view) {
    return {
        float_bits(view.origin[0]),
        float_bits(view.origin[1]),
        float_bits(view.scale[0]),
        float_bits(view.scale[1]),
        view.width,
        view.height,
        view.enabled,
    };
}

bool same_view(const ViewSnapshot& expected, const ViewState& actual) {
    const ViewSnapshot current = snapshot_view(actual);
    return expected.origin_x == current.origin_x &&
           expected.origin_y == current.origin_y &&
           expected.scale_x == current.scale_x &&
           expected.scale_y == current.scale_y &&
           expected.width == current.width &&
           expected.height == current.height &&
           expected.enabled == current.enabled;
}

bool same_margins(const ViewMargins& expected, const ViewMargins& actual) {
    return expected.margin == actual.margin &&
           expected.left == actual.left &&
           expected.top == actual.top &&
           expected.right == actual.right &&
           expected.bottom == actual.bottom;
}

bool check_inputs_unchanged(
    const ViewSnapshot& view_before,
    const ViewMargins& margins_before,
    const ViewState& view,
    const ViewMargins& margins) {
    return check(
        same_view(view_before, view) && same_margins(margins_before, margins),
        "view predicate mutated an input field");
}

bool test_disabled_view_is_never_outside() {
    ViewState view = make_view(123.25f, -45.75f, 0.5f, 1.5f, 100, 200, false);
    ViewMargins margins = make_margins(196, -7, 11, 13, -17);
    const ViewSnapshot view_before = snapshot_view(view);
    const ViewMargins margins_before = margins;

    return check(
               !is_outside_view(999.25f, -999.75f, view, margins),
               "disabled view reported a point outside") &&
           check_inputs_unchanged(view_before, margins_before, view, margins);
}

bool test_inclusive_edges_and_strict_exclusions() {
    ViewState view = make_view(10.0f, 20.0f, 1.0f, 1.0f, 100, 50, true);
    ViewMargins margins = make_margins(3, 1, 2, 4, 5);
    const ViewSnapshot view_before = snapshot_view(view);
    const ViewMargins margins_before = margins;
    bool passed = true;

    passed = check(!is_outside_view(8.0f, 19.0f, view, margins), "lower inclusive edge was outside") && passed;
    passed = check(!is_outside_view(117.0f, 78.0f, view, margins), "upper inclusive edge was outside") && passed;
    passed = check(is_outside_view(7.0f, 19.0f, view, margins), "x just below the lower edge was inside") && passed;
    passed = check(is_outside_view(118.0f, 78.0f, view, margins), "x just above the upper edge was inside") && passed;
    passed = check(is_outside_view(8.0f, 18.0f, view, margins), "y just below the lower edge was inside") && passed;
    passed = check(is_outside_view(117.0f, 79.0f, view, margins), "y just above the upper edge was inside") && passed;
    return check_inputs_unchanged(view_before, margins_before, view, margins) && passed;
}

bool test_coordinates_and_origins_truncate_toward_zero() {
    ViewState view = make_view(-0.9f, 0.9f, 1.0f, 1.0f, 0, 0, true);
    ViewMargins margins = make_margins(0, 0, 0, 0, 0);
    const ViewSnapshot view_before = snapshot_view(view);
    const ViewMargins margins_before = margins;
    bool passed = true;

    passed = check(
                 !is_outside_view(0.9f, -0.9f, view, margins),
                 "fractional coordinates or origins did not truncate toward zero") &&
             passed;
    passed = check(
                 !is_outside_view(-0.9f, 0.9f, view, margins),
                 "negative fractional coordinates or origins did not truncate toward zero") &&
             passed;
    passed = check(is_outside_view(1.1f, 0.0f, view, margins), "positive fractional x was rounded into bounds") &&
             passed;
    passed = check(is_outside_view(-1.1f, 0.0f, view, margins), "negative fractional x was rounded into bounds") &&
             passed;
    passed = check(is_outside_view(0.0f, 1.1f, view, margins), "positive fractional y was rounded into bounds") &&
             passed;
    passed = check(is_outside_view(0.0f, -1.1f, view, margins), "negative fractional y was rounded into bounds") &&
             passed;
    return check_inputs_unchanged(view_before, margins_before, view, margins) && passed;
}

bool test_asymmetric_margins_are_independent_per_axis() {
    ViewState view = make_view(100.0f, -100.0f, 1.0f, 1.0f, 10, 20, true);
    ViewMargins margins = make_margins(5, -2, 3, 7, -4);
    const ViewSnapshot view_before = snapshot_view(view);
    const ViewMargins margins_before = margins;
    bool passed = true;

    passed = check(!is_outside_view(93.0f, -102.0f, view, margins), "asymmetric lower edges were outside") && passed;
    passed = check(!is_outside_view(122.0f, -79.0f, view, margins), "asymmetric upper edges were outside") && passed;
    passed = check(is_outside_view(92.0f, -90.0f, view, margins), "left margin did not constrain x") && passed;
    passed = check(is_outside_view(123.0f, -90.0f, view, margins), "right margin did not constrain x") && passed;
    passed = check(is_outside_view(100.0f, -103.0f, view, margins), "top margin did not constrain y") && passed;
    passed = check(is_outside_view(100.0f, -78.0f, view, margins), "bottom margin did not constrain y") && passed;
    return check_inputs_unchanged(view_before, margins_before, view, margins) && passed;
}

bool test_ring_margin_196() {
    ViewState view = make_view(0.0f, 0.0f, 1.0f, 1.0f, 100, 100, true);
    ViewMargins margins = make_margins(196, 0, 0, 0, 0);
    const ViewSnapshot view_before = snapshot_view(view);
    const ViewMargins margins_before = margins;
    bool passed = true;

    passed = check(!is_outside_view(-196.0f, -196.0f, view, margins), "margin 196 lower edge was outside") && passed;
    passed = check(!is_outside_view(296.0f, 296.0f, view, margins), "margin 196 upper edge was outside") && passed;
    passed = check(is_outside_view(-197.0f, 0.0f, view, margins), "margin 196 left exclusion was inside") && passed;
    passed = check(is_outside_view(297.0f, 0.0f, view, margins), "margin 196 right exclusion was inside") && passed;
    passed = check(is_outside_view(0.0f, -197.0f, view, margins), "margin 196 top exclusion was inside") && passed;
    passed = check(is_outside_view(0.0f, 297.0f, view, margins), "margin 196 bottom exclusion was inside") && passed;
    return check_inputs_unchanged(view_before, margins_before, view, margins) && passed;
}

bool test_negative_extent_remains_inverted() {
    ViewState view = make_view(0.0f, 0.0f, 1.0f, 1.0f, -10, 10, true);
    ViewMargins margins = make_margins(0, 0, 0, 0, 0);
    const ViewSnapshot view_before = snapshot_view(view);
    const ViewMargins margins_before = margins;

    return check(is_outside_view(0.0f, 5.0f, view, margins), "negative width was normalized at its low edge") &&
           check(is_outside_view(-10.0f, 5.0f, view, margins), "negative width was normalized at its high edge") &&
           check_inputs_unchanged(view_before, margins_before, view, margins);
}

bool test_scale_deadband_and_adjacent_float_values() {
    const float lower_deadband = 1.0f - 0x1p-23f;
    const float upper_deadband = 1.0f + 0x1p-23f;
    const float below_lower = std::nextafter(lower_deadband, -std::numeric_limits<float>::infinity());
    const float above_upper = std::nextafter(upper_deadband, std::numeric_limits<float>::infinity());
    ViewState lower_view = make_view(0.0f, 0.0f, lower_deadband, lower_deadband, 32767, 32767, true);
    ViewState upper_view = make_view(0.0f, 0.0f, upper_deadband, upper_deadband, 32767, 32767, true);
    ViewState below_lower_view = make_view(0.0f, 0.0f, below_lower, below_lower, 32767, 32767, true);
    ViewState above_upper_view = make_view(0.0f, 0.0f, above_upper, above_upper, 32767, 32767, true);
    ViewMargins margins = make_margins(0, 0, 0, 0, 0);
    const ViewSnapshot lower_before = snapshot_view(lower_view);
    const ViewSnapshot upper_before = snapshot_view(upper_view);
    const ViewSnapshot below_lower_before = snapshot_view(below_lower_view);
    const ViewSnapshot above_upper_before = snapshot_view(above_upper_view);
    const ViewMargins margins_before = margins;
    bool passed = true;

    passed = check(!is_outside_view(32767.0f, 32767.0f, lower_view, margins), "lower deadband endpoint scaled an extent") &&
             passed;
    passed = check(!is_outside_view(32767.0f, 32767.0f, upper_view, margins), "upper deadband endpoint scaled an extent") &&
             passed;
    passed = check(
                 !is_outside_view(32767.0f, 32767.0f, below_lower_view, margins),
                 "lower adjacent scale unexpectedly changed the concrete extent") &&
             passed;
    passed = check(
                 !is_outside_view(32766.0f, 32766.0f, above_upper_view, margins),
                 "upper adjacent scale did not retain its truncated extent") &&
             passed;
    passed = check(
                 is_outside_view(32767.0f, 32766.0f, above_upper_view, margins),
                 "upper adjacent x scale did not shrink the extent") &&
             passed;
    passed = check(
                 is_outside_view(32766.0f, 32767.0f, above_upper_view, margins),
                 "upper adjacent y scale did not shrink the extent") &&
             passed;
    return check_inputs_unchanged(lower_before, margins_before, lower_view, margins) &&
           check_inputs_unchanged(upper_before, margins_before, upper_view, margins) &&
           check_inputs_unchanged(below_lower_before, margins_before, below_lower_view, margins) &&
           check_inputs_unchanged(above_upper_before, margins_before, above_upper_view, margins) &&
           passed;
}

bool test_scaled_extent_wraps_to_signed_16_bit() {
    ViewState view = make_view(0.0f, 0.0f, -1.0f, 1.0f, 30000, 5, true);
    ViewMargins margins = make_margins(0, 0, 0, 0, 0);
    const ViewSnapshot view_before = snapshot_view(view);
    const ViewMargins margins_before = margins;

    return check(!is_outside_view(24464.0f, 0.0f, view, margins), "wrapped signed-16 extent excluded its upper edge") &&
           check(is_outside_view(24465.0f, 0.0f, view, margins), "scaled extent did not wrap to signed 16 bits") &&
           check_inputs_unchanged(view_before, margins_before, view, margins);
}

bool test_bounds_wrap_as_signed_32_bit() {
    ViewState high_wrap_view = make_view(2147483520.0f, 0.0f, 1.0f, 1.0f, 200, 0, true);
    ViewState low_wrap_view = make_view(-2147483648.0f, 0.0f, 1.0f, 1.0f, 0, 0, true);
    ViewMargins no_margins = make_margins(0, 0, 0, 0, 0);
    ViewMargins margin_196 = make_margins(196, 0, 0, 0, 0);
    const ViewSnapshot high_wrap_before = snapshot_view(high_wrap_view);
    const ViewSnapshot low_wrap_before = snapshot_view(low_wrap_view);
    const ViewMargins no_margins_before = no_margins;
    const ViewMargins margin_196_before = margin_196;

    return check(
               is_outside_view(2147483520.0f, 0.0f, high_wrap_view, no_margins),
               "positive bound did not wrap through signed 32-bit range") &&
           check(
               is_outside_view(-2147483648.0f, 0.0f, low_wrap_view, margin_196),
               "negative bound did not wrap through signed 32-bit range") &&
           check_inputs_unchanged(high_wrap_before, no_margins_before, high_wrap_view, no_margins) &&
           check_inputs_unchanged(low_wrap_before, margin_196_before, low_wrap_view, margin_196);
}

}

int main() {
    return test_disabled_view_is_never_outside() &&
                   test_inclusive_edges_and_strict_exclusions() &&
                   test_coordinates_and_origins_truncate_toward_zero() &&
                   test_asymmetric_margins_are_independent_per_axis() &&
                   test_ring_margin_196() &&
                   test_negative_extent_remains_inverted() &&
                   test_scale_deadband_and_adjacent_float_values() &&
                   test_scaled_extent_wraps_to_signed_16_bit() &&
                   test_bounds_wrap_as_signed_32_bit()
               ? 0
               : 1;
}
