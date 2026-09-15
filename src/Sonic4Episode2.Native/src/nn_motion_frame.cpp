#include "nn_motion_frame.h"

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <stdexcept>

namespace {

constexpr std::uint32_t kBypassBit = 0x40u;
constexpr std::uint32_t kModeMask = 0x1f0000u;
constexpr std::uint32_t kIntervalMode = 0x10000u;
constexpr std::uint32_t kClampMode = 0x20000u;
constexpr std::uint32_t kRepeatMode = 0x40000u;
constexpr std::uint32_t kMirrorMode = 0x80000u;
constexpr std::uint32_t kLinearFlagMask = 0xe77u;
constexpr std::uint32_t kLinearFlagValue = 0x2u;
constexpr double kCycleLimit = 1073741824.0;

void require(bool condition, const char* message) {
    if (!condition) {
        throw std::invalid_argument(message);
    }
}

void require_precision(CameraPrecision precision) {
    require(
        precision == CameraPrecision::Single || precision == CameraPrecision::Double,
        "motion frame precision is unsupported");
}

void require_finite_inputs(float start, float end, float frame) {
    require(
        std::isfinite(start) && std::isfinite(end) && std::isfinite(frame),
        "motion frame inputs must be finite");
}

struct Arithmetic {
    CameraPrecision precision;

