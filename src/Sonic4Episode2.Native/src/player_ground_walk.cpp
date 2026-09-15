#include "player_ground_walk.h"

#include "nn_trig.h"

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace {

constexpr float kInputLimit = 16777216.0f;
constexpr double kDownhillReleaseMultiplier = 0.4000000059604645;

void require_finite(float value) {
    if (!std::isfinite(value) || std::fabs(value) > kInputLimit) {
        throw std::invalid_argument("ground walk value is nonfinite or out of range");
    }
}

void require_nonnegative(float value) {
    require_finite(value);
    if (value < 0.0f) {
        throw std::invalid_argument("ground walk parameter is negative");
    }
}

void require_direction(PlayerWalkDirection direction) {
    if (direction != PlayerWalkDirection::None && direction != PlayerWalkDirection::Left &&
        direction != PlayerWalkDirection::Right) {
        throw std::invalid_argument("ground walk direction is unsupported");
    }
}

bool release_angle_gate(std::uint16_t angle) {
    const std::uint16_t shifted = static_cast<std::uint16_t>(
        static_cast<std::uint32_t>(angle) + 0x2000u);
    return (shifted & 0xff00u) <= 0x4000u;
}

bool preserves_previous_maximum(float ground_speed, std::uint16_t angle) {
    return (ground_speed > 0.0f && angle < 0x8000u) ||
           (ground_speed < 0.0f && angle > 0x8000u);
}

bool forward_downhill(
    float ground_speed,
    std::uint16_t angle,
    bool facing_left,
    std::uint16_t start_angle) {
    return (ground_speed > 0.0f && !facing_left && angle <= start_angle) ||
           (ground_speed < 0.0f && facing_left &&
               static_cast<std::uint32_t>(angle) >= 0x10000u - static_cast<std::uint32_t>(start_angle));
}

bool non_near_flat(std::uint16_t angle) {
    return static_cast<std::uint16_t>(static_cast<std::uint32_t>(angle) + 0x20u) > 0x40u;
}

CameraPrecision trig_precision(ObjectSpeedPrecision precision) {
    return precision == ObjectSpeedPrecision::Single ? CameraPrecision::Single : CameraPrecision::Double;
}

struct Arithmetic {
    ObjectSpeedPrecision precision;

    double rounded(double value) const {
        if (precision == ObjectSpeedPrecision::Double || !std::isfinite(value)) return value;
        int exponent = 0;
        const float mantissa = static_cast<float>(std::frexp(value, &exponent));
        return std::ldexp(static_cast<double>(mantissa), exponent);
    }

    double add(double left, double right) const { return rounded(left + right); }
    double subtract(double left, double right) const { return rounded(left - right); }
    double multiply(double left, double right) const { return rounded(left * right); }
    double divide(double left, double right) const { return rounded(left / right); }
    float spill(double value) const { return static_cast<float>(value); }
};

float clamp_to_maximum(float value, float maximum) {
    if (value > maximum) return maximum;
    const float negative_maximum = -maximum;
    if (value < negative_maximum) return negative_maximum;
    return value;
}

}

