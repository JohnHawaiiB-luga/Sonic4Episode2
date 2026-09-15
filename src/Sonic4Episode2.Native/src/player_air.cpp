#include "player_air.h"

#include <cmath>
#include <stdexcept>

namespace {

constexpr float kInputLimit = 16777216.0f;

[[noreturn]] void fail(const char* message) {
    throw std::invalid_argument(message);
}

void require_precision(ObjectSpeedPrecision precision) {
    if (precision != ObjectSpeedPrecision::Single && precision != ObjectSpeedPrecision::Double) {
        fail("player air precision is unsupported");
    }
}

void require_direction(PlayerWalkDirection direction) {
    if (direction != PlayerWalkDirection::None && direction != PlayerWalkDirection::Left &&
        direction != PlayerWalkDirection::Right) {
        fail("player air direction is unsupported");
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

struct Arithmetic {
    ObjectSpeedPrecision precision;

    double rounded(double value) const {
        if (precision == ObjectSpeedPrecision::Double || !std::isfinite(value)) {
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

float clamp_to_maximum(float value, float maximum, const Arithmetic& arithmetic) {
    if (value > maximum) {
        return arithmetic.spill(maximum);
    }
    const float negative_maximum = -maximum;
    if (value < negative_maximum) {
        return arithmetic.spill(negative_maximum);
    }
    return arithmetic.spill(value);
}

void validate_inputs(
    const PlayerAirState& state,
    const PlayerAirParameters& parameters,
    PlayerWalkDirection direction,
    float time_scale,
    ObjectSpeedPrecision precision) {
    require_precision(precision);
    require_direction(direction);
    require_finite(state.vx, "player air horizontal speed is nonfinite or out of range");
    require_finite(state.ground_speed, "player air ground speed is nonfinite or out of range");
    require_finite(state.working_maximum, "player air working maximum is nonfinite or out of range");
    require_nonnegative(state.deceleration_delay, "player air deceleration delay is invalid");
    require_nonnegative(parameters.acceleration, "player air acceleration is invalid");
    require_nonnegative(parameters.maximum_speed, "player air maximum speed is invalid");
    require_nonnegative(parameters.deceleration, "player air deceleration is invalid");
    require_nonnegative(parameters.attenuation_threshold, "player air attenuation threshold is invalid");
    require_nonnegative(time_scale, "player air time scale is invalid");
}

}

void advance_ordinary_player_air(
    PlayerAirState& state,
    const PlayerAirParameters& parameters,
    PlayerWalkDirection direction,
    float time_scale,
    ObjectSpeedPrecision precision) {
    validate_inputs(state, parameters, direction, time_scale, precision);

    const Arithmetic arithmetic{precision};
    PlayerAirState next = state;
    next.working_maximum = 0.0f;

    float acceleration = arithmetic.spill(parameters.acceleration);
    float deceleration = arithmetic.spill(parameters.deceleration);
    const float base_deceleration = arithmetic.spill(parameters.deceleration);
    const float maximum = arithmetic.spill(parameters.maximum_speed);
    const float attenuation_threshold = arithmetic.spill(parameters.attenuation_threshold);

    const std::uint16_t shifted_surface_angle = static_cast<std::uint16_t>(
        static_cast<std::uint32_t>(next.surface_angle) + 0x2000u);
    if ((shifted_surface_angle & 0xc000u) != 0u || next.surface_angle == 0xe000u) {
        deceleration = arithmetic.spill(arithmetic.multiply(deceleration, 0.25));
    }

    if (next.deceleration_delay > 0.0f) {
        deceleration = 0.0f;
        acceleration = arithmetic.spill(arithmetic.multiply(acceleration, 0.25));
    } else {
        float ratio = 0.0f;
        const float absolute_vx = arithmetic.spill(std::fabs(static_cast<double>(next.vx)));
        if (absolute_vx > attenuation_threshold) {
            const double denominator = arithmetic.subtract(maximum, attenuation_threshold);
            ratio = denominator != 0.0
                ? arithmetic.spill(arithmetic.divide(
                      arithmetic.subtract(absolute_vx, attenuation_threshold), denominator))
                : 1.0f;
            if (ratio > 1.0f) {
                ratio = 1.0f;
            }
            ratio = arithmetic.spill(arithmetic.multiply(ratio, 0.96875));
        }
        acceleration = arithmetic.spill(arithmetic.subtract(
            acceleration, arithmetic.multiply(acceleration, ratio)));
    }

    if (next.half_acceleration) {
        acceleration = arithmetic.spill(arithmetic.multiply(acceleration, 0.5));
        deceleration = arithmetic.spill(arithmetic.multiply(deceleration, 0.5));
    }

    const float reverse_deceleration = arithmetic.spill(arithmetic.add(deceleration, deceleration));
    switch (direction) {
    case PlayerWalkDirection::Left:
        if (next.vx > 0.0f) {
            next.vx = object_speed_down(next.vx, reverse_deceleration, time_scale, precision);
            next.ground_speed = object_speed_down(next.ground_speed, reverse_deceleration, time_scale, precision);
        }
        next.ground_speed = object_speed_down(next.ground_speed, base_deceleration, time_scale, precision);
        next.vx = object_speed_up(next.vx, -acceleration, maximum, time_scale, precision);
        break;
    case PlayerWalkDirection::Right:
        if (next.vx < 0.0f) {
            next.vx = object_speed_down(next.vx, reverse_deceleration, time_scale, precision);
            next.ground_speed = object_speed_down(next.ground_speed, reverse_deceleration, time_scale, precision);
        }
        next.ground_speed = object_speed_down(next.ground_speed, base_deceleration, time_scale, precision);
        next.vx = object_speed_up(next.vx, acceleration, maximum, time_scale, precision);
        break;
    case PlayerWalkDirection::None:
        next.vx = clamp_to_maximum(next.vx, maximum, arithmetic);
        next.ground_speed = clamp_to_maximum(next.ground_speed, maximum, arithmetic);
        next.speed_pool = 0u;
        next.vx = object_speed_down(next.vx, deceleration, time_scale, precision);
        next.ground_speed = object_speed_down(next.ground_speed, base_deceleration, time_scale, precision);
        break;
    }

    state = next;
}
