#include "nn_motion_scalar.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <exception>
#include <limits>
#include <optional>
#include <stdexcept>
#include <utility>

namespace {

constexpr std::uint32_t kInterval = 0x10000u;
constexpr std::uint32_t kClamp = 0x20000u;
constexpr std::uint32_t kBypass = 0x40u;

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

template <std::size_t Count>
bool same_a16_keys(
    const std::array<NnMotionA16Key, Count>& left,
    const std::array<NnMotionA16Key, Count>& right) {
    for (std::size_t index = 0u; index < Count; ++index) {
        if (left[index].frame != right[index].frame || left[index].value != right[index].value) {
            return false;
        }
    }
    return true;
}

template <std::size_t Count>
bool same_float_keys(
    const std::array<NnMotionFloatKey, Count>& left,
    const std::array<NnMotionFloatKey, Count>& right) {
    for (std::size_t index = 0u; index < Count; ++index) {
        if (float_bits(left[index].frame) != float_bits(right[index].frame) ||
            float_bits(left[index].value) != float_bits(right[index].value)) {
            return false;
        }
    }
    return true;
}

bool test_constant_a16_truncates_negative_fractional_frames() {
    std::array<NnMotionA16Key, 6u> keys = {{
        {-32768, -32768},
        {-2, 10},
        {-1, 20},
        {0, -1},
        {3, 123},
        {32767, 32767},
    }};
    const std::array<NnMotionA16Key, 6u> original = keys;
    const std::array<NnMotionA16Key, 1u> one_key = {{
        {-32768, -123},
    }};

    for (CameraPrecision precision : {CameraPrecision::Single, CameraPrecision::Double}) {
        if (!check(
                nn_sample_constant_a16(keys.data(), keys.size(), -32768.0f, precision) == -32768,
                "constant A16 did not retain the signed lower boundary") ||
            !check(
                nn_sample_constant_a16(keys.data(), keys.size(), -1.9f, precision) == 20,
                "constant A16 rounded a negative fractional frame toward negative infinity") ||
            !check(
                nn_sample_constant_a16(keys.data(), keys.size(), -0.9f, precision) == -1,
                "constant A16 did not truncate toward zero near negative zero") ||
            !check(
                nn_sample_constant_a16(keys.data(), keys.size(), 32767.9f, precision) == 32767,
                "constant A16 did not retain the signed upper boundary") ||
            !check(
                nn_sample_constant_a16(one_key.data(), one_key.size(), 32767.9f, precision) == -123,
                "constant A16 one-key sampling changed its signed value")) {
            return false;
        }
    }

    return check(same_a16_keys(keys, original), "constant A16 sampling modified key data");
}

bool test_float_selected_values_ratio_spill_and_precision_goldens() {
    const std::array<NnMotionFloatKey, 3u> signed_values = {{
        {0.0f, float_from_bits(0x80000000u)},
        {1.0f, float_from_bits(0x00000001u)},
        {2.0f, float_from_bits(0x80000001u)},
    }};
    const std::array<NnMotionFloatKey, 1u> one_key = {{
        {0.0f, float_from_bits(0x80000000u)},
    }};
    const std::array<NnMotionFloatKey, 2u> ratio_spill = {{
        {0.0f, float_from_bits(1266679808u)},
        {1.0f, float_from_bits(3414163456u)},
    }};
    const std::array<NnMotionFloatKey, 2u> precision_case = {{
        {float_from_bits(3263630183u), float_from_bits(831638544u)},
        {float_from_bits(1117850213u), float_from_bits(3255455605u)},
    }};
    const std::array<NnMotionFloatKey, 3u> original_signed_values = signed_values;
    const std::array<NnMotionFloatKey, 1u> original_one_key = one_key;
    const std::array<NnMotionFloatKey, 2u> original_ratio_spill = ratio_spill;
    const std::array<NnMotionFloatKey, 2u> original_precision_case = precision_case;

    for (CameraPrecision precision : {CameraPrecision::Single, CameraPrecision::Double}) {
        if (!check(
                float_bits(nn_sample_constant_float(
                    signed_values.data(), signed_values.size(), 0.5f, precision)) == 0x80000000u,
                "constant float sampling lost a selected negative zero") ||
            !check(
                float_bits(nn_sample_constant_float(
                    signed_values.data(), signed_values.size(), 1.5f, precision)) == 0x00000001u,
                "constant float sampling lost a selected positive subnormal") ||
            !check(
                float_bits(nn_sample_linear_float(
                    signed_values.data(), signed_values.size(), 2.0f, precision)) == 0x80000001u,
                "last linear float key did not preserve its negative subnormal") ||
            !check(
                float_bits(nn_sample_linear_float(one_key.data(), one_key.size(), 0.5f, precision)) ==
                    0x80000000u,
                "one-key linear float sampling lost negative zero") ||
            !check(
                float_bits(nn_sample_constant_float(one_key.data(), one_key.size(), 0.5f, precision)) ==
                    0x80000000u,
                "one-key constant float sampling lost negative zero") ||
            !check(
                float_bits(nn_sample_linear_float(
                    ratio_spill.data(),
                    ratio_spill.size(),
                    float_from_bits(1036831949u),
                    precision)) == 1263324364u,
                "linear float sampling omitted the binary32 ratio spill")) {
            return false;
        }
    }

    return check(
               float_bits(nn_sample_linear_float(
                   precision_case.data(),
                   precision_case.size(),
                   float_from_bits(3255507547u),
                   CameraPrecision::Single)) == 3237265439u,
               "single precision scalar float golden changed") &&
           check(
               float_bits(nn_sample_linear_float(
                   precision_case.data(),
                   precision_case.size(),
                   float_from_bits(3255507547u),
                   CameraPrecision::Double)) == 3237265435u,
               "double precision scalar float golden changed") &&
           check(
               same_float_keys(signed_values, original_signed_values) &&
                   same_float_keys(one_key, original_one_key) &&
                   same_float_keys(ratio_spill, original_ratio_spill) &&
                   same_float_keys(precision_case, original_precision_case),
               "float sampling modified key data");
}

bool test_rejects_invalid_scalar_sampler_inputs() {
    std::array<NnMotionA16Key, 2u> a16 = {{
        {0, 1},
        {1, 2},
    }};
    std::array<NnMotionA16Key, 2u> unsorted_a16 = {{
        {0, 1},
        {0, 2},
    }};
    std::array<NnMotionFloatKey, 2u> floats = {{
        {0.0f, 1.0f},
        {1.0f, 2.0f},
    }};
    std::array<NnMotionFloatKey, 2u> unsorted_floats = {{
        {0.0f, 1.0f},
        {0.0f, 2.0f},
    }};
    std::array<NnMotionFloatKey, 2u> nonfinite_floats = {{
        {0.0f, 1.0f},
        {1.0f, std::numeric_limits<float>::infinity()},
    }};
    const std::array<NnMotionA16Key, 2u> original_a16 = a16;
    const std::array<NnMotionFloatKey, 2u> original_floats = floats;

    return expects_invalid_argument(
               []() {
                   static_cast<void>(nn_sample_constant_a16(nullptr, 1u, 0.0f, CameraPrecision::Single));
               },
               "constant A16 accepted null keys") &&
           expects_invalid_argument(
               [&]() {
                   static_cast<void>(nn_sample_constant_a16(a16.data(), 0u, 0.0f, CameraPrecision::Single));
               },
               "constant A16 accepted an empty key range") &&
           expects_invalid_argument(
               [&]() {
                   static_cast<void>(nn_sample_constant_a16(a16.data(), 65537u, 0.0f, CameraPrecision::Single));
               },
               "constant A16 accepted too many keys") &&
           expects_invalid_argument(
               [&]() {
                   static_cast<void>(nn_sample_constant_a16(
                       unsorted_a16.data(), unsorted_a16.size(), 0.0f, CameraPrecision::Single));
               },
               "constant A16 accepted unordered keys") &&
           expects_invalid_argument(
               [&]() {
                   static_cast<void>(nn_sample_constant_a16(
                       a16.data(),
                       a16.size(),
                       std::numeric_limits<float>::quiet_NaN(),
                       CameraPrecision::Single));
               },
               "constant A16 accepted a NaN frame") &&
           expects_invalid_argument(
               [&]() {
                   static_cast<void>(nn_sample_constant_a16(
                       a16.data(), a16.size(), -0.5f, CameraPrecision::Single));
               },
               "constant A16 accepted a frame before its first key") &&
           expects_invalid_argument(
               [&]() {
                   static_cast<void>(nn_sample_constant_a16(
                       a16.data(), a16.size(), 32768.0f, CameraPrecision::Double));
               },
               "constant A16 accepted its exclusive upper frame bound") &&
           expects_invalid_argument(
               [&]() {
                   static_cast<void>(nn_sample_constant_a16(
                       a16.data(), a16.size(), 0.0f, static_cast<CameraPrecision>(99)));
               },
               "constant A16 accepted an unsupported precision") &&
           expects_invalid_argument(
               []() {
                   static_cast<void>(nn_sample_linear_float(nullptr, 1u, 0.0f, CameraPrecision::Single));
               },
               "linear float accepted null keys") &&
           expects_invalid_argument(
               [&]() {
                   static_cast<void>(nn_sample_constant_float(floats.data(), 0u, 0.0f, CameraPrecision::Single));
               },
               "constant float accepted an empty key range") &&
           expects_invalid_argument(
               [&]() {
                   static_cast<void>(nn_sample_linear_float(
                       floats.data(), 65537u, 0.0f, CameraPrecision::Single));
               },
               "linear float accepted too many keys") &&
           expects_invalid_argument(
               [&]() {
                   static_cast<void>(nn_sample_linear_float(
                       unsorted_floats.data(), unsorted_floats.size(), 0.0f, CameraPrecision::Single));
               },
               "linear float accepted unordered keys") &&
           expects_invalid_argument(
               [&]() {
                   static_cast<void>(nn_sample_constant_float(
                       nonfinite_floats.data(), nonfinite_floats.size(), 0.0f, CameraPrecision::Single));
               },
               "constant float accepted a nonfinite key value") &&
           expects_invalid_argument(
               [&]() {
                   static_cast<void>(nn_sample_linear_float(
                       floats.data(),
                       floats.size(),
                       std::numeric_limits<float>::infinity(),
                       CameraPrecision::Double));
               },
               "linear float accepted an infinite frame") &&
           expects_invalid_argument(
               [&]() {
                   static_cast<void>(nn_sample_linear_float(
                       floats.data(), floats.size(), -0.5f, CameraPrecision::Double));
               },
               "linear float accepted a frame before its first key") &&
           expects_invalid_argument(
               [&]() {
                   static_cast<void>(nn_sample_constant_float(
                       floats.data(),
                       floats.size(),
                       0.0f,
                       static_cast<CameraPrecision>(99)));
               },
               "constant float accepted an unsupported precision") &&
           check(same_a16_keys(a16, original_a16), "rejected constant A16 sampling modified key data") &&
           check(same_float_keys(floats, original_floats), "rejected float sampling modified key data");
}

bool test_evaluators_map_active_frames_and_preserve_constant_selection() {
    const std::array<NnMotionA16Key, 2u> a16 = {{
        {0, -300},
        {60, 900},
    }};
    const std::array<NnMotionFloatKey, 2u> floats = {{
        {0.0f, float_from_bits(0x80000000u)},
        {60.0f, 10.0f},
    }};
    const std::optional<std::int16_t> inactive_a16 = nn_evaluate_constant_a16(
        kInterval | 0x4u,
        0.0f,
        60.0f,
        60.0f,
        nullptr,
        0u,
        CameraPrecision::Single);
    const std::optional<float> inactive_float = nn_evaluate_scalar_float(
        kInterval | 0x2u,
        0.0f,
        60.0f,
        60.0f,
        nullptr,
        0u,
        CameraPrecision::Single);
    const std::optional<std::int16_t> clamped_a16 = nn_evaluate_constant_a16(
        kClamp | 0x4u,
        0.0f,
        60.0f,
        90.0f,
        a16.data(),
        a16.size(),
        CameraPrecision::Double);
    const std::optional<float> constant_float = nn_evaluate_scalar_float(
        kClamp | 0x4u,
        0.0f,
        60.0f,
        30.0f,
        floats.data(),
        floats.size(),
        CameraPrecision::Double);

    return check(!inactive_a16.has_value(), "inactive constant A16 evaluation sampled null keys") &&
           check(!inactive_float.has_value(), "inactive scalar float evaluation sampled null keys") &&
           check(clamped_a16.has_value() && *clamped_a16 == 900,
                 "constant A16 evaluation did not sample the mapped endpoint") &&
           check(constant_float.has_value() && float_bits(*constant_float) == 0x80000000u,
                 "constant scalar evaluation used linear interpolation") &&
           expects_invalid_argument(
               [&]() {
                   static_cast<void>(nn_evaluate_constant_a16(
                       kClamp | 0x2u,
                       0.0f,
                       60.0f,
                       0.0f,
                       a16.data(),
                       a16.size(),
                       CameraPrecision::Single));
               },
               "constant A16 evaluation accepted linear flags") &&
           expects_invalid_argument(
               [&]() {
                   static_cast<void>(nn_evaluate_scalar_float(
                       kClamp,
                       0.0f,
                       60.0f,
                       0.0f,
                       floats.data(),
                       floats.size(),
                       CameraPrecision::Single));
               },
               "scalar float evaluation accepted unsupported flags") &&
           expects_invalid_argument(
               [&]() {
                   static_cast<void>(nn_evaluate_scalar_float(
                       kBypass | 0x2u,
                       0.0f,
                       60.0f,
                       0.0f,
                       floats.data(),
                       floats.size(),
                       CameraPrecision::Single));
               },
               "scalar float evaluation accepted bypass-plus-linear flags");
}

}

int main() {
    return test_constant_a16_truncates_negative_fractional_frames() &&
                   test_float_selected_values_ratio_spill_and_precision_goldens() &&
                   test_rejects_invalid_scalar_sampler_inputs() &&
                   test_evaluators_map_active_frames_and_preserve_constant_selection()
        ? 0
        : 1;
}