void advance_flat_player_walk(
    PlayerGroundWalkState& state,
    const PlayerGroundWalkParameters& parameters,
    PlayerWalkDirection direction,
    std::int32_t input_magnitude,
    float time_scale,
    ObjectSpeedPrecision precision) {
    if (precision != ObjectSpeedPrecision::Single && precision != ObjectSpeedPrecision::Double) {
        throw std::invalid_argument("ground walk precision is unsupported");
    }
    require_direction(direction);
    if (input_magnitude < -32767 || input_magnitude > 32767) {
        throw std::invalid_argument("ground walk input magnitude is out of range");
    }
    require_finite(state.ground_speed);
    require_finite(state.horizontal_velocity);
    require_nonnegative(state.previous_maximum);
    require_nonnegative(state.working_maximum);
    require_nonnegative(state.deceleration_delay);
    require_nonnegative(parameters.acceleration);
    require_nonnegative(parameters.maximum_speed);
    require_nonnegative(parameters.deceleration);
    require_nonnegative(parameters.attenuation_speed);
    require_nonnegative(parameters.slope_cap_addition);
    require_nonnegative(parameters.slope_acceleration);
    require_nonnegative(parameters.slope_speed_cap);
    require_nonnegative(time_scale);

    const Arithmetic arithmetic{precision};
    const CameraPrecision sine_precision = trig_precision(precision);
    PlayerGroundWalkState next = state;
    float maximum = parameters.maximum_speed;
    float acceleration = parameters.acceleration;
    float deceleration = parameters.deceleration;
    if (direction != PlayerWalkDirection::None) {
        const std::int32_t magnitude = std::min(std::abs(input_magnitude), 28672);
        maximum = arithmetic.spill(arithmetic.divide(
            arithmetic.multiply(static_cast<double>(maximum), static_cast<double>(magnitude)), 28672.0));
    }
    if (maximum < next.previous_maximum) {
        if (direction == PlayerWalkDirection::None &&
            preserves_previous_maximum(next.ground_speed, next.surface_angle)) {
            maximum = next.previous_maximum;
        } else {
            maximum = arithmetic.spill(arithmetic.subtract(next.previous_maximum, deceleration));
            if (maximum < 0.0f) maximum = 0.0f;
        }
    }
    next.previous_maximum = maximum;

    if (next.surface_angle != 0u) {
        const float slope_cap_addition = arithmetic.spill(arithmetic.multiply(
            static_cast<double>(nn_sin_cos(next.surface_angle, sine_precision).sine),
            static_cast<double>(parameters.slope_cap_addition)));
        if (slope_cap_addition > 0.0f) {
            maximum = arithmetic.spill(arithmetic.add(maximum, slope_cap_addition));
        }
    }

    if (next.deceleration_delay != 0.0f) {
        deceleration = 0.0f;
    } else {
        float ratio = 0.0f;
        const float speed = std::fabs(next.ground_speed);
        if (speed > parameters.attenuation_speed) {
            const double denominator = arithmetic.subtract(maximum, parameters.attenuation_speed);
            ratio = denominator != 0.0
                ? arithmetic.spill(arithmetic.divide(
                      arithmetic.subtract(speed, parameters.attenuation_speed), denominator))
                : 1.0f;
            if (ratio > 1.0f) ratio = 1.0f;
            ratio = arithmetic.spill(arithmetic.multiply(ratio, 0.96875));
        }
        acceleration = arithmetic.spill(arithmetic.subtract(
            acceleration, arithmetic.multiply(acceleration, ratio)));
    }
    const float reversal_deceleration = arithmetic.spill(
        arithmetic.multiply(deceleration, static_cast<double>(1.3f)));

    const float absolute_speed = std::fabs(next.ground_speed);
    if (next.working_maximum >= maximum && absolute_speed >= maximum) {
        if (next.working_maximum > next.ground_speed) next.working_maximum = absolute_speed;
        maximum = next.working_maximum;
    }
    if (direction != PlayerWalkDirection::None) {
        const bool right = direction == PlayerWalkDirection::Right;
        if ((right && next.ground_speed < 0.0f) || (!right && next.ground_speed > 0.0f)) {
            next.ground_speed = object_speed_down(next.ground_speed, reversal_deceleration, time_scale, precision);
        }
        next.ground_speed = object_speed_up(
            next.ground_speed, right ? acceleration : -acceleration, maximum, time_scale, precision);
        state = next;
        return;
    }

    next.spin_charge = 0u;
    next.horizontal_velocity = std::clamp(next.horizontal_velocity, -maximum, maximum);
    next.ground_speed = std::clamp(next.ground_speed, -maximum, maximum);
    if (!release_angle_gate(next.surface_angle)) {
        state = next;
        return;
    }
    if (next.inhibit_release_deceleration) {
        next.inhibit_release_deceleration = false;
        state = next;
        return;
    }

    const float release_absolute_speed = std::fabs(next.ground_speed);
    const bool downhill = non_near_flat(next.surface_angle) && forward_downhill(
        next.ground_speed, next.surface_angle, next.facing_left, parameters.slope_acceleration_start_angle);
    if (release_absolute_speed >= deceleration && downhill) {
        const float cosine = nn_sin_cos(next.surface_angle, sine_precision).cosine;
        deceleration = arithmetic.spill(arithmetic.multiply(
            arithmetic.multiply(static_cast<double>(cosine), static_cast<double>(parameters.slope_acceleration)),
            kDownhillReleaseMultiplier));
    }
    next.ground_speed = object_speed_down(next.ground_speed, deceleration, time_scale, precision);

    if (non_near_flat(next.surface_angle) && next.ground_speed != 0.0f && forward_downhill(
            next.ground_speed,
            next.surface_angle,
            next.facing_left,
            parameters.slope_acceleration_start_angle)) {
        const float slope_delta = arithmetic.spill(arithmetic.multiply(
            static_cast<double>(nn_sin_cos(next.surface_angle, sine_precision).sine),
            static_cast<double>(parameters.slope_acceleration)));
        if (slope_delta != 0.0f) {
            next.ground_speed = object_speed_up(
                next.ground_speed, slope_delta, parameters.slope_speed_cap, time_scale, precision);
        } else {
            next.ground_speed = clamp_to_maximum(next.ground_speed, parameters.slope_speed_cap);
        }
    }

    state = next;
}
