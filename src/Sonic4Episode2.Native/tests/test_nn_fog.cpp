#include "nn_fog.h"

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <limits>
#include <stdexcept>
#include <utility>

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

struct FogSnapshot {
    std::array<std::uint32_t, 4u> color;
    std::array<std::uint32_t, 4u> configured;
    std::array<std::uint32_t, 4u> active;
    std::uint32_t packed_argb;
    std::uint32_t mode;
    std::uint32_t range_near;
    std::uint32_t range_far;
    std::uint32_t coefficient_a;
    std::uint32_t coefficient_b;
    std::uint32_t requested;
    std::uint32_t effective;
};

FogSnapshot snapshot(const NnFogState& state) {
    FogSnapshot result{};
    for (std::size_t index = 0u; index < state.color.size(); ++index) {
        result.color[index] = float_bits(state.color[index]);
    }
    result.configured = {{
        float_bits(state.configured.density),
        float_bits(state.configured.start),
        float_bits(state.configured.end),
        float_bits(state.configured.scale),
    }};
    result.active = {{
        float_bits(state.active.density),
        float_bits(state.active.start),
        float_bits(state.active.end),
        float_bits(state.active.scale),
    }};
    result.packed_argb = state.packed_argb;
    result.mode = state.mode;
    result.range_near = float_bits(state.range_near);
    result.range_far = float_bits(state.range_far);
    result.coefficient_a = float_bits(state.coefficient_a);
    result.coefficient_b = float_bits(state.coefficient_b);
    result.requested = state.requested;
    result.effective = state.effective;
    return result;
}

bool same_snapshot(const FogSnapshot& expected, const NnFogState& state) {
    const FogSnapshot actual = snapshot(state);
    return expected.color == actual.color &&
           expected.configured == actual.configured &&
           expected.active == actual.active &&
           expected.packed_argb == actual.packed_argb &&
           expected.mode == actual.mode &&
           expected.range_near == actual.range_near &&
           expected.range_far == actual.range_far &&
           expected.coefficient_a == actual.coefficient_a &&
           expected.coefficient_b == actual.coefficient_b &&
           expected.requested == actual.requested &&
           expected.effective == actual.effective;
}

NnFogState make_state() {
    NnFogState state{};
    state.color = {{
        float_from_bits(0x3e000000u),
        float_from_bits(0x3e800000u),
        float_from_bits(0x3f400000u),
        float_from_bits(0x3f200000u),
    }};
    state.configured = {
        float_from_bits(0x3e800000u),
        float_from_bits(0x41200000u),
        float_from_bits(0x42dc0000u),
        float_from_bits(0x3c23d70au),
    };
    state.active = {
        float_from_bits(0xbe000000u),
        float_from_bits(0xc0400000u),
        float_from_bits(0xc0000000u),
        float_from_bits(0xbe800000u),
    };
    state.packed_argb = 0x13579bdfu;
    state.mode = 7u;
    state.range_near = float_from_bits(0x41200000u);
    state.range_far = float_from_bits(0x42dc0000u);
    state.coefficient_a = float_from_bits(0x3ec00000u);
    state.coefficient_b = float_from_bits(0xbf200000u);
    state.requested = 0u;
    state.effective = 17u;
    return state;
}

template <typename Callable>
bool expects_invalid_argument(Callable&& callable, const char* message) {
    try {
        std::forward<Callable>(callable)();
    } catch (const std::invalid_argument&) {
        return true;
    } catch (...) {
        std::fprintf(stderr, "%s (wrong exception type)\n", message);
        return false;
    }

    std::fprintf(stderr, "%s\n", message);
    return false;
}

template <typename Callable>
bool rejects_without_mutation(NnFogState& state, Callable&& callable, const char* message) {
    const FogSnapshot before = snapshot(state);
    const bool rejected = expects_invalid_argument(std::forward<Callable>(callable), message);
    return check(same_snapshot(before, state), "rejected fog operation changed state") && rejected;
}

