#include "normal_camera_follow.h"

#include "object_speed.h"

#include <cmath>
#include <cstddef>
#include <stdexcept>

namespace {

constexpr float kInputLimit = 16777216.0f;
constexpr float kAllowX = 15.0f;
constexpr float kAllowY = 50.0f;
constexpr float kAllowYDecay = 4.0f;
constexpr float kInternalZ = 50.0f;
constexpr std::array<float, 3u> kSpeedAcceleration{{3.0f, 3.0f, 0.5f}};
constexpr std::array<float, 3u> kSpeedMaximum{{16.0f, 16.0f, 4.0f}};

[[noreturn]] void fail(const char* message) {
    throw std::invalid_argument(message);
}

void require_precision(CameraPrecision precision) {
    if (precision != CameraPrecision::Single && precision != CameraPrecision::Double) {
        fail("normal camera follow precision is unsupported");
    }
}

void require_finite(float value, const char* message) {
    if (!std::isfinite(value) || std::fabs(value) > kInputLimit) {
        fail(message);
    }
}

void require_nonnegative(float value, const char* message) {
    require_finite(value, message);
    if (value < 0.0f) {
        fail(message);
    }
}

void require_vector(const std::array<float, 3u>& value, const char* message) {
    for (const float component : value) {
        require_finite(component, message);
    }
}

void require_nonnegative_vector(const std::array<float, 3u>& value, const char* message) {
    for (const float component : value) {
        require_nonnegative(component, message);
    }
}

ObjectSpeedPrecision speed_precision(CameraPrecision precision) {
    return precision == CameraPrecision::Single
        ? ObjectSpeedPrecision::Single
        : ObjectSpeedPrecision::Double;
}

struct Arithmetic {
    CameraPrecision precision;

    double rounded(double value) const {
        if (precision == CameraPrecision::Double) {
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

    float spill(double value) const {
        return static_cast<float>(value);
    }
};

void validate_inputs(
    const NormalCameraFollowState& state,
    const std::array<float, 3u>& subject_position,
    float time_scale,
    CameraPrecision precision) {
    require_precision(precision);
    require_vector(state.internal_position, "normal camera internal position is invalid");
    require_nonnegative_vector(state.speed, "normal camera speed is invalid");
    require_nonnegative_vector(state.allowance, "normal camera allowance is invalid");
    require_vector(subject_position, "normal camera subject position is invalid");
    require_nonnegative(time_scale, "normal camera time scale is invalid");
}

float constrain_to_deadzone(float desired, float internal, float allowance, const Arithmetic& arithmetic) {
    const double lower = arithmetic.subtract(internal, allowance);
    const double upper = arithmetic.add(internal, allowance);
    if (desired < lower) {
        return arithmetic.spill(arithmetic.add(desired, allowance));
    }
    if (desired > upper) {
        return arithmetic.spill(arithmetic.subtract(desired, allowance));
    }
    return internal;
}

float advance_toward(float internal, float desired, float speed, const Arithmetic& arithmetic) {
    if (desired > internal) {
        const float next = arithmetic.spill(arithmetic.add(internal, speed));
        return next > desired ? desired : next;
    }
    const float next = arithmetic.spill(arithmetic.subtract(internal, speed));
    return next < desired ? desired : next;
}

}

std::array<float, 3u> update_normal_camera_follow(
    NormalCameraFollowState& state,
    const std::array<float, 3u>& subject_position,
    bool move_flag_10,
    float time_scale,
    CameraPrecision precision) {
    validate_inputs(state, subject_position, time_scale, precision);

    const Arithmetic arithmetic{precision};
    const ObjectSpeedPrecision object_precision = speed_precision(precision);
    NormalCameraFollowState next = state;
    next.allowance[0u] = kAllowX;
    next.allowance[2u] = 0.0f;
    if (move_flag_10) {
        next.allowance[1u] = kAllowY;
    } else {
        const float decayed = arithmetic.spill(arithmetic.subtract(next.allowance[1u], kAllowYDecay));
        next.allowance[1u] = decayed > 0.0f ? decayed : 0.0f;
    }

    std::array<float, 3u> desired{{
        subject_position[0u],
        arithmetic.spill(arithmetic.subtract(6.0, subject_position[1u])),
        subject_position[2u],
    }};
    for (std::size_t index = 0u; index < 2u; ++index) {
        desired[index] = constrain_to_deadzone(
            desired[index], next.internal_position[index], next.allowance[index], arithmetic);
    }
    for (std::size_t index = 0u; index < desired.size(); ++index) {
        next.speed[index] = desired[index] == next.internal_position[index]
            ? object_speed_down(next.speed[index], kSpeedAcceleration[index], time_scale, object_precision)
            : object_speed_up(
                  next.speed[index],
                  kSpeedAcceleration[index],
                  kSpeedMaximum[index],
                  time_scale,
                  object_precision);
        next.internal_position[index] = advance_toward(
            next.internal_position[index], desired[index], next.speed[index], arithmetic);
    }
    next.internal_position[2u] = kInternalZ;

    state = next;
    return state.internal_position;
}
