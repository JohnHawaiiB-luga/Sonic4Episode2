#include "nn_motion_scalar.h"

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <stdexcept>

namespace {

constexpr std::size_t kMaximumKeyCount = 65536u;
constexpr float kMinimumA16Frame = -32768.0f;
constexpr float kMaximumA16Frame = 32768.0f;
constexpr std::uint32_t kInterpolationMask = 0xe77u;
constexpr std::uint32_t kLinearInterpolation = 0x2u;
constexpr std::uint32_t kConstantInterpolation = 0x4u;

void require(bool condition, const char* message) {
    if (!condition) {
        throw std::invalid_argument(message);
    }
}

void require_precision(CameraPrecision precision) {
    require(
        precision == CameraPrecision::Single || precision == CameraPrecision::Double,
        "motion scalar precision is unsupported");
}

void require_a16_keys(const NnMotionA16Key* keys, std::size_t count) {
    require(keys != nullptr, "motion constant A16 keys are null");
    require(
        count > 0u && count <= kMaximumKeyCount,
        "motion constant A16 key count is out of range");
    for (std::size_t index = 1u; index < count; ++index) {
        require(
            keys[index - 1u].frame < keys[index].frame,
            "motion constant A16 keys are not strictly ascending");
    }
}

void require_a16_frame(float frame, const NnMotionA16Key& first_key) {
    require(
        std::isfinite(frame) && frame >= kMinimumA16Frame && frame < kMaximumA16Frame,
        "motion constant A16 frame is nonfinite or out of range");
    require(
        frame >= static_cast<float>(first_key.frame),
        "motion constant A16 frame precedes the first key");
}

void require_float_keys(const NnMotionFloatKey* keys, std::size_t count) {
    require(keys != nullptr, "motion scalar float keys are null");
    require(
        count > 0u && count <= kMaximumKeyCount,
        "motion scalar float key count is out of range");
    for (std::size_t index = 0u; index < count; ++index) {
        require(
            std::isfinite(keys[index].frame) && std::isfinite(keys[index].value),
            "motion scalar float keys must be finite");
        if (index > 0u) {
            require(
                keys[index - 1u].frame < keys[index].frame,
                "motion scalar float keys are not strictly ascending");
        }
    }
}

void require_float_frame(float frame, const NnMotionFloatKey& first_key) {
    require(std::isfinite(frame), "motion scalar float frame is nonfinite");
    require(frame >= first_key.frame, "motion scalar float frame precedes the first key");
}

std::size_t find_a16_lower_key(
    const NnMotionA16Key* keys,
    std::size_t count,
    std::int32_t frame) {
    std::size_t lower = 0u;
    std::size_t upper = count;
    while (upper - lower > 1u) {
        const std::size_t middle = lower + (upper - lower) / 2u;
        if (frame >= static_cast<std::int32_t>(keys[middle].frame)) {
            lower = middle;
        } else {
            upper = middle;
        }
    }
    return lower;
}

std::size_t find_float_lower_key(
    const NnMotionFloatKey* keys,
    std::size_t count,
    float frame) {
    std::size_t lower = 0u;
    std::size_t upper = count;
    while (upper - lower > 1u) {
        const std::size_t middle = lower + (upper - lower) / 2u;
        if (frame >= keys[middle].frame) {
            lower = middle;
        } else {
            upper = middle;
        }
    }
    return lower;
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

    double divide(double left, double right) const {
        return rounded(left / right);
    }

    float spill(double value) const {
        return static_cast<float>(value);
    }
};

float interpolate_float(
    const NnMotionFloatKey& lower_key,
    const NnMotionFloatKey& next_key,
    float frame,
    const Arithmetic& arithmetic) {
    const double frame_minus_next = arithmetic.subtract(
        static_cast<double>(frame),
        static_cast<double>(next_key.frame));
    const double lower_minus_next = arithmetic.subtract(
        static_cast<double>(lower_key.frame),
        static_cast<double>(next_key.frame));
    const float ratio = arithmetic.spill(arithmetic.divide(frame_minus_next, lower_minus_next));
    const double complement = arithmetic.subtract(1.0, static_cast<double>(ratio));
    const double next_weight = arithmetic.multiply(complement, static_cast<double>(next_key.value));
    const double lower_weight = arithmetic.multiply(static_cast<double>(ratio), static_cast<double>(lower_key.value));
    return arithmetic.spill(arithmetic.add(lower_weight, next_weight));
}

void require_constant_a16_flags(std::uint32_t flags) {
    require(
        (flags & kInterpolationMask) == kConstantInterpolation,
        "motion constant A16 evaluation flags are unsupported");
}

std::uint32_t require_scalar_float_flags(std::uint32_t flags) {
    const std::uint32_t interpolation = flags & kInterpolationMask;
    require(
        interpolation == kLinearInterpolation || interpolation == kConstantInterpolation,
        "motion scalar float evaluation flags are unsupported");
    return interpolation;
}

}

std::int16_t nn_sample_constant_a16(
    const NnMotionA16Key* keys,
    std::size_t count,
    float frame,
    CameraPrecision precision) {
    require_precision(precision);
    require_a16_keys(keys, count);
    require_a16_frame(frame, keys[0u]);

    const std::int32_t truncated_frame = static_cast<std::int32_t>(std::trunc(static_cast<double>(frame)));
    return keys[find_a16_lower_key(keys, count, truncated_frame)].value;
}

float nn_sample_linear_float(
    const NnMotionFloatKey* keys,
    std::size_t count,
    float frame,
    CameraPrecision precision) {
    require_precision(precision);
    require_float_keys(keys, count);
    require_float_frame(frame, keys[0u]);

    const std::size_t lower = find_float_lower_key(keys, count, frame);
    if (lower == count - 1u) {
        return keys[lower].value;
    }
    return interpolate_float(keys[lower], keys[lower + 1u], frame, Arithmetic{precision});
}

float nn_sample_constant_float(
    const NnMotionFloatKey* keys,
    std::size_t count,
    float frame,
    CameraPrecision precision) {
    require_precision(precision);
    require_float_keys(keys, count);
    require_float_frame(frame, keys[0u]);
    return keys[find_float_lower_key(keys, count, frame)].value;
}

std::optional<std::int16_t> nn_evaluate_constant_a16(
    std::uint32_t flags,
    float start,
    float end,
    float frame,
    const NnMotionA16Key* keys,
    std::size_t count,
    CameraPrecision precision) {
    require_constant_a16_flags(flags);
    const NnMotionFrameResult mapped = nn_map_motion_frame(flags, start, end, frame, precision);
    if (!mapped.active) {
        return std::nullopt;
    }
    return nn_sample_constant_a16(keys, count, mapped.frame, precision);
}

std::optional<float> nn_evaluate_scalar_float(
    std::uint32_t flags,
    float start,
    float end,
    float frame,
    const NnMotionFloatKey* keys,
    std::size_t count,
    CameraPrecision precision) {
    const std::uint32_t interpolation = require_scalar_float_flags(flags);
    const NnMotionFrameResult mapped = nn_map_motion_frame(flags, start, end, frame, precision);
    if (!mapped.active) {
        return std::nullopt;
    }
    if (interpolation == kLinearInterpolation) {
        return nn_sample_linear_float(keys, count, mapped.frame, precision);
    }
    return nn_sample_constant_float(keys, count, mapped.frame, precision);
}