bool test_request_changes_only_request_and_coefficients() {
    NnFogState state = make_state();
    FogSnapshot expected = snapshot(state);
    nn_set_fog_request(state, true, NnFogPrecision::Single);
    expected.requested = 1u;
    expected.coefficient_b = 0x3dcccccdu;
    bool passed = check(same_snapshot(expected, state),
                        "fog request-on did not preserve its limited write set");

    expected = snapshot(state);
    nn_set_fog_request(state, false, NnFogPrecision::Double);
    expected.requested = 0u;
    expected.coefficient_a = 0x00000000u;
    expected.coefficient_b = 0x00000000u;
    passed = check(same_snapshot(expected, state),
                   "fog request-off did not clear both coefficients with positive zero") &&
             passed;
    return passed;
}

bool test_color_preserves_alpha_and_uses_truncating_components() {
    bool passed = true;

    {
        NnFogState state = make_state();
        state.range_near = std::numeric_limits<float>::quiet_NaN();
        state.range_far = std::numeric_limits<float>::quiet_NaN();
        state.color[3u] = std::numeric_limits<float>::quiet_NaN();
        FogSnapshot expected = snapshot(state);
        nn_set_fog_color(
            state,
            {{float_from_bits(0x3e000000u),
              float_from_bits(0x3f000000u),
              float_from_bits(0x3f600000u)}},
            NnFogPrecision::Single);
        expected.color[0u] = 0x3e000000u;
        expected.color[1u] = 0x3f000000u;
        expected.color[2u] = 0x3f600000u;
        expected.packed_argb = 0xff1f7fdfu;
        passed = check(same_snapshot(expected, state),
                       "fog color changed alpha, ignored fields, or truncating packed color") &&
                 passed;
    }

    {
        NnFogState state = make_state();
        FogSnapshot expected = snapshot(state);
        nn_set_fog_color(
            state,
            {{float_from_bits(0x80000000u), 0.0f, 1.0f}},
            NnFogPrecision::Double);
        expected.color[0u] = 0x80000000u;
        expected.color[1u] = 0x00000000u;
        expected.color[2u] = 0x3f800000u;
        expected.packed_argb = 0xff0000ffu;
        passed = check(same_snapshot(expected, state),
                       "fog color did not preserve signed zero or boundary components") &&
                 passed;
    }

    {
        NnFogState state = make_state();
        state.requested = 1u;
        FogSnapshot expected = snapshot(state);
        nn_set_fog_color(
            state,
            {{float_from_bits(0x3b808081u),
              float_from_bits(0x3efefeffu),
              float_from_bits(0x3f7efeffu)}},
            NnFogPrecision::Double);
        expected.color[0u] = 0x3b808081u;
        expected.color[1u] = 0x3efefeffu;
        expected.color[2u] = 0x3f7efeffu;
        expected.packed_argb = 0xff017ffeu;
        expected.coefficient_a = 0xbc23d70au;
        expected.coefficient_b = 0x3dcccccdu;
        passed = check(same_snapshot(expected, state),
                       "requested fog color did not update packed color and both coefficients") &&
                 passed;
    }

    return passed;
}

bool test_range_uses_requested_precision_and_preserves_active_parameters() {
    struct RangeCase {
        NnFogPrecision precision;
        std::uint32_t scale;
        std::uint32_t coefficient_a;
        std::uint32_t coefficient_b;
    };

    const std::array<RangeCase, 2u> cases = {{
        {NnFogPrecision::Single, 0x3fc00001u, 0xbfc00001u, 0x3f000001u},
        {NnFogPrecision::Double, 0x3fc00000u, 0xbfc00000u, 0x3f000000u},
    }};
    const float near_distance = float_from_bits(0x3eaaaaabu);
    const float far_distance = 1.0f;
    bool passed = true;

    for (const RangeCase& expected_case : cases) {
        NnFogState state = make_state();
        FogSnapshot expected = snapshot(state);
        nn_set_fog_range(state, near_distance, far_distance, expected_case.precision);
        expected.range_near = 0x3eaaaaabu;
        expected.range_far = 0x3f800000u;
        expected.configured[1u] = 0x3eaaaaabu;
        expected.configured[2u] = 0x3f800000u;
        expected.configured[3u] = expected_case.scale;
        expected.mode = 3u;
        passed = check(same_snapshot(expected, state),
                       "unrequested fog range changed fields or rounded scale incorrectly") &&
                 passed;

        state = make_state();
        state.requested = 1u;
        expected = snapshot(state);
        nn_set_fog_range(state, near_distance, far_distance, expected_case.precision);
        expected.range_near = 0x3eaaaaabu;
        expected.range_far = 0x3f800000u;
        expected.configured[1u] = 0x3eaaaaabu;
        expected.configured[2u] = 0x3f800000u;
        expected.configured[3u] = expected_case.scale;
        expected.coefficient_a = expected_case.coefficient_a;
        expected.coefficient_b = expected_case.coefficient_b;
        expected.mode = 3u;
        passed = check(same_snapshot(expected, state),
                       "requested fog range did not apply independent precision coefficients") &&
                 passed;
    }

    return passed;
}

