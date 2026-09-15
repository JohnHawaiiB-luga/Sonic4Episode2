#include "camera_view.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <limits>
#include <optional>

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

std::uint32_t float_bits(const float& value) {
    std::uint32_t bits = 0u;
    std::memcpy(&bits, &value, sizeof(bits));
    return bits;
}

bool check_bits(float actual, std::uint32_t expected, const char* message) {
    return check(float_bits(actual) == expected, message);
}

bool check_vector_bits(
    const std::array<float, 3u>& actual,
    const std::array<std::uint32_t, 3u>& expected,
    const char* message) {
    for (std::size_t index = 0u; index < actual.size(); ++index) {
        if (!check(float_bits(actual[index]) == expected[index], message)) {
            return false;
        }
    }
    return true;
}

bool test_inline_target_and_raw_scalar_bits() {
    const std::array<std::uint32_t, 3u> eye_bits = {{0x80000000u, 0x7f800000u, 0xff800000u}};
    const std::array<std::uint32_t, 3u> inline_target_bits = {{0x7fa12345u, 0x00000000u, 0x80000000u}};
    CameraViewInput input = {
        std::numeric_limits<std::int32_t>::min(),
        float_from_bits(0x80000000u),
        float_from_bits(0x7f800000u),
        float_from_bits(0x7fa54321u),
        {float_from_bits(eye_bits[0u]), float_from_bits(eye_bits[1u]), float_from_bits(eye_bits[2u])},
        {float_from_bits(inline_target_bits[0u]),
         float_from_bits(inline_target_bits[1u]),
         float_from_bits(inline_target_bits[2u])},
        std::nullopt,
        std::numeric_limits<std::int32_t>::max(),
    };

    const CameraViewParameters output = make_camera_view_parameters(input);
    bool passed = true;
    passed = check(output.leading_word == 0u, "view leading word was not zero") && passed;
    passed = check(output.fov_angle == std::numeric_limits<std::int32_t>::min(),
                   "view FOV signed boundary changed") &&
             passed;
    passed = check(output.roll_angle == std::numeric_limits<std::int32_t>::max(),
                   "view roll signed boundary changed") &&
             passed;
    passed = check_bits(output.aspect, 0x80000000u, "view aspect negative zero changed") && passed;
    passed = check_bits(output.near_plane, 0x7f800000u, "view near infinity changed") && passed;
    passed = check_bits(output.far_plane, 0x7fa54321u, "view far NaN payload changed") && passed;
    passed = check_vector_bits(output.eye, eye_bits, "view eye float bits changed") && passed;
    passed = check_vector_bits(output.target, inline_target_bits, "view inline target float bits changed") && passed;
    return passed;
}

bool test_linked_target_overrides_inline_without_mutation() {
    const std::array<std::uint32_t, 3u> inline_target_bits = {{0x3f800000u, 0x40000000u, 0x40400000u}};
    const std::array<std::uint32_t, 3u> linked_target_bits = {{0x80000000u, 0xff800000u, 0x7fa00001u}};
    CameraViewInput input = {
        -1,
        float_from_bits(0x3f800000u),
        float_from_bits(0x3f000000u),
        float_from_bits(0x40800000u),
        {float_from_bits(0x3f800000u), float_from_bits(0x40000000u), float_from_bits(0x40400000u)},
        {float_from_bits(inline_target_bits[0u]),
         float_from_bits(inline_target_bits[1u]),
         float_from_bits(inline_target_bits[2u])},
        std::array<float, 3u>{{float_from_bits(linked_target_bits[0u]),
                               float_from_bits(linked_target_bits[1u]),
                               float_from_bits(linked_target_bits[2u])}},
        -32768,
    };
    const std::array<std::uint32_t, 3u> input_inline_before = {{
        float_bits(input.inline_target[0u]),
        float_bits(input.inline_target[1u]),
        float_bits(input.inline_target[2u]),
    }};
    const std::array<std::uint32_t, 3u> input_linked_before = {{
        float_bits((*input.linked_target)[0u]),
        float_bits((*input.linked_target)[1u]),
        float_bits((*input.linked_target)[2u]),
    }};

    const CameraViewParameters output = make_camera_view_parameters(input);
    bool passed = true;
    passed = check(output.leading_word == 0u, "linked view leading word changed") && passed;
    passed = check(output.fov_angle == -1 && output.roll_angle == -32768,
                   "linked view signed words changed") &&
             passed;
    passed = check_vector_bits(output.target, linked_target_bits, "linked target did not override inline target") && passed;
    passed = check_vector_bits(input.inline_target, input_inline_before, "inline target input changed") && passed;
    passed = check_vector_bits(*input.linked_target, input_linked_before, "linked target input changed") && passed;
    return passed;
}

}

int main() {
    return test_inline_target_and_raw_scalar_bits() && test_linked_target_overrides_inline_without_mutation() ? 0 : 1;
}
