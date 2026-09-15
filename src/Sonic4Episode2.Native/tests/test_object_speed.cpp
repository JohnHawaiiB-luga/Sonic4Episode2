#include "object_speed.h"

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

constexpr std::array<ObjectSpeedPrecision, 2u> kPrecisions{{
    ObjectSpeedPrecision::Single,
    ObjectSpeedPrecision::Double,
}};
constexpr float kInputLimit = 16777216.0f;

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

bool test_directional_speed_up_saturation() {
    for (ObjectSpeedPrecision precision : kPrecisions) {
        if (!check(
                object_speed_up(12.0f, 1.0f, 10.0f, 1.0f, precision) == 10.0f,
                "positive outward acceleration did not clamp to the positive cap") ||
            !check(
                object_speed_up(12.0f, -1.0f, 10.0f, 1.0f, precision) == 11.0f,
                "positive over-cap speed moving toward zero was clamped") ||
            !check(
                object_speed_up(-12.0f, -1.0f, 10.0f, 1.0f, precision) == -10.0f,
                "negative outward acceleration did not clamp to the negative cap") ||
            !check(
                object_speed_up(-12.0f, 1.0f, 10.0f, 1.0f, precision) == -11.0f,
                "negative over-cap speed moving toward zero was clamped") ||
            !check(
                object_speed_up(12.0f, 0.0f, 10.0f, 1.0f, precision) == 10.0f,
                "zero acceleration did not select the positive clamp direction")) {
            return false;
        }
    }
    return true;
}

bool test_zero_cap_and_time_scale() {
    for (ObjectSpeedPrecision precision : kPrecisions) {
        if (!check(
                object_speed_up(5.0f, 1.0f, 0.0f, 1.0f, precision) == 6.0f,
                "zero cap unexpectedly clamped positive acceleration") ||
            !check(
                object_speed_up(-5.0f, -1.0f, 0.0f, 1.0f, precision) == -6.0f,
                "zero cap unexpectedly clamped negative acceleration") ||
            !check(
                object_speed_up(12.0f, 1.0f, 10.0f, 0.0f, precision) == 10.0f,
                "zero time scale lost the positive directional cap") ||
            !check(
                object_speed_up(-12.0f, -1.0f, 10.0f, 0.0f, precision) == -10.0f,
                "zero time scale lost the negative directional cap") ||
            !check(
                object_speed_down(3.0f, 1.0f, 0.0f, precision) == 3.0f,
                "zero time scale changed positive braking speed") ||
            !check(
                object_speed_down(-3.0f, 1.0f, 0.0f, precision) == -3.0f,
                "zero time scale changed negative braking speed")) {
            return false;
        }
    }
    return true;
}

bool test_braking_through_zero() {
    for (ObjectSpeedPrecision precision : kPrecisions) {
        if (!check(
                float_bits(object_speed_down(0.25f, 0.5f, 1.0f, precision)) == 0x00000000u,
                "positive braking overshoot did not clamp to positive zero") ||
            !check(
                float_bits(object_speed_down(-0.25f, 0.5f, 1.0f, precision)) == 0x00000000u,
                "negative braking overshoot did not clamp to positive zero")) {
            return false;
        }
    }
    return true;
}

bool test_signed_zero_behavior() {
    const float negative_zero = float_from_bits(0x80000000u);
    for (ObjectSpeedPrecision precision : kPrecisions) {
        if (!check(
                float_bits(object_speed_up(negative_zero, negative_zero, 10.0f, 1.0f, precision)) == 0x80000000u,
                "speed-up lost negative zero through a negative-zero add") ||
            !check(
                float_bits(object_speed_down(negative_zero, negative_zero, 1.0f, precision)) == 0x80000000u,
                "speed-down lost negative zero through a negative-zero deceleration") ||
            !check(
                float_bits(object_speed_down(0.0f, negative_zero, 1.0f, precision)) == 0x00000000u,
                "speed-down changed positive zero through a negative-zero deceleration") ||
            !check(
                float_bits(object_speed_down(0.0f, 1.0f, 1.0f, precision)) == 0x00000000u,
                "zero speed selected the positive-speed braking branch")) {
            return false;
        }
    }
    return true;
}

