#include "nn_motion_a16.h"

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <stdexcept>

namespace {

constexpr std::size_t kMaximumKeyCount = 65536u;
constexpr float kMinimumFrame = -32768.0f;
constexpr float kMaximumFrame = 32768.0f;
constexpr double kRatioScale = 65536.0;

void require(bool condition, const char* message) {
    if (!condition) {
        throw std::invalid_argument(message);
    }
}

void require_precision(CameraPrecision precision) {
    require(
        precision == CameraPrecision::Single || precision == CameraPrecision::Double,
        "motion A16 precision is unsupported");
}

void require_keys(const NnMotionA16Key* keys, std::size_t count) {
    require(keys != nullptr, "motion A16 keys are null");
    require(count > 0u && count <= kMaximumKeyCount, "motion A16 key count is out of range");
    for (std::size_t index = 1u; index < count; ++index) {
        require(keys[index - 1u].frame < keys[index].frame, "motion A16 keys are not strictly ascending");
    }
}

void require_frame(float frame, const NnMotionA16Key& first_key) {
    require(
        std::isfinite(frame) && frame >= kMinimumFrame && frame < kMaximumFrame,
        "motion A16 frame is nonfinite or out of range");
    require(frame >= static_cast<float>(first_key.frame), "motion A16 frame precedes the first key");
}

struct Arithmetic {
    CameraPrecision precision;

    double rounded(double value) const {
        if (precision == CameraPrecision::Double || !std::isfinite(value)) {
            return value;
        }
        // x87 precision control narrows the significand while retaining the exponent range.
        int exponent = 0;
        const float mantissa = static_cast<float>(std::frexp(value, &exponent));
        return std::ldexp(static_cast<double>(mantissa), exponent);
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

std::uint16_t low16(std::uint32_t value) {
    return static_cast<std::uint16_t>(value & 0xffffu);
}

std::int16_t signed16_from_bits(std::uint16_t bits) {
    if ((bits & 0x8000u) == 0u) {
        return static_cast<std::int16_t>(bits);
    }
    return static_cast<std::int16_t>(static_cast<std::int32_t>(bits) - 0x10000);
}

std::int32_t wrapped_value_difference(std::int16_t lower_value, std::int16_t next_value) {
    const std::uint32_t difference =
        static_cast<std::uint32_t>(static_cast<std::uint16_t>(lower_value)) -
        static_cast<std::uint32_t>(static_cast<std::uint16_t>(next_value));
    return static_cast<std::int32_t>(signed16_from_bits(low16(difference)));
}

std::uint32_t multiply_low32(std::int32_t left, std::int32_t right) {
    const std::uint64_t product =
        static_cast<std::uint64_t>(static_cast<std::uint32_t>(left)) *
        static_cast<std::uint64_t>(static_cast<std::uint32_t>(right));
    return static_cast<std::uint32_t>(product);
}

std::uint32_t arithmetic_shift_right_16_bits(std::uint32_t value) {
    const std::uint32_t shifted = value >> 16u;
    return (value & 0x80000000u) == 0u ? shifted : shifted | 0xffff0000u;
}

std::int16_t interpolate(
    const NnMotionA16Key& lower_key,
    const NnMotionA16Key& next_key,
    float frame,
    const Arithmetic& arithmetic) {
    const double frame_minus_next = arithmetic.subtract(
        static_cast<double>(frame),
        static_cast<double>(next_key.frame));
    const double scaled_frame_minus_next = arithmetic.multiply(frame_minus_next, kRatioScale);
    const std::int32_t lower_minus_next =
        static_cast<std::int32_t>(lower_key.frame) - static_cast<std::int32_t>(next_key.frame);
    const double ratio_value = arithmetic.divide(
        scaled_frame_minus_next,
        static_cast<double>(lower_minus_next));
    const std::int32_t truncated_ratio = static_cast<std::int32_t>(std::trunc(ratio_value));

    const std::int32_t wrapped_delta = wrapped_value_difference(lower_key.value, next_key.value);
    const std::uint32_t product = multiply_low32(truncated_ratio, wrapped_delta);
    const std::uint32_t shifted_product = arithmetic_shift_right_16_bits(product);
    const std::uint32_t result_bits =
        shifted_product + static_cast<std::uint32_t>(static_cast<std::uint16_t>(next_key.value));
    return signed16_from_bits(low16(result_bits));
}

}

std::int16_t nn_sample_linear_a16(
    const NnMotionA16Key* keys,
    std::size_t count,
    float frame,
    CameraPrecision precision) {
    require_precision(precision);
    require_keys(keys, count);
    require_frame(frame, keys[0u]);

    const std::int16_t truncated_frame = static_cast<std::int16_t>(static_cast<std::int32_t>(frame));
    std::size_t lower = 0u;
    std::size_t upper = count;
    while (upper - lower > 1u) {
        const std::size_t middle = lower + (upper - lower) / 2u;
        if (truncated_frame >= keys[middle].frame) {
            lower = middle;
        } else {
            upper = middle;
        }
    }

    if (lower == count - 1u) {
        return keys[lower].value;
    }

    const Arithmetic arithmetic{precision};
    return interpolate(keys[lower], keys[lower + 1u], frame, arithmetic);
}
