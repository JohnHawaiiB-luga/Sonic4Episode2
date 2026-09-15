#include "nn_motion_frame.h"

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

constexpr std::uint32_t kBypass = 0x40u;
constexpr std::uint32_t kInterval = 0x10000u;
constexpr std::uint32_t kClamp = 0x20000u;
constexpr std::uint32_t kRepeat = 0x40000u;
constexpr std::uint32_t kMirror = 0x80000u;
constexpr std::uint32_t kLinearFlags = 0x2u;

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

bool check_result(
    const NnMotionFrameResult& result,
    std::uint32_t expected_frame_bits,
    bool expected_active,
    const char* message) {
    return check(
        float_bits(result.frame) == expected_frame_bits && result.active == expected_active,
        message);
}

template <std::size_t Count>
bool same_keys(
    const std::array<NnMotionA16Key, Count>& left,
    const std::array<NnMotionA16Key, Count>& right) {
    for (std::size_t index = 0u; index < Count; ++index) {
        if (left[index].frame != right[index].frame || left[index].value != right[index].value) {
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

bool test_default_bypass_interval_and_clamp_modes() {
    constexpr float start = 0.0f;
    constexpr float end = 60.0f;
    constexpr float before_start = -240.0f;

    for (CameraPrecision precision : {CameraPrecision::Single, CameraPrecision::Double}) {
        if (!check_result(
                nn_map_motion_frame(0u, start, end, before_start, precision),
                float_bits(before_start),
                false,
                "default motion frame mode changed the input") ||
            !check_result(
                nn_map_motion_frame(kBypass, start, end, before_start, precision),
                float_bits(before_start),
                true,
                "bypass motion frame mode was not active") ||
            !check_result(
                nn_map_motion_frame(kInterval, start, end, before_start, precision),
                float_bits(before_start),
                false,
                "interval mode accepted a frame before start") ||
            !check_result(
                nn_map_motion_frame(kClamp, start, end, before_start, precision),
                float_bits(start),
                true,
                "clamp mode did not clamp a frame before start") ||
            !check_result(
                nn_map_motion_frame(kInterval, start, end, end, precision),
                float_bits(end),
                false,
                "interval mode accepted its exclusive end") ||
            !check_result(
                nn_map_motion_frame(kClamp, start, end, end, precision),
                float_bits(end),
                true,
                "clamp mode did not retain its end") ||
            !check_result(
                nn_map_motion_frame(0x30000u, start, end, before_start, precision),
                float_bits(before_start),
                false,
                "unknown motion frame mode was active") ||
            !check_result(
                nn_map_motion_frame(kBypass | kRepeat, start, end, before_start, precision),
                float_bits(before_start),
                true,
                "bypass did not precede the cyclic mode")) {
            return false;
        }
    }
    return true;
}

bool test_cyclic_modes_preserve_negative_cycle_and_signed_zero_rules() {
    constexpr float start = 0.0f;
    constexpr float end = 60.0f;
    const float negative_zero = float_from_bits(0x80000000u);

    for (CameraPrecision precision : {CameraPrecision::Single, CameraPrecision::Double}) {
        if (!check_result(
                nn_map_motion_frame(kRepeat, start, end, -60.0f, precision),
                float_bits(end),
                true,
                "repeat mode did not decrement an exact negative cycle") ||
            !check_result(
                nn_map_motion_frame(kRepeat, start, end, 70.0f, precision),
                float_bits(10.0f),
                true,
                "repeat mode did not retain its wrapped remainder") ||
            !check_result(
                nn_map_motion_frame(kMirror, start, end, 70.0f, precision),
                float_bits(50.0f),
                true,
                "mirror mode did not reflect an odd cycle") ||
            !check_result(
                nn_map_motion_frame(kMirror, start, end, -60.0f, precision),
                float_bits(end),
                true,
                "mirror mode did not retain the adjusted even negative cycle") ||
            !check_result(
                nn_map_motion_frame(kInterval, start, end, negative_zero, precision),
                0x80000000u,
                true,
                "interval mode lost a signed zero") ||
            !check_result(
                nn_map_motion_frame(kRepeat, start, end, negative_zero, precision),
                0x80000000u,
                true,
                "repeat mode lost an in-range signed zero")) {
            return false;
        }
    }
    return true;
}

bool test_mirror_maps_in_range_precision_cancellation_cases() {
    const float just_below_one = float_from_bits(0x3f7fffffu);

    return check_result(
               nn_map_motion_frame(
                   kMirror,
                   -10000000000.0f,
                   1.0f,
                   0.5f,
                   CameraPrecision::Single),
               float_bits(0.0f),
               true,
               "single precision mirror bypassed an in-range cancellation") &&
           check_result(
               nn_map_motion_frame(
                   kMirror,
                   -10000000000.0f,
                   1.0f,
                   0.5f,
                   CameraPrecision::Double),
               float_bits(0.0f),
               true,
               "double precision mirror bypassed an in-range cancellation") &&
           check_result(
               nn_map_motion_frame(
                   kMirror,
                   -8388608.0f,
                   1.0f,
                   just_below_one,
                   CameraPrecision::Single),
               float_bits(2.0f),
               true,
               "single precision mirror did not preserve in-range cycle rounding") &&
           check_result(
               nn_map_motion_frame(
                   kMirror,
                   -8388608.0f,
                   1.0f,
                   just_below_one,
                   CameraPrecision::Double),
               float_bits(1.0f),
               true,
               "double precision mirror did not preserve in-range cycle rounding");
}

bool test_repeat_direct_fimul_precision_control() {
    const float length = float_from_bits(0x3ff85841u);
    constexpr float frame = -1885890048.0f;

    return check_result(
               nn_map_motion_frame(kRepeat, 0.0f, length, frame, CameraPrecision::Single),
               0x43000000u,
               true,
               "single precision repeat did not use direct FIMUL rounding") &&
           check_result(
               nn_map_motion_frame(kRepeat, 0.0f, length, frame, CameraPrecision::Double),
               0x42800000u,
               true,
               "double precision repeat FIMUL control differed");
}

bool test_rejects_invalid_motion_frame_domain() {
    return expects_invalid_argument(
               []() {
                   static_cast<void>(nn_map_motion_frame(
                       0u,
                       0.0f,
                       60.0f,
                       0.0f,
                       static_cast<CameraPrecision>(99)));
               },
               "unsupported motion frame precision was accepted") &&
           expects_invalid_argument(
               []() {
                   static_cast<void>(nn_map_motion_frame(
                       0u,
                       std::numeric_limits<float>::quiet_NaN(),
                       60.0f,
                       0.0f,
                       CameraPrecision::Single));
               },
               "NaN motion frame start was accepted") &&
           expects_invalid_argument(
               []() {
                   static_cast<void>(nn_map_motion_frame(
                       kBypass,
                       0.0f,
                       60.0f,
                       std::numeric_limits<float>::infinity(),
                       CameraPrecision::Single));
               },
               "infinite bypass frame was accepted") &&
           expects_invalid_argument(
               []() {
                   static_cast<void>(nn_map_motion_frame(kRepeat, 0.0f, 0.0f, 0.0f, CameraPrecision::Double));
               },
               "zero cyclic length was accepted") &&
           expects_invalid_argument(
               []() {
                   static_cast<void>(nn_map_motion_frame(kMirror, 60.0f, 0.0f, 30.0f, CameraPrecision::Double));
               },
               "negative cyclic length was accepted") &&
           expects_invalid_argument(
               []() {
                   static_cast<void>(nn_map_motion_frame(
                       kRepeat,
                       0.0f,
                       1.0f,
                       1073741824.0f,
                       CameraPrecision::Single));
               },
               "out-of-range cyclic quotient was accepted") &&
           expects_invalid_argument(
               []() {
                   static_cast<void>(nn_map_motion_frame(
                       kMirror,
                       -std::numeric_limits<float>::max(),
                       std::numeric_limits<float>::max(),
                       0.0f,
                       CameraPrecision::Single));
               },
               "nonfinite float-spilled cyclic length was accepted");
}

bool test_linear_evaluation_uses_active_mapping_only() {
    std::array<NnMotionA16Key, 2u> keys = {{
        {0, -300},
        {60, 900},
    }};
    const std::array<NnMotionA16Key, 2u> original = keys;

    const std::optional<std::int16_t> inactive = nn_evaluate_linear_a16(
        kInterval | kLinearFlags,
        0.0f,
        60.0f,
        60.0f,
        nullptr,
        0u,
        CameraPrecision::Single);
    const std::optional<std::int16_t> active = nn_evaluate_linear_a16(
        kClamp | kLinearFlags,
        0.0f,
        60.0f,
        90.0f,
        keys.data(),
        keys.size(),
        CameraPrecision::Double);

    return check(!inactive.has_value(), "inactive linear frame sampled null keys") &&
           check(active.has_value() && *active == 900, "active linear frame did not sample the clamped endpoint") &&
           check(same_keys(keys, original), "linear frame evaluation modified motion keys") &&
           expects_invalid_argument(
               []() {
                   static_cast<void>(nn_evaluate_linear_a16(
                       kBypass | kLinearFlags,
                       0.0f,
                       60.0f,
                       0.0f,
                       nullptr,
                       0u,
                       CameraPrecision::Single));
               },
               "linear evaluator accepted bypass-plus-linear flags");
}

}

int main() {
    return test_default_bypass_interval_and_clamp_modes() &&
                   test_cyclic_modes_preserve_negative_cycle_and_signed_zero_rules() &&
                   test_mirror_maps_in_range_precision_cancellation_cases() &&
                   test_repeat_direct_fimul_precision_control() &&
                   test_rejects_invalid_motion_frame_domain() &&
                   test_linear_evaluation_uses_active_mapping_only()
        ? 0
        : 1;
}