bool test_apply_requires_explicit_refresh_and_preserves_raw_parameters() {
    bool passed = true;

    {
        NnFogState state = make_state();
        state.requested = 1u;
        FogSnapshot expected = snapshot(state);
        nn_apply_fog(state, false, 300.0f, NnFogPrecision::Single);
        expected.active[0u] = 0x00000000u;
        expected.active[1u] = 0x43960000u;
        expected.active[2u] = 0x43968000u;
        expected.active[3u] = 0x3f800000u;
        expected.effective = 0u;
        passed = check(same_snapshot(expected, state),
                       "disabled fog did not produce the measured fallback parameters") &&
                 passed;
    }

    {
        NnFogState state = make_state();
        const float negative_zero = float_from_bits(0x80000000u);
        state.configured = {negative_zero, negative_zero, negative_zero, negative_zero};
        FogSnapshot expected = snapshot(state);
        nn_apply_fog(
            state,
            true,
            std::numeric_limits<float>::quiet_NaN(),
            NnFogPrecision::Double);
        expected.active = {{0x80000000u, 0x80000000u, 0x80000000u, 0x80000000u}};
        expected.effective = 1u;
        passed = check(same_snapshot(expected, state),
                       "enabled fog did not raw-copy configured signed-zero parameters") &&
                 passed;
    }

    {
        NnFogState state = make_state();
        state.effective = 1u;
        FogSnapshot expected = snapshot(state);
        nn_set_fog_range(state, 20.0f, 220.0f, NnFogPrecision::Double);
        expected.range_near = 0x41a00000u;
        expected.range_far = 0x435c0000u;
        expected.configured[1u] = 0x41a00000u;
        expected.configured[2u] = 0x435c0000u;
        expected.configured[3u] = 0x3ba3d70au;
        expected.mode = 3u;
        passed = check(same_snapshot(expected, state),
                       "fog range implicitly refreshed active parameters") &&
                 passed;

        nn_apply_fog(
            state,
            true,
            std::numeric_limits<float>::quiet_NaN(),
            NnFogPrecision::Double);
        expected.active = expected.configured;
        expected.effective = 1u;
        passed = check(same_snapshot(expected, state),
                       "explicit fog refresh did not copy configured parameters") &&
                 passed;
    }

    return passed;
}

bool test_accepts_reversed_and_subnormal_finite_ranges() {
    NnFogState reversed = make_state();
    reversed.requested = 1u;
    nn_set_fog_range(reversed, 100.0f, 50.0f, NnFogPrecision::Double);
    bool passed = check(
        float_bits(reversed.range_near) == 0x42c80000u &&
            float_bits(reversed.range_far) == 0x42480000u &&
            float_bits(reversed.configured.start) == 0x42c80000u &&
            float_bits(reversed.configured.end) == 0x42480000u &&
            reversed.mode == 3u && std::isfinite(reversed.configured.scale) &&
            reversed.configured.scale < 0.0f && std::isfinite(reversed.coefficient_a) &&
            std::isfinite(reversed.coefficient_b),
        "reversed finite fog range was not supported");

    NnFogState subnormal = make_state();
    const float near_distance = float_from_bits(0x00000001u);
    const float far_distance = float_from_bits(0x00800000u);
    nn_set_fog_range(subnormal, near_distance, far_distance, NnFogPrecision::Single);
    passed = check(
                 float_bits(subnormal.range_near) == 0x00000001u &&
                     float_bits(subnormal.range_far) == 0x00800000u &&
                     float_bits(subnormal.configured.start) == 0x00000001u &&
                     float_bits(subnormal.configured.end) == 0x00800000u &&
                     subnormal.mode == 3u && std::isfinite(subnormal.configured.scale) &&
                     subnormal.configured.scale > 0.0f,
                 "finite subnormal fog range was not supported") &&
             passed;
    return passed;
}

