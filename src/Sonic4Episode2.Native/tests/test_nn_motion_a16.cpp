#include "nn_motion_a16.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <exception>
#include <limits>
#include <stdexcept>
#include <utility>
#include <vector>

namespace {

constexpr std::array<CameraPrecision, 2u> kPrecisions = {{
    CameraPrecision::Single,
    CameraPrecision::Double,
}};

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

template <std::size_t Count, typename Callable>
bool rejects_without_mutating(
    std::array<NnMotionA16Key, Count>& keys,
    Callable&& callable,
    const char* message) {
    const std::array<NnMotionA16Key, Count> original = keys;
    return expects_invalid_argument(std::forward<Callable>(callable), message) &&
           check(same_keys(keys, original), "rejected motion sampler input was modified");
}

bool test_holds_endpoints_fractional_samples_and_wrap_seams() {
    std::array<NnMotionA16Key, 1u> one_key = {{
        {-32768, -1234},
    }};
    std::array<NnMotionA16Key, 2u> ordinary = {{
        {0, -1000},
        {7, 2000},
    }};
    std::array<NnMotionA16Key, 2u> seam = {{
        {0, 32760},
        {1, -32760},
    }};
    std::array<NnMotionA16Key, 2u> reversed_seam = {{
        {0, -32760},
        {1, 32760},
    }};
    const std::array<NnMotionA16Key, 1u> original_one_key = one_key;
    const std::array<NnMotionA16Key, 2u> original_ordinary = ordinary;
    const std::array<NnMotionA16Key, 2u> original_seam = seam;
    const std::array<NnMotionA16Key, 2u> original_reversed_seam = reversed_seam;

    for (CameraPrecision precision : kPrecisions) {
        if (!check(
                nn_sample_linear_a16(one_key.data(), one_key.size(), -32768.0f, precision) == -1234,
                "single key did not preserve its lower endpoint") ||
            !check(
                nn_sample_linear_a16(one_key.data(), one_key.size(), 32767.5f, precision) == -1234,
                "single key did not hold through the upper frame bound") ||
            !check(
                nn_sample_linear_a16(ordinary.data(), ordinary.size(), 0.0f, precision) == -1000,
                "first ordinary key did not match exactly") ||
            !check(
                nn_sample_linear_a16(ordinary.data(), ordinary.size(), 7.0f, precision) == 2000,
                "last ordinary key did not match exactly") ||
            !check(
                nn_sample_linear_a16(ordinary.data(), ordinary.size(), 8.0f, precision) == 2000,
                "last ordinary key did not hold") ||
            !check(
                nn_sample_linear_a16(ordinary.data(), ordinary.size(), 0.1f, precision) == -958,
                "ordinary fractional sample differed") ||
            !check(
                nn_sample_linear_a16(ordinary.data(), ordinary.size(), 1.0f, precision) == -572,
                "ordinary integer sample differed") ||
            !check(
                nn_sample_linear_a16(seam.data(), seam.size(), 0.5f, precision) == -32768,
                "positive-to-negative sign seam differed") ||
            !check(
                nn_sample_linear_a16(reversed_seam.data(), reversed_seam.size(), 0.5f, precision) == -32768,
                "negative-to-positive sign seam differed")) {
            return false;
        }
    }

    return check(same_keys(one_key, original_one_key), "single key input was modified") &&
           check(same_keys(ordinary, original_ordinary), "ordinary key input was modified") &&
           check(same_keys(seam, original_seam), "sign seam input was modified") &&
           check(same_keys(reversed_seam, original_reversed_seam), "reversed sign seam input was modified");
}

bool test_negative_search_and_precision_goldens() {
    std::array<NnMotionA16Key, 4u> negative_search = {{
        {-3, 32767},
        {-2, -32768},
        {-1, 0},
        {0, 32767},
    }};
    std::array<NnMotionA16Key, 4u> precision_case = {{
        {-20684, -15816},
        {-4486, 14723},
        {-1747, -24307},
        {17906, 12303},
    }};
    const std::array<NnMotionA16Key, 4u> original_negative_search = negative_search;
    const std::array<NnMotionA16Key, 4u> original_precision_case = precision_case;
    const float precision_frame = float_from_bits(1175923883u);

    for (CameraPrecision precision : kPrecisions) {
        if (!check(
                nn_sample_linear_a16(negative_search.data(), negative_search.size(), -2.5f, precision) == 16384,
                "negative fractional search differed")) {
            return false;
        }
    }

    return check(
               nn_sample_linear_a16(
                   precision_case.data(),
                   precision_case.size(),
                   precision_frame,
                   CameraPrecision::Single) == 24417,
               "single precision synthetic-82 sample differed") &&
           check(
               nn_sample_linear_a16(
                   precision_case.data(),
                   precision_case.size(),
                   precision_frame,
                   CameraPrecision::Double) == 24416,
               "double precision synthetic-82 sample differed") &&
           check(same_keys(negative_search, original_negative_search), "negative search input was modified") &&
           check(same_keys(precision_case, original_precision_case), "precision case input was modified");
}

bool test_maximum_distinct_frame_count() {
    std::vector<NnMotionA16Key> keys(65536u);
    for (std::size_t index = 0u; index < keys.size(); ++index) {
        const auto value = static_cast<std::int16_t>(static_cast<std::int32_t>(index) - 32768);
        keys[index] = {value, value};
    }
    for (CameraPrecision precision : kPrecisions) {
        for (std::int32_t frame : {-32768, -16384, -1, 0, 12345, 32767}) {
            if (!check(
                    nn_sample_linear_a16(keys.data(), keys.size(), static_cast<float>(frame), precision) == frame,
                    "maximum key count lost an exact frame")) {
                return false;
            }
        }
    }
    for (std::size_t index = 0u; index < keys.size(); ++index) {
        const auto expected = static_cast<std::int16_t>(static_cast<std::int32_t>(index) - 32768);
        if (!check(keys[index].frame == expected && keys[index].value == expected,
                   "maximum key count input was modified")) {
            return false;
        }
    }
    return true;
}

bool test_rejects_invalid_inputs_without_mutating_keys() {
    std::array<NnMotionA16Key, 2u> valid_keys = {{
        {-32768, -10},
        {32767, 10},
    }};
    std::array<NnMotionA16Key, 2u> pre_first_keys = {{
        {0, 1},
        {1, 2},
    }};
    std::array<NnMotionA16Key, 3u> unsorted_keys = {{
        {0, 1},
        {2, 2},
        {1, 3},
    }};
    std::array<NnMotionA16Key, 2u> duplicate_keys = {{
        {0, 1},
        {0, 2},
    }};

    return expects_invalid_argument(
               []() {
                   static_cast<void>(nn_sample_linear_a16(nullptr, 1u, 0.0f, CameraPrecision::Single));
               },
               "null motion key data was accepted") &&
           rejects_without_mutating(
               valid_keys,
               [&]() {
                   static_cast<void>(nn_sample_linear_a16(valid_keys.data(), 0u, 0.0f, CameraPrecision::Single));
               },
               "zero motion key count was accepted") &&
           rejects_without_mutating(
               valid_keys,
               [&]() {
                   static_cast<void>(nn_sample_linear_a16(valid_keys.data(), 65537u, 0.0f, CameraPrecision::Single));
               },
               "motion key count above signed16-frame capacity was accepted") &&
           rejects_without_mutating(
               valid_keys,
               [&]() {
                   static_cast<void>(nn_sample_linear_a16(
                       valid_keys.data(),
                       valid_keys.size(),
                       0.0f,
                       static_cast<CameraPrecision>(99)));
               },
               "unsupported motion precision was accepted") &&
           rejects_without_mutating(
               valid_keys,
               [&]() {
                   static_cast<void>(nn_sample_linear_a16(
                       valid_keys.data(),
                       valid_keys.size(),
                       std::numeric_limits<float>::quiet_NaN(),
                       CameraPrecision::Single));
               },
               "NaN motion frame was accepted") &&
           rejects_without_mutating(
               valid_keys,
               [&]() {
                   static_cast<void>(nn_sample_linear_a16(
                       valid_keys.data(),
                       valid_keys.size(),
                       std::numeric_limits<float>::infinity(),
                       CameraPrecision::Single));
               },
               "infinite motion frame was accepted") &&
           rejects_without_mutating(
               valid_keys,
               [&]() {
                   static_cast<void>(nn_sample_linear_a16(
                       valid_keys.data(),
                       valid_keys.size(),
                       -32768.5f,
                       CameraPrecision::Single));
               },
               "motion frame below the signed16 lower bound was accepted") &&
           rejects_without_mutating(
               valid_keys,
               [&]() {
                   static_cast<void>(nn_sample_linear_a16(
                       valid_keys.data(),
                       valid_keys.size(),
                       32768.0f,
                       CameraPrecision::Single));
               },
               "motion frame at the exclusive upper bound was accepted") &&
           rejects_without_mutating(
               pre_first_keys,
               [&]() {
                   static_cast<void>(nn_sample_linear_a16(
                       pre_first_keys.data(),
                       pre_first_keys.size(),
                       -0.5f,
                       CameraPrecision::Single));
               },
               "motion frame before the first key was accepted") &&
           rejects_without_mutating(
               unsorted_keys,
               [&]() {
                   static_cast<void>(nn_sample_linear_a16(
                       unsorted_keys.data(),
                       unsorted_keys.size(),
                       0.0f,
                       CameraPrecision::Double));
               },
               "unsorted motion keys were accepted") &&
           rejects_without_mutating(
               duplicate_keys,
               [&]() {
                   static_cast<void>(nn_sample_linear_a16(
                       duplicate_keys.data(),
                       duplicate_keys.size(),
                       0.0f,
                       CameraPrecision::Double));
               },
               "duplicate motion key frames were accepted");
}

}

int main() {
    return test_holds_endpoints_fractional_samples_and_wrap_seams() &&
                   test_negative_search_and_precision_goldens() &&
                   test_maximum_distinct_frame_count() &&
                   test_rejects_invalid_inputs_without_mutating_keys()
        ? 0
        : 1;
}