bool test_precision_discriminator() {
    constexpr float epsilon = 0x1p-23f;
    constexpr float acceleration = 1.0f + epsilon;
    constexpr float time_scale = 1.0f - epsilon;
    return check(
               float_bits(object_speed_up(
                   -1.0f,
                   acceleration,
                   0.0f,
                   time_scale,
                   ObjectSpeedPrecision::Single)) == 0x00000000u,
               "single-precision speed-up did not round the discriminator to positive zero") &&
           check(
               float_bits(object_speed_up(
                   -1.0f,
                   acceleration,
                   0.0f,
                   time_scale,
                   ObjectSpeedPrecision::Double)) == 0xa8800000u,
               "double-precision speed-up did not retain the discriminator residual") &&
           check(
               float_bits(object_speed_down(
                   1.0f,
                   acceleration,
                   time_scale,
                   ObjectSpeedPrecision::Single)) == 0x00000000u,
               "single-precision speed-down did not round the discriminator to positive zero") &&
           check(
               float_bits(object_speed_down(
                   1.0f,
                   acceleration,
                   time_scale,
                   ObjectSpeedPrecision::Double)) == 0x28800000u,
               "double-precision speed-down did not retain the discriminator residual");
}

bool test_finite_boundaries_and_subnormal_spill() {
    const float minimum_subnormal = float_from_bits(0x00000001u);
    const float huge_finite = static_cast<float>(281474976710656.0);
    for (ObjectSpeedPrecision precision : kPrecisions) {
        if (!check(
                float_bits(object_speed_up(minimum_subnormal, minimum_subnormal, 0.0f, 0.5f, precision)) ==
                    0x00000002u,
                "speed-up rounded a subnormal product before the required final spill") ||
            !check(
                object_speed_up(0.0f, kInputLimit, 0.0f, kInputLimit, precision) == huge_finite,
                "finite boundary product was not retained before the final spill") ||
            !check(
                object_speed_up(kInputLimit, kInputLimit, kInputLimit, 1.0f, precision) == kInputLimit,
                "finite boundary acceleration did not clamp") ||
            !check(
                float_bits(object_speed_down(kInputLimit, kInputLimit, 1.0f, precision)) == 0x00000000u,
                "positive finite boundary braking did not reach zero") ||
            !check(
                float_bits(object_speed_down(-kInputLimit, kInputLimit, 1.0f, precision)) == 0x00000000u,
                "negative finite boundary braking did not reach zero")) {
            return false;
        }
    }
    return true;
}

bool test_invalid_inputs_and_precision() {
    const float over_limit = std::nextafter(kInputLimit, std::numeric_limits<float>::infinity());
    return expects_invalid_argument(
               []() {
                   static_cast<void>(object_speed_up(
                       std::numeric_limits<float>::quiet_NaN(), 0.0f, 0.0f, 0.0f, ObjectSpeedPrecision::Single));
               },
               "speed-up accepted NaN speed") &&
           expects_invalid_argument(
               []() {
                   static_cast<void>(object_speed_up(
                       0.0f, std::numeric_limits<float>::infinity(), 0.0f, 0.0f, ObjectSpeedPrecision::Single));
               },
               "speed-up accepted infinite acceleration") &&
           expects_invalid_argument(
               [over_limit]() {
                   static_cast<void>(object_speed_up(
                       over_limit, 0.0f, 0.0f, 0.0f, ObjectSpeedPrecision::Single));
               },
               "speed-up accepted an out-of-range finite speed") &&
           expects_invalid_argument(
               []() {
                   static_cast<void>(object_speed_up(0.0f, 0.0f, -1.0f, 0.0f, ObjectSpeedPrecision::Single));
               },
               "speed-up accepted a negative cap") &&
           expects_invalid_argument(
               []() {
                   static_cast<void>(object_speed_up(0.0f, 0.0f, 0.0f, -1.0f, ObjectSpeedPrecision::Single));
               },
               "speed-up accepted a negative time scale") &&
           expects_invalid_argument(
               []() {
                   static_cast<void>(object_speed_down(0.0f, -1.0f, 0.0f, ObjectSpeedPrecision::Single));
               },
               "speed-down accepted negative deceleration") &&
           expects_invalid_argument(
               []() {
                   static_cast<void>(object_speed_down(0.0f, 0.0f, -1.0f, ObjectSpeedPrecision::Single));
               },
               "speed-down accepted negative time scale") &&
           expects_invalid_argument(
               []() {
                   static_cast<void>(object_speed_up(
                       0.0f, 0.0f, 0.0f, 0.0f, static_cast<ObjectSpeedPrecision>(99)));
               },
               "speed-up accepted an invalid precision") &&
           expects_invalid_argument(
               []() {
                   static_cast<void>(object_speed_down(
                       0.0f, 0.0f, 0.0f, static_cast<ObjectSpeedPrecision>(99)));
               },
               "speed-down accepted an invalid precision");
}

}

int main() {
    return test_directional_speed_up_saturation() &&
                   test_zero_cap_and_time_scale() &&
                   test_braking_through_zero() &&
                   test_signed_zero_behavior() &&
                   test_precision_discriminator() &&
                   test_finite_boundaries_and_subnormal_spill() &&
                   test_invalid_inputs_and_precision()
        ? 0
        : 1;
}
