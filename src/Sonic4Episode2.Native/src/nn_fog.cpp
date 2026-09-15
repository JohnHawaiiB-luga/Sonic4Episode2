#include "nn_fog.h"

#include <cmath>
#include <cstdint>
#include <stdexcept>

namespace {

constexpr float kInputLimit = 16777216.0f;

[[noreturn]] void fail(const char* message) {
    throw std::invalid_argument(message);
}

void require_supported_precision(NnFogPrecision precision) {
    if (precision != NnFogPrecision::Single && precision != NnFogPrecision::Double) {
        fail("fog precision is unsupported");
    }
}

void require_finite_bounded(float value, const char* message) {
    if (!std::isfinite(value) || std::fabs(value) > kInputLimit) {
        fail(message);
    }
}

void require_finite(float value, const char* message) {
    if (!std::isfinite(value)) {
        fail(message);
    }
}

void require_color_component(float value) {
    if (!std::isfinite(value) || value < 0.0f || value > 1.0f) {
        fail("fog color component is nonfinite or out of range");
    }
}

struct Arithmetic {
    NnFogPrecision precision;

    double rounded(double value) const {
        if (precision == NnFogPrecision::Double || !std::isfinite(value)) {
            return value;
        }
        int exponent = 0;
        const float mantissa = static_cast<float>(std::frexp(value, &exponent));
        return std::ldexp(static_cast<double>(mantissa), exponent);
    }

    double add(double left, double right) const {
        return rounded(left + right);
    }

    double subtract(double left, double right) const {
        return rounded(left - right);
    }

    double multiply(double left, double right) const {
        return rounded(left * right);
    }

    double divide(double left, double right) const {
        return rounded(left / right);
    }
};

float store_finite(double value, const char* message) {
    const float stored = static_cast<float>(value);
    if (!std::isfinite(stored)) {
        fail(message);
    }
    return stored;
}

double checked_delta(float near_distance, float far_distance, const Arithmetic& arithmetic) {
    require_finite_bounded(near_distance, "fog near distance is nonfinite or out of range");
    require_finite_bounded(far_distance, "fog far distance is nonfinite or out of range");
    const double delta = arithmetic.subtract(
        static_cast<double>(far_distance),
        static_cast<double>(near_distance));
    if (delta == 0.0) {
        fail("fog range delta is zero");
    }
    return delta;
}

struct FogCoefficients {
    float a;
    float b;
};

FogCoefficients calculate_coefficients(
    float near_distance,
    double delta,
    const Arithmetic& arithmetic) {
    return {
        store_finite(arithmetic.divide(-1.0, delta), "fog coefficient is nonfinite"),
        store_finite(
            arithmetic.divide(static_cast<double>(near_distance), delta),
            "fog coefficient is nonfinite"),
    };
}

FogCoefficients calculate_coefficients_for_state(const NnFogState& state, const Arithmetic& arithmetic) {
    const double delta = checked_delta(state.range_near, state.range_far, arithmetic);
    return calculate_coefficients(state.range_near, delta, arithmetic);
}

std::uint32_t pack_color_component(float value, const Arithmetic& arithmetic) {
    const double scaled = arithmetic.multiply(static_cast<double>(value), 255.0);
    return static_cast<std::uint32_t>(std::trunc(scaled));
}

}

void nn_set_fog_request(NnFogState& state, bool requested, NnFogPrecision precision) {
    require_supported_precision(precision);
    const Arithmetic arithmetic{precision};
    if (!requested) {
        state.requested = 0u;
        state.coefficient_a = 0.0f;
        state.coefficient_b = 0.0f;
        return;
    }

    const double delta = checked_delta(state.range_near, state.range_far, arithmetic);
    const float coefficient_b = store_finite(
        arithmetic.divide(static_cast<double>(state.range_near), delta),
        "fog coefficient is nonfinite");
    state.requested = 1u;
    state.coefficient_b = coefficient_b;
}

void nn_set_fog_color(
    NnFogState& state,
    const std::array<float, 3u>& rgb,
    NnFogPrecision precision) {
    require_supported_precision(precision);
    require_color_component(rgb[0u]);
    require_color_component(rgb[1u]);
    require_color_component(rgb[2u]);

    const Arithmetic arithmetic{precision};
    const std::uint32_t red = pack_color_component(rgb[0u], arithmetic);
    const std::uint32_t green = pack_color_component(rgb[1u], arithmetic);
    const std::uint32_t blue = pack_color_component(rgb[2u], arithmetic);
    const std::uint32_t packed_argb =
        0xff000000u | (red << 16u) | (green << 8u) | blue;

    if (state.requested != 0u) {
        const FogCoefficients coefficients = calculate_coefficients_for_state(state, arithmetic);
        state.color[0u] = rgb[0u];
        state.color[1u] = rgb[1u];
        state.color[2u] = rgb[2u];
        state.packed_argb = packed_argb;
        state.coefficient_a = coefficients.a;
        state.coefficient_b = coefficients.b;
        return;
    }

    state.color[0u] = rgb[0u];
    state.color[1u] = rgb[1u];
    state.color[2u] = rgb[2u];
    state.packed_argb = packed_argb;
}

void nn_set_fog_range(
    NnFogState& state,
    float near_distance,
    float far_distance,
    NnFogPrecision precision) {
    require_supported_precision(precision);
    const Arithmetic arithmetic{precision};
    const double delta = checked_delta(near_distance, far_distance, arithmetic);
    const float scale = store_finite(
        arithmetic.divide(1.0, delta),
        "fog scale is nonfinite");

    if (state.requested != 0u) {
        const FogCoefficients coefficients = calculate_coefficients(near_distance, delta, arithmetic);
        state.range_near = near_distance;
        state.range_far = far_distance;
        state.configured.start = near_distance;
        state.configured.end = far_distance;
        state.configured.scale = scale;
        state.mode = 3u;
        state.coefficient_a = coefficients.a;
        state.coefficient_b = coefficients.b;
        return;
    }

    state.range_near = near_distance;
    state.range_far = far_distance;
    state.configured.start = near_distance;
    state.configured.end = far_distance;
    state.configured.scale = scale;
    state.mode = 3u;
}

void nn_apply_fog(
    NnFogState& state,
    bool enabled,
    float fallback_distance,
    NnFogPrecision precision) {
    require_supported_precision(precision);
    const Arithmetic arithmetic{precision};
    if (enabled) {
        require_finite(state.configured.density, "fog density is nonfinite");
        require_finite(state.configured.start, "fog start is nonfinite");
        require_finite(state.configured.end, "fog end is nonfinite");
        require_finite(state.configured.scale, "fog scale is nonfinite");
        const NnFogParameters active = state.configured;
        state.active = active;
        state.effective = 1u;
        return;
    }

    require_finite_bounded(fallback_distance, "fog fallback distance is nonfinite or out of range");
    const float fallback_end = store_finite(
        arithmetic.add(static_cast<double>(fallback_distance), 1.0),
        "fog fallback end is nonfinite");
    state.active.density = 0.0f;
    state.active.start = fallback_distance;
    state.active.end = fallback_end;
    state.active.scale = 1.0f;
    state.effective = 0u;
}

