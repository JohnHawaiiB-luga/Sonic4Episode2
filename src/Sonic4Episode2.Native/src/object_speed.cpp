#include "object_speed.h"

#include <cmath>
#include <stdexcept>

namespace {

constexpr float kInputLimit = 16777216.0f;

[[noreturn]] void fail(const char* message) {
    throw std::invalid_argument(message);
}

void require_precision(ObjectSpeedPrecision precision) {
    if (precision != ObjectSpeedPrecision::Single && precision != ObjectSpeedPrecision::Double) {
        fail("object speed precision is unsupported");
    }
}

void require_finite_bounded(float value, const char* message) {
    if (!std::isfinite(value) || std::fabs(value) > kInputLimit) {
        fail(message);
    }
}

void require_nonnegative_finite_bounded(float value, const char* message) {
    require_finite_bounded(value, message);
    if (value < 0.0f) {
        fail(message);
    }
}

struct Arithmetic {
    ObjectSpeedPrecision precision;

    double rounded(double value) const {
        if (precision == ObjectSpeedPrecision::Double || !std::isfinite(value)) {
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

    float spill(double value) const {
        return static_cast<float>(value);
    }
};

void require_speed_up_inputs(float speed, float acceleration, float maximum, float time_scale) {
    require_finite_bounded(speed, "object speed-up speed is nonfinite or out of range");
    require_finite_bounded(acceleration, "object speed-up acceleration is nonfinite or out of range");
    require_nonnegative_finite_bounded(maximum, "object speed-up maximum is negative, nonfinite, or out of range");
    require_nonnegative_finite_bounded(time_scale, "object speed-up time scale is negative, nonfinite, or out of range");
}

void require_speed_down_inputs(float speed, float deceleration, float time_scale) {
    require_finite_bounded(speed, "object speed-down speed is nonfinite or out of range");
    require_nonnegative_finite_bounded(deceleration, "object speed-down deceleration is negative, nonfinite, or out of range");
    require_nonnegative_finite_bounded(time_scale, "object speed-down time scale is negative, nonfinite, or out of range");
}

}

float object_speed_up(
    float speed,
    float acceleration,
    float maximum,
    float time_scale,
    ObjectSpeedPrecision precision) {
    require_precision(precision);
    require_speed_up_inputs(speed, acceleration, maximum, time_scale);

    const Arithmetic arithmetic{precision};
    const double scaled_acceleration = arithmetic.multiply(
        static_cast<double>(acceleration),
        static_cast<double>(time_scale));
    const float stored = arithmetic.spill(arithmetic.add(static_cast<double>(speed), scaled_acceleration));
    if (maximum == 0.0f) {
        return stored;
    }
    if (acceleration >= 0.0f) {
        return stored > maximum ? maximum : stored;
    }
    const float negative_maximum = -maximum;
    return stored < negative_maximum ? negative_maximum : stored;
}

float object_speed_down(
    float speed,
    float deceleration,
    float time_scale,
    ObjectSpeedPrecision precision) {
    require_precision(precision);
    require_speed_down_inputs(speed, deceleration, time_scale);

    const Arithmetic arithmetic{precision};
    const double scaled_deceleration = arithmetic.multiply(
        static_cast<double>(deceleration),
        static_cast<double>(time_scale));
    const bool initially_positive = speed > 0.0f;
    const float stored = initially_positive
        ? arithmetic.spill(arithmetic.subtract(static_cast<double>(speed), scaled_deceleration))
        : arithmetic.spill(arithmetic.add(static_cast<double>(speed), scaled_deceleration));
    if (initially_positive) {
        return stored < 0.0f ? 0.0f : stored;
    }
    return stored > 0.0f ? 0.0f : stored;
}
