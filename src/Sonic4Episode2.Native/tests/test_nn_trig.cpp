#include "nn_trig.h"

#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <exception>
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

std::uint32_t float_bits(float value) {
    std::uint32_t bits = 0u;
    std::memcpy(&bits, &value, sizeof(bits));
    return bits;
}

template <typename Callable>
bool expects_invalid_argument(Callable&& callable, const char* message) {
    try {
        std::forward<Callable>(callable)();
    } catch (const std::invalid_argument&) {
        return true;
    } catch (const std::exception&) {
        return check(false, message);
    }
    return check(false, message);
}

bool test_axes_and_signed_zeroes() {
    struct AxisCase {
        std::uint16_t angle;
        std::uint32_t sine_bits;
        std::uint32_t cosine_bits;
    };

    const std::array<AxisCase, 4u> cases = {{
        {0u, 0x00000000u, 0x3F800000u},
        {16384u, 0x3F800000u, 0x80000000u},
        {32768u, 0x80000000u, 0xBF800000u},
        {49152u, 0xBF800000u, 0x00000000u},
    }};

    for (const AxisCase& expected : cases) {
        if (!check(float_bits(nn_sin(expected.angle)) == expected.sine_bits, "sine axis result mismatch") ||
            !check(float_bits(nn_cos(expected.angle)) == expected.cosine_bits, "cosine axis result mismatch")) {
            return false;
        }
    }
    return true;
}

bool test_combined_axes_quadrants_and_precision_validation() {
    struct AxisCase {
        std::uint16_t angle;
        std::uint32_t sine_bits;
        std::uint32_t cosine_bits;
    };

    const std::array<AxisCase, 4u> axes = {{
        {0u, 0x00000000u, 0x3f800000u},
        {16384u, 0x3f800000u, 0x80000000u},
        {32768u, 0x80000000u, 0xbf800000u},
        {49152u, 0xbf800000u, 0x00000000u},
    }};
    for (CameraPrecision precision : {CameraPrecision::Single, CameraPrecision::Double}) {
        for (const AxisCase& expected : axes) {
            const NnSinCos result = nn_sin_cos(expected.angle, precision);
            if (!check(
                    float_bits(result.sine) == expected.sine_bits &&
                        float_bits(result.cosine) == expected.cosine_bits,
                    "combined sine/cosine axis result mismatch")) {
                return false;
            }
        }

        const NnSinCos first = nn_sin_cos(0x1234u, precision);
        const NnSinCos second = nn_sin_cos(0x5234u, precision);
        const NnSinCos third = nn_sin_cos(0x9234u, precision);
        const NnSinCos fourth = nn_sin_cos(0xd234u, precision);
        if (!check(
                float_bits(second.sine) == float_bits(first.cosine) &&
                    float_bits(second.cosine) == float_bits(-first.sine) &&
                    float_bits(third.sine) == float_bits(-first.sine) &&
                    float_bits(third.cosine) == float_bits(-first.cosine) &&
                    float_bits(fourth.sine) == float_bits(-first.cosine) &&
                    float_bits(fourth.cosine) == float_bits(first.sine),
                "combined sine/cosine quadrant mapping mismatch")) {
            return false;
        }
    }

    return expects_invalid_argument(
        []() { static_cast<void>(nn_sin_cos(0u, static_cast<CameraPrecision>(99))); },
        "combined sine/cosine accepted an unsupported precision");
}

bool test_combined_original_smoke_goldens() {
    struct Golden {
        std::uint16_t angle;
        std::uint32_t sine_bits;
        std::uint32_t cosine_bits;
    };

    const std::array<Golden, 6u> goldens = {{
        {1u, 952701056u, 1065353215u},
        {15u, 985431928u, 1065353200u},
        {16u, 986255488u, 1065353199u},
        {16383u, 1065353215u, 952701056u},
        {16385u, 1065353215u, 3100184704u},
        {65535u, 3100184704u, 1065353215u},
    }};

    for (CameraPrecision precision : {CameraPrecision::Single, CameraPrecision::Double}) {
        for (const Golden& expected : goldens) {
            const NnSinCos result = nn_sin_cos(expected.angle, precision);
            if (!check(
                    float_bits(result.sine) == expected.sine_bits &&
                        float_bits(result.cosine) == expected.cosine_bits,
                    "combined sine/cosine original smoke golden mismatch")) {
                return false;
            }
        }
    }
    return true;
}

bool test_quadrant_reflection() {
    constexpr std::uint16_t angle = 0x1234u;
    const float base = nn_sin(angle);
    const std::uint16_t reflected = static_cast<std::uint16_t>(0x8000u - angle);
    const std::uint16_t opposite = static_cast<std::uint16_t>(0x8000u + angle);
    const std::uint16_t wrapped = static_cast<std::uint16_t>(0u - angle);

    return check(float_bits(nn_sin(reflected)) == float_bits(base), "first/second quadrant reflection mismatch") &&
           check(float_bits(nn_sin(opposite)) == float_bits(-base), "third quadrant reflection mismatch") &&
           check(float_bits(nn_sin(wrapped)) == float_bits(-base), "fourth quadrant reflection mismatch");
}

bool test_cosine_phase_and_wrap() {
    const std::array<std::uint16_t, 10u> angles = {{
        0u,
        1u,
        15u,
        16u,
        0x1234u,
        0x3FFFu,
        0x4000u,
        0x7FFFu,
        0x8000u,
        0xFFFFu,
    }};

    for (const std::uint16_t angle : angles) {
        const std::uint16_t quarter_turn_later = static_cast<std::uint16_t>(angle + 16384u);
        if (!check(float_bits(nn_cos(angle)) == float_bits(nn_sin(quarter_turn_later)), "cosine phase mismatch")) {
            return false;
        }
    }
    return check(float_bits(nn_cos(0xFFFFu)) == float_bits(nn_sin(0x3FFFu)), "cosine wrap mismatch");
}

bool test_between_sample_interpolation() {
    const float at_zero = nn_sin(0u);
    const float halfway = nn_sin(8u);
    const float at_next_sample = nn_sin(16u);
    const float reflected_halfway = nn_sin(0x4008u);
    const float reflected_previous = nn_sin(0x3FF8u);

    return check(float_bits(halfway) == float_bits(0.000767f), "halfway interpolation result mismatch") &&
           check(halfway > at_zero && halfway < at_next_sample, "halfway interpolation collapsed to a table endpoint") &&
           check(float_bits(reflected_halfway) == float_bits(reflected_previous), "odd quadrant interpolation mismatch");
}

bool test_all_angles_are_finite_and_bounded() {
    for (std::uint32_t raw_angle = 0u; raw_angle <= std::numeric_limits<std::uint16_t>::max(); ++raw_angle) {
        const std::uint16_t angle = static_cast<std::uint16_t>(raw_angle);
        const float sine = nn_sin(angle);
        const float cosine = nn_cos(angle);
        if (!std::isfinite(sine) || !std::isfinite(cosine) || std::fabs(sine) > 1.0f || std::fabs(cosine) > 1.0f) {
            std::fprintf(stderr, "non-finite or out-of-range trigonometric result at %u\n", raw_angle);
            return false;
        }
    }
    return true;
}

}

int main() {
    return test_axes_and_signed_zeroes() &&
                   test_combined_axes_quadrants_and_precision_validation() &&
                   test_combined_original_smoke_goldens() &&
                   test_quadrant_reflection() &&
                   test_cosine_phase_and_wrap() &&
                   test_between_sample_interpolation() &&
                   test_all_angles_are_finite_and_bounded()
               ? 0
               : 1;
}
