#include "nn_quaternion.h"

#include <cmath>
#include <cstdint>
#include <stdexcept>

namespace {

constexpr double kNearUnityDot = 0.9999899864196777;
constexpr double kSqrtEight = 2.8284270763397217;

void require_precision(CameraPrecision precision) {
    if (precision != CameraPrecision::Single && precision != CameraPrecision::Double) {
        throw std::invalid_argument("quaternion precision is unsupported");
    }
}

struct Arithmetic {
    CameraPrecision precision;

    double rounded(double value) const {
        if (precision == CameraPrecision::Double || !std::isfinite(value)) {
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

    float spill(double value) const {
        return static_cast<float>(value);
    }
};

std::uint16_t arithmetic_half_angle(std::int32_t rotation) {
    const std::uint32_t bits = static_cast<std::uint32_t>(rotation);
    const std::uint32_t shifted = (bits >> 1u) |
        ((bits & 0x80000000u) == 0u ? 0u : 0x80000000u);
    return static_cast<std::uint16_t>(shifted & 0xffffu);
}

NnSinCos sin_cos_for_rotation(std::int32_t rotation, CameraPrecision precision) {
    if (rotation == 0) {
        return {0.0f, 1.0f};
    }
    return nn_sin_cos(arithmetic_half_angle(rotation), precision);
}

float inverse_square_root(float value, const Arithmetic& arithmetic) {
    const double root = arithmetic.rounded(std::sqrt(static_cast<double>(value)));
    return arithmetic.spill(arithmetic.rounded(1.0 / root));
}

float weighted_component(
    float primary,
    float secondary,
    float primary_weight,
    float secondary_weight,
    const Arithmetic& arithmetic) {
    const double primary_product = arithmetic.multiply(
        static_cast<double>(primary), static_cast<double>(primary_weight));
    const double secondary_product = arithmetic.multiply(
        static_cast<double>(secondary), static_cast<double>(secondary_weight));
    return arithmetic.spill(arithmetic.add(primary_product, secondary_product));
}

}

NnQuaternion nn_quaternion_multiply(
    const NnQuaternion& left,
    const NnQuaternion& right,
    CameraPrecision precision) {
    require_precision(precision);
    const Arithmetic arithmetic{precision};
    return {{
        arithmetic.spill(arithmetic.subtract(arithmetic.add(arithmetic.add(
            arithmetic.multiply(right[0u], left[3u]), arithmetic.multiply(right[3u], left[0u])),
            arithmetic.multiply(right[2u], left[1u])), arithmetic.multiply(right[1u], left[2u]))),
        arithmetic.spill(arithmetic.add(arithmetic.add(arithmetic.subtract(
            arithmetic.multiply(right[1u], left[3u]), arithmetic.multiply(right[2u], left[0u])),
            arithmetic.multiply(right[3u], left[1u])), arithmetic.multiply(right[0u], left[2u]))),
        arithmetic.spill(arithmetic.add(arithmetic.subtract(arithmetic.add(
            arithmetic.multiply(right[2u], left[3u]), arithmetic.multiply(right[1u], left[0u])),
            arithmetic.multiply(right[0u], left[1u])), arithmetic.multiply(right[3u], left[2u]))),
        arithmetic.spill(arithmetic.subtract(arithmetic.subtract(arithmetic.subtract(
            arithmetic.multiply(left[3u], right[3u]), arithmetic.multiply(left[0u], right[0u])),
            arithmetic.multiply(left[1u], right[1u])), arithmetic.multiply(left[2u], right[2u]))),
    }};
}

NnQuaternion nn_quaternion_xyz(
    const std::array<std::int32_t, 3u>& rotation,
    CameraPrecision precision) {
    require_precision(precision);

    const NnSinCos x_rotation = sin_cos_for_rotation(rotation[0u], precision);
    const NnSinCos y_rotation = sin_cos_for_rotation(rotation[1u], precision);
    const NnSinCos z_rotation = sin_cos_for_rotation(rotation[2u], precision);
    const float sx = x_rotation.sine;
    const float cx = x_rotation.cosine;
    const float sy = y_rotation.sine;
    const float cy = y_rotation.cosine;
    const float sz = z_rotation.sine;
    const float cz = z_rotation.cosine;
    const Arithmetic arithmetic{precision};

    const float a = arithmetic.spill(arithmetic.multiply(
        static_cast<double>(cy), static_cast<double>(cz)));
    const float b = arithmetic.spill(arithmetic.multiply(
        static_cast<double>(sy), static_cast<double>(cz)));
    const float c = arithmetic.spill(arithmetic.multiply(
        static_cast<double>(sy), static_cast<double>(sz)));
    const float d = arithmetic.spill(arithmetic.multiply(
        static_cast<double>(cy), static_cast<double>(sz)));

    const double sx_a = arithmetic.multiply(static_cast<double>(sx), static_cast<double>(a));
    const double cx_c = arithmetic.multiply(static_cast<double>(cx), static_cast<double>(c));
    const float x = arithmetic.spill(arithmetic.subtract(sx_a, cx_c));

    const double cx_b = arithmetic.multiply(static_cast<double>(cx), static_cast<double>(b));
    const double sx_d = arithmetic.multiply(static_cast<double>(sx), static_cast<double>(d));
    const float y = arithmetic.spill(arithmetic.add(cx_b, sx_d));

    const double cx_d = arithmetic.multiply(static_cast<double>(cx), static_cast<double>(d));
    const double sx_b = arithmetic.multiply(static_cast<double>(sx), static_cast<double>(b));
    const float z = arithmetic.spill(arithmetic.subtract(cx_d, sx_b));

    const double cx_a = arithmetic.multiply(static_cast<double>(cx), static_cast<double>(a));
    const double sx_c = arithmetic.multiply(static_cast<double>(sx), static_cast<double>(c));
    const float w = arithmetic.spill(arithmetic.add(cx_a, sx_c));
    return {{x, y, z, w}};
}

NnQuaternion nn_quaternion_slerp(
    const NnQuaternion& primary,
    const NnQuaternion& secondary,
    float weight,
    CameraPrecision precision) {
    require_precision(precision);

    const Arithmetic arithmetic{precision};
    const float one_minus_weight = arithmetic.spill(arithmetic.subtract(
        1.0, static_cast<double>(weight)));

    double dot_value = arithmetic.multiply(
        static_cast<double>(primary[0u]), static_cast<double>(secondary[0u]));
    dot_value = arithmetic.add(dot_value, arithmetic.multiply(
        static_cast<double>(primary[1u]), static_cast<double>(secondary[1u])));
    dot_value = arithmetic.add(dot_value, arithmetic.multiply(
        static_cast<double>(primary[2u]), static_cast<double>(secondary[2u])));
    dot_value = arithmetic.add(dot_value, arithmetic.multiply(
        static_cast<double>(primary[3u]), static_cast<double>(secondary[3u])));
    float dot = arithmetic.spill(dot_value);
    float sign = 1.0f;
    if (dot < 0.0f) {
        dot = arithmetic.spill(-static_cast<double>(dot));
        sign = -1.0f;
    }

    float primary_weight = 0.0f;
    float secondary_weight = 0.0f;
    if (static_cast<double>(dot) < kNearUnityDot) {
        const float inverse = inverse_square_root(arithmetic.spill(arithmetic.add(
            1.0, static_cast<double>(dot))), arithmetic);
        const float adjustment = arithmetic.spill(arithmetic.subtract(
            2.0,
            arithmetic.multiply(static_cast<double>(inverse), kSqrtEight)));
        const float u = arithmetic.spill(arithmetic.multiply(
            static_cast<double>(adjustment), static_cast<double>(weight)));
        const float v0 = arithmetic.spill(arithmetic.multiply(
            static_cast<double>(one_minus_weight),
            arithmetic.subtract(1.0, static_cast<double>(u))));
        const float v1 = arithmetic.spill(arithmetic.subtract(
            static_cast<double>(weight),
            arithmetic.multiply(static_cast<double>(u), static_cast<double>(one_minus_weight))));
        const double squared_sum = arithmetic.add(
            arithmetic.multiply(static_cast<double>(v0), static_cast<double>(v0)),
            arithmetic.multiply(static_cast<double>(v1), static_cast<double>(v1)));
        const double cross_term = arithmetic.multiply(
            arithmetic.multiply(
                arithmetic.multiply(2.0, static_cast<double>(v0)),
                static_cast<double>(v1)),
            static_cast<double>(dot));
        const float normalizer = inverse_square_root(arithmetic.spill(arithmetic.add(
            squared_sum, cross_term)), arithmetic);
        primary_weight = arithmetic.spill(arithmetic.multiply(
            static_cast<double>(v0), static_cast<double>(normalizer)));
        secondary_weight = arithmetic.spill(arithmetic.multiply(
            arithmetic.multiply(static_cast<double>(v1), static_cast<double>(normalizer)),
            static_cast<double>(sign)));
    } else {
        primary_weight = one_minus_weight;
        secondary_weight = arithmetic.spill(arithmetic.multiply(
            static_cast<double>(weight), static_cast<double>(sign)));
    }

    return {{
        weighted_component(primary[0u], secondary[0u], primary_weight, secondary_weight, arithmetic),
        weighted_component(primary[1u], secondary[1u], primary_weight, secondary_weight, arithmetic),
        weighted_component(primary[2u], secondary[2u], primary_weight, secondary_weight, arithmetic),
        weighted_component(primary[3u], secondary[3u], primary_weight, secondary_weight, arithmetic),
    }};
}