bool test_rejects_consumed_invalid_inputs_without_mutation() {
    bool passed = true;
    const float outside_limit = std::nextafter(
        16777216.0f,
        std::numeric_limits<float>::infinity());
    const float minimum_subnormal = float_from_bits(0x00000001u);

    {
        NnFogState state = make_state();
        state.range_near = std::numeric_limits<float>::quiet_NaN();
        passed = rejects_without_mutation(
                     state,
                     [&]() { nn_set_fog_request(state, true, NnFogPrecision::Single); },
                     "request-on accepted a nonfinite fog range") &&
                 passed;
    }
    {
        NnFogState state = make_state();
        state.requested = 1u;
        state.range_far = state.range_near;
        passed = rejects_without_mutation(
                     state,
                     [&]() {
                         nn_set_fog_color(state, {{1.0f, 0.0f, 1.0f}}, NnFogPrecision::Double);
                     },
                     "requested fog color accepted a zero range delta") &&
                 passed;
    }
    {
        NnFogState state = make_state();
        passed = rejects_without_mutation(
                     state,
                     [&]() { nn_set_fog_range(state, 1.0f, 1.0f, NnFogPrecision::Double); },
                     "equal fog range was accepted") &&
                 passed;
    }
    {
        NnFogState state = make_state();
        passed = rejects_without_mutation(
                     state,
                     [&]() { nn_set_fog_range(state, 0.0f, outside_limit, NnFogPrecision::Single); },
                     "out-of-bounds fog range was accepted") &&
                 passed;
    }
    {
        NnFogState state = make_state();
        passed = rejects_without_mutation(
                     state,
                     [&]() { nn_set_fog_range(state, 0.0f, minimum_subnormal, NnFogPrecision::Double); },
                     "nonfinite stored fog scale was accepted") &&
                 passed;
    }
    {
        NnFogState state = make_state();
        passed = rejects_without_mutation(
                     state,
                     [&]() {
                         nn_set_fog_color(
                             state,
                             {{-0.5f, 0.0f, 0.0f}},
                             NnFogPrecision::Single);
                     },
                     "out-of-bounds fog color was accepted") &&
                 passed;
    }
    {
        NnFogState state = make_state();
        passed = rejects_without_mutation(
                     state,
                     [&]() {
                         nn_apply_fog(
                             state,
                             false,
                             std::numeric_limits<float>::quiet_NaN(),
                             NnFogPrecision::Double);
                     },
                     "nonfinite disabled fog fallback was accepted") &&
                 passed;
    }
    {
        NnFogState state = make_state();
        state.configured.density = std::numeric_limits<float>::infinity();
        passed = rejects_without_mutation(
                     state,
                     [&]() { nn_apply_fog(state, true, 0.0f, NnFogPrecision::Single); },
                     "nonfinite enabled fog configuration was accepted") &&
                 passed;
    }

    const NnFogPrecision invalid_precision = static_cast<NnFogPrecision>(2);
    {
        NnFogState state = make_state();
        passed = rejects_without_mutation(
                     state,
                     [&]() { nn_set_fog_request(state, false, invalid_precision); },
                     "unsupported fog request precision was accepted") &&
                 passed;
    }
    {
        NnFogState state = make_state();
        passed = rejects_without_mutation(
                     state,
                     [&]() {
                         nn_set_fog_color(state, {{0.0f, 0.0f, 0.0f}}, invalid_precision);
                     },
                     "unsupported fog color precision was accepted") &&
                 passed;
    }
    {
        NnFogState state = make_state();
        passed = rejects_without_mutation(
                     state,
                     [&]() { nn_set_fog_range(state, 0.0f, 1.0f, invalid_precision); },
                     "unsupported fog range precision was accepted") &&
                 passed;
    }
    {
        NnFogState state = make_state();
        passed = rejects_without_mutation(
                     state,
                     [&]() { nn_apply_fog(state, false, 0.0f, invalid_precision); },
                     "unsupported fog effective precision was accepted") &&
                 passed;
    }

    return passed;
}