    double rounded(double value) const {
        if (precision == CameraPrecision::Double || !std::isfinite(value)) {
            return value;
        }
        // x87 precision control narrows the significand without narrowing the exponent.
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

std::uint32_t float_bits(float value) {
    std::uint32_t bits = 0u;
    std::memcpy(&bits, &value, sizeof(bits));
    return bits;
}

unsigned bit_width(std::uint64_t value) {
    unsigned width = 0u;
    do {
        ++width;
        value >>= 1u;
    } while (value != 0u);
    return width;
}

double direct_fimul(float value, std::int32_t integer, CameraPrecision precision) {
    if (integer == 0) {
        return 0.0;
    }

    const std::uint32_t bits = float_bits(value);
    const std::uint32_t exponent_bits = (bits >> 23u) & 0xffu;
    const std::uint64_t significand = exponent_bits == 0u
        ? static_cast<std::uint64_t>(bits & 0x7fffffu)
        : static_cast<std::uint64_t>((bits & 0x7fffffu) | 0x800000u);
    const int exponent = exponent_bits == 0u
        ? -149
        : static_cast<int>(exponent_bits) - 127 - 23;
    const std::uint32_t integer_bits = static_cast<std::uint32_t>(integer);
    const std::uint64_t magnitude = integer < 0
        ? static_cast<std::uint64_t>(std::uint32_t{0} - integer_bits)
        : static_cast<std::uint64_t>(integer_bits);
    const std::uint64_t product = significand * magnitude;
    const unsigned precision_bits = precision == CameraPrecision::Single ? 24u : 53u;
    const unsigned width = bit_width(product);
    unsigned shift = 0u;
    std::uint64_t rounded_significand = product;
    if (width > precision_bits) {
        shift = width - precision_bits;
        rounded_significand = product >> shift;
        const std::uint64_t remainder = product & ((std::uint64_t{1} << shift) - 1u);
        const std::uint64_t halfway = std::uint64_t{1} << (shift - 1u);
        if (remainder > halfway || (remainder == halfway && (rounded_significand & 1u) != 0u)) {
            ++rounded_significand;
        }
        if (rounded_significand == (std::uint64_t{1} << precision_bits)) {
            rounded_significand >>= 1u;
            ++shift;
        }
    }

    const double result = std::ldexp(
        static_cast<double>(rounded_significand),
        exponent + static_cast<int>(shift));
    return integer < 0 ? -result : result;
}

struct CycleState {
    float length;
    float offset;
    float quotient;
    std::int32_t cycle;
};

CycleState make_cycle_state(float start, float end, float frame, const Arithmetic& arithmetic) {
    const float length = arithmetic.spill(arithmetic.subtract(
        static_cast<double>(end),
        static_cast<double>(start)));
    const float offset = arithmetic.spill(arithmetic.subtract(
        static_cast<double>(frame),
        static_cast<double>(start)));
    require(std::isfinite(length) && std::isfinite(offset), "motion frame cycle intermediates are nonfinite");
    require(length > 0.0f, "motion frame cycle length is not positive");

    const float quotient = arithmetic.spill(arithmetic.divide(
        static_cast<double>(offset),
        static_cast<double>(length)));
    require(std::isfinite(quotient), "motion frame cycle quotient is nonfinite");
    require(
        static_cast<double>(quotient) > -kCycleLimit && static_cast<double>(quotient) < kCycleLimit,
        "motion frame cycle quotient is out of range");

    std::int32_t cycle = static_cast<std::int32_t>(std::trunc(static_cast<double>(quotient)));
    if (quotient < 0.0f) {
        --cycle;
    }
    return {length, offset, quotient, cycle};
}

float map_repeat(float frame, const CycleState& cycle, const Arithmetic& arithmetic) {
    const double product = direct_fimul(cycle.length, cycle.cycle, arithmetic.precision);
    return arithmetic.spill(arithmetic.subtract(static_cast<double>(frame), product));
}

float map_mirror(float start, float frame, const CycleState& cycle, const Arithmetic& arithmetic) {
    const float spilled_cycle = static_cast<float>(cycle.cycle);
    if ((static_cast<std::uint32_t>(cycle.cycle) & 1u) != 0u) {
        const double incremented_cycle = arithmetic.add(static_cast<double>(spilled_cycle), 1.0);
        const double product = arithmetic.multiply(static_cast<double>(cycle.length), incremented_cycle);
        const double start_minus_offset = arithmetic.subtract(
            static_cast<double>(start),
            static_cast<double>(cycle.offset));
        return arithmetic.spill(arithmetic.add(start_minus_offset, product));
    }

    const double product = arithmetic.multiply(
        static_cast<double>(cycle.length),
        static_cast<double>(spilled_cycle));
    return arithmetic.spill(arithmetic.subtract(static_cast<double>(frame), product));
}

bool in_interval(float start, float end, float frame) {
    return frame >= start && frame < end;
}

void require_linear_flags(std::uint32_t flags) {
    require(
        (flags & kLinearFlagMask) == kLinearFlagValue,
        "motion A16 evaluation flags are not linear");
}

}

NnMotionFrameResult nn_map_motion_frame(
    std::uint32_t flags,
    float start,
    float end,
    float frame,
    CameraPrecision precision) {
    require_precision(precision);
    require_finite_inputs(start, end, frame);

    if ((flags & kBypassBit) != 0u) {
        return {frame, true};
    }

    const std::uint32_t mode = flags & kModeMask;
    if (mode == kIntervalMode) {
        return {frame, in_interval(start, end, frame)};
    }
    if (mode == kClampMode) {
        if (frame < start) {
            return {start, true};
        }
        if (frame >= end) {
            return {end, true};
        }
        return {frame, true};
    }
    if (mode == kRepeatMode || mode == kMirrorMode) {
        const Arithmetic arithmetic{precision};
        const CycleState cycle = make_cycle_state(start, end, frame, arithmetic);
        if (mode == kRepeatMode) {
            if (in_interval(start, end, frame)) {
                return {frame, true};
            }
            return {map_repeat(frame, cycle, arithmetic), true};
        }
        return {map_mirror(start, frame, cycle, arithmetic), true};
    }
    return {frame, false};
}

std::optional<std::int16_t> nn_evaluate_linear_a16(
    std::uint32_t flags,
    float start,
    float end,
    float frame,
    const NnMotionA16Key* keys,
    std::size_t count,
    CameraPrecision precision) {
    require_linear_flags(flags);
    const NnMotionFrameResult mapped = nn_map_motion_frame(flags, start, end, frame, precision);
    if (!mapped.active) {
        return std::nullopt;
    }
    return nn_sample_linear_a16(keys, count, mapped.frame, precision);
}
