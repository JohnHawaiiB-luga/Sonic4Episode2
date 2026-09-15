#include "nn_quaternion.h"

#include <array>
#include <cstddef>
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

bool same_quaternion(const NnQuaternion& left, const NnQuaternion& right) {
    for (std::size_t index = 0u; index < left.size(); ++index) {
        if (float_bits(left[index]) != float_bits(right[index])) {
            return false;
        }
    }
    return true;
}

bool quaternion_has_bits(
    const NnQuaternion& quaternion,
    const std::array<std::uint32_t, 4u>& expected) {
    for (std::size_t index = 0u; index < quaternion.size(); ++index) {
        if (float_bits(quaternion[index]) != expected[index]) {
            return false;
        }
    }
    return true;
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

bool test_identity_and_unit_axes() {
    const std::array<std::int32_t, 3u> identity_rotation = {{0, 0, 0}};
    const std::array<std::int32_t, 3u> x_rotation = {{16384, 0, 0}};
    const std::array<std::int32_t, 3u> y_rotation = {{0, 16384, 0}};
    const std::array<std::int32_t, 3u> z_rotation = {{0, 0, 16384}};

    for (CameraPrecision precision : {CameraPrecision::Single, CameraPrecision::Double}) {
        const NnQuaternion identity = nn_quaternion_xyz(identity_rotation, precision);
        const NnQuaternion x = nn_quaternion_xyz(x_rotation, precision);
        const NnQuaternion y = nn_quaternion_xyz(y_rotation, precision);
        const NnQuaternion z = nn_quaternion_xyz(z_rotation, precision);
        if (!check(
                float_bits(identity[0u]) == 0x00000000u &&
                    float_bits(identity[1u]) == 0x00000000u &&
                    float_bits(identity[2u]) == 0x00000000u &&
                    float_bits(identity[3u]) == 0x3f800000u,
                "zero XYZ rotation did not produce the identity quaternion") ||
            !check(
                x[0u] > 0.0f && x[3u] > 0.0f &&
                    float_bits(x[1u]) == 0x00000000u && float_bits(x[2u]) == 0x00000000u,
                "X-only rotation did not remain on the X quaternion axis") ||
            !check(
                y[1u] > 0.0f && y[3u] > 0.0f &&
                    float_bits(y[0u]) == 0x00000000u && float_bits(y[2u]) == 0x00000000u,
                "Y-only rotation did not remain on the Y quaternion axis") ||
            !check(
                z[2u] > 0.0f && z[3u] > 0.0f &&
                    float_bits(z[0u]) == 0x00000000u && float_bits(z[1u]) == 0x00000000u,
                "Z-only rotation did not remain on the Z quaternion axis")) {
            return false;
        }
    }
    return true;
}

bool test_negative_odd_half_angle_and_minimum_input() {
    const std::array<std::int32_t, 3u> negative_odd = {{-1, 0, 0}};
    const std::array<std::int32_t, 3u> negative_even = {{-2, 0, 0}};
    const std::array<std::int32_t, 3u> identity_rotation = {{0, 0, 0}};
    const std::array<std::int32_t, 3u> minimum_rotation = {{
        std::numeric_limits<std::int32_t>::min(),
        0,
        0,
    }};

    for (CameraPrecision precision : {CameraPrecision::Single, CameraPrecision::Double}) {
        const NnQuaternion odd = nn_quaternion_xyz(negative_odd, precision);
        const NnQuaternion even = nn_quaternion_xyz(negative_even, precision);
        const NnQuaternion identity = nn_quaternion_xyz(identity_rotation, precision);
        const NnQuaternion minimum = nn_quaternion_xyz(minimum_rotation, precision);
        if (!check(
                same_quaternion(odd, even),
                "negative odd rotation did not use arithmetic half-angle rounding") ||
            !check(
                same_quaternion(minimum, identity),
                "minimum signed rotation did not narrow through the low half-angle bits")) {
            return false;
        }
    }
    return expects_invalid_argument(
        []() {
            const std::array<std::int32_t, 3u> rotation = {{0, 0, 0}};
            static_cast<void>(nn_quaternion_xyz(
                rotation,
                static_cast<CameraPrecision>(99)));
        },
        "XYZ quaternion accepted an unsupported precision");
}

bool test_original_smoke_goldens() {
    const std::array<std::int32_t, 3u> x_axis = {{16384, 0, 0}};
    const std::array<std::int32_t, 3u> negative_odd = {{-1, 0, 0}};
    const std::array<std::int32_t, 3u> extrema = {{
        std::numeric_limits<std::int32_t>::min(),
        std::numeric_limits<std::int32_t>::max(),
        -1,
    }};
    const std::array<std::int32_t, 3u> precision_witness = {{16203, -185, -14403}};
    const std::array<std::uint32_t, 4u> x_axis_expected = {{
        1060439287u, 0u, 0u, 1060439287u,
    }};
    const std::array<std::uint32_t, 4u> negative_odd_expected = {{
        3100184704u, 0u, 0u, 1065353215u,
    }};
    const std::array<std::uint32_t, 4u> extrema_expected = {{
        2988305130u, 3100184703u, 3100184703u, 1065353214u,
    }};
    const std::array<std::uint32_t, 4u> single_precision_expected = {{
        1057573411u, 3202814228u, 3202751712u, 1057867721u,
    }};
    const std::array<std::uint32_t, 4u> double_precision_expected = {{
        1057573411u, 3202814228u, 3202751713u, 1057867721u,
    }};

    for (CameraPrecision precision : {CameraPrecision::Single, CameraPrecision::Double}) {
        if (!check(
                quaternion_has_bits(nn_quaternion_xyz(x_axis, precision), x_axis_expected),
                "X-axis quaternion original smoke golden mismatch") ||
            !check(
                quaternion_has_bits(
                    nn_quaternion_xyz(negative_odd, precision),
                    negative_odd_expected),
                "negative odd quaternion original smoke golden mismatch") ||
            !check(
                quaternion_has_bits(nn_quaternion_xyz(extrema, precision), extrema_expected),
                "extreme rotation quaternion original smoke golden mismatch")) {
            return false;
        }
    }

    return check(
        quaternion_has_bits(
            nn_quaternion_xyz(precision_witness, CameraPrecision::Single),
            single_precision_expected),
        "single-precision quaternion original smoke golden mismatch") &&
        check(
            quaternion_has_bits(
                nn_quaternion_xyz(precision_witness, CameraPrecision::Double),
                double_precision_expected),
            "double-precision quaternion original smoke golden mismatch");
}

}

int main() {
    return test_identity_and_unit_axes() &&
                   test_negative_odd_half_angle_and_minimum_input() &&
                   test_original_smoke_goldens()
        ? 0
        : 1;
}