bool test_ignores_unconsumed_nonfinite_fields() {
    bool passed = true;

    {
        NnFogState state = make_state();
        state.range_near = std::numeric_limits<float>::quiet_NaN();
        state.range_far = std::numeric_limits<float>::quiet_NaN();
        FogSnapshot expected = snapshot(state);
        nn_set_fog_request(state, false, NnFogPrecision::Single);
        expected.coefficient_a = 0x00000000u;
        expected.coefficient_b = 0x00000000u;
        passed = check(same_snapshot(expected, state),
                       "request-off consumed an unused invalid fog range") &&
                 passed;
    }

    {
        NnFogState state = make_state();
        state.range_near = std::numeric_limits<float>::quiet_NaN();
        state.range_far = std::numeric_limits<float>::quiet_NaN();
        FogSnapshot expected = snapshot(state);
        nn_set_fog_color(state, {{0.0f, 0.0f, 0.0f}}, NnFogPrecision::Double);
        expected.color[0u] = 0x00000000u;
        expected.color[1u] = 0x00000000u;
        expected.color[2u] = 0x00000000u;
        expected.packed_argb = 0xff000000u;
        passed = check(same_snapshot(expected, state),
                       "unrequested fog color consumed an unused invalid range") &&
                 passed;
    }

    {
        NnFogState state = make_state();
        state.coefficient_a = std::numeric_limits<float>::quiet_NaN();
        state.coefficient_b = std::numeric_limits<float>::quiet_NaN();
        FogSnapshot expected = snapshot(state);
        nn_set_fog_range(state, 10.0f, 110.0f, NnFogPrecision::Double);
        expected.range_near = 0x41200000u;
        expected.range_far = 0x42dc0000u;
        expected.configured[1u] = 0x41200000u;
        expected.configured[2u] = 0x42dc0000u;
        expected.configured[3u] = 0x3c23d70au;
        expected.mode = 3u;
        passed = check(same_snapshot(expected, state),
                       "unrequested fog range consumed old coefficients") &&
                 passed;
    }

    {
        NnFogState state = make_state();
        state.configured = {
            std::numeric_limits<float>::quiet_NaN(),
            std::numeric_limits<float>::infinity(),
            -std::numeric_limits<float>::infinity(),
            std::numeric_limits<float>::quiet_NaN(),
        };
        FogSnapshot expected = snapshot(state);
        nn_apply_fog(state, false, 300.0f, NnFogPrecision::Single);
        expected.active[0u] = 0x00000000u;
        expected.active[1u] = 0x43960000u;
        expected.active[2u] = 0x43968000u;
        expected.active[3u] = 0x3f800000u;
        expected.effective = 0u;
        passed = check(same_snapshot(expected, state),
                       "disabled fog consumed an unused invalid configuration") &&
                 passed;
    }

    return passed;
}

}

int main() {
    return test_request_changes_only_request_and_coefficients() &&
                   test_color_preserves_alpha_and_uses_truncating_components() &&
                   test_range_uses_requested_precision_and_preserves_active_parameters() &&
                   test_apply_requires_explicit_refresh_and_preserves_raw_parameters() &&
                   test_accepts_reversed_and_subnormal_finite_ranges() &&
                   test_rejects_consumed_invalid_inputs_without_mutation() &&
                   test_ignores_unconsumed_nonfinite_fields()
               ? 0
               : 1;
}
