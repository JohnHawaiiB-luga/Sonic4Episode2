#include "object_movement.h"

#include "object_speed.h"
#include "nn_trig.h"

#include <cmath>
#include <stdexcept>

namespace {

constexpr std::uint32_t kMoveGrounded = 0x00000001u;
constexpr std::uint32_t kMoveSurfaceMotion = 0x00000040u;
constexpr std::uint32_t kMoveGravity = 0x00000080u;
constexpr std::uint32_t kMoveSuppressGroundProjection = 0x00008000u;
constexpr std::uint32_t kMoveSlopeAcceleration = 0x00020000u;
constexpr std::uint32_t kMoveSuppressScroll = 0x04000000u;
constexpr std::uint32_t kMoveClearFlow = 0x08000000u;

[[noreturn]] void fail(const char* message) {
    throw std::invalid_argument(message);
}

void require_precision(ObjectMovementPrecision precision) {
    if (precision != ObjectMovementPrecision::Single && precision != ObjectMovementPrecision::Double) {
        fail("object movement precision is unsupported");
    }
}

void require_finite(float value, const char* message) {
    if (!std::isfinite(value)) {
        fail(message);
    }
}

void require_vector(const ObjectMovementVector& value, const char* message) {
    require_finite(value.x, message);
    require_finite(value.y, message);
    require_finite(value.z, message);
}

void validate_inputs(const ObjectMovementState& state, const ObjectMovementEnvironment& environment) {
    require_vector(state.position, "object movement position is nonfinite");
    require_vector(state.velocity, "object movement velocity is nonfinite");
    require_vector(state.velocity_add, "object movement velocity addition is nonfinite");
    require_vector(state.flow, "object movement flow is nonfinite");
    require_finite(state.ground_speed, "object movement ground speed is nonfinite");
    require_finite(state.gravity, "object movement gravity is nonfinite");
    require_finite(state.maximum_fall_speed, "object movement fall-speed maximum is nonfinite");
    require_finite(state.hitstop_timer, "object movement hitstop timer is nonfinite");
    require_finite(environment.time_scale, "object movement time scale is nonfinite");
    require_finite(environment.scroll_x, "object movement X scroll is nonfinite");
    require_finite(environment.scroll_y, "object movement Y scroll is nonfinite");
    if (environment.time_scale < 0.0f) {
        fail("object movement time scale is negative");
    }
}

void validate_slope_parameters(const ObjectSlopeParameters& parameters) {
    require_finite(parameters.downhill_acceleration,
        "object movement downhill slope acceleration is nonfinite");
    require_finite(parameters.uphill_acceleration,
        "object movement uphill slope acceleration is nonfinite");
    require_finite(parameters.maximum_speed,
        "object movement slope speed maximum is nonfinite");
    if (parameters.maximum_speed < 0.0f) {
        fail("object movement slope speed maximum is negative");
    }
}

struct Arithmetic {
    ObjectMovementPrecision precision;

    double rounded(double value) const {
        if (precision == ObjectMovementPrecision::Double || !std::isfinite(value)) {
            return value;
        }
        int exponent = 0;
        const float mantissa = static_cast<float>(std::frexp(value, &exponent));
        return std::ldexp(static_cast<double>(mantissa), exponent);
    }

    double add(double left, double right) const {
        return rounded(left + right);
    }

    double multiply(double left, double right) const {
        return rounded(left * right);
    }

    float spill(double value) const {
        return static_cast<float>(value);
    }
};

bool has_move_flag(const ObjectMovementState& state, std::uint32_t flag) {
    return (state.move_flags & flag) != 0u;
}

bool slope_acceleration_would_run(const ObjectMovementState& state) {
    if (!has_move_flag(state, kMoveSurfaceMotion) ||
        !has_move_flag(state, kMoveSlopeAcceleration)) {
        return false;
    }
    const std::uint16_t shifted_angle = static_cast<std::uint16_t>(
        static_cast<std::uint32_t>(state.surface_angle) +
        static_cast<std::uint32_t>(state.slope_acceleration_start_angle));
    const std::uint32_t activation_angle =
        static_cast<std::uint32_t>(state.slope_acceleration_start_angle) * 2u;
    return static_cast<std::uint32_t>(shifted_angle) >= activation_angle;
}

CameraPrecision camera_precision(ObjectMovementPrecision precision) {
    return precision == ObjectMovementPrecision::Single
        ? CameraPrecision::Single
        : CameraPrecision::Double;
}

ObjectSpeedPrecision object_speed_precision(ObjectMovementPrecision precision) {
    return precision == ObjectMovementPrecision::Single
        ? ObjectSpeedPrecision::Single
        : ObjectSpeedPrecision::Double;
}

std::int32_t signed_surface_angle(std::uint16_t angle) {
    return angle < 0x8000u
        ? static_cast<std::int32_t>(angle)
        : static_cast<std::int32_t>(angle) - 0x10000;
}

float clamp_slope_speed(float speed, float maximum) {
    if (speed > maximum) {
        return maximum;
    }
    const float negative_maximum = -maximum;
    return speed < negative_maximum ? negative_maximum : speed;
}

float scale(float value, float time_scale, const Arithmetic& arithmetic) {
    return arithmetic.spill(arithmetic.multiply(
        static_cast<double>(value),
        static_cast<double>(time_scale)));
}

}

float object_slope_speed_delta(
    const ObjectMovementState& state,
    ObjectMovementPrecision precision) {
    require_precision(precision);
    require_finite(state.ground_speed, "object movement ground speed is nonfinite");
    if (!state.slope_parameters.has_value()) {
        fail("object movement slope parameters are unavailable");
    }
    const ObjectSlopeParameters& parameters = *state.slope_parameters;
    validate_slope_parameters(parameters);

    const double signed_angle_speed = static_cast<double>(signed_surface_angle(state.surface_angle)) *
        static_cast<double>(state.ground_speed);
    const float acceleration = signed_angle_speed >= 0.0
        ? parameters.downhill_acceleration
        : parameters.uphill_acceleration;
    const auto rotation = nn_sin_cos(state.surface_angle, camera_precision(precision));
    const Arithmetic arithmetic{precision};
    return arithmetic.spill(arithmetic.multiply(
        static_cast<double>(rotation.sine),
        static_cast<double>(acceleration)));
}

ObjectMovementStatus update_object_movement(
    ObjectMovementState& state,
    const ObjectMovementEnvironment& environment,
    ObjectMovementPrecision precision) {
    require_precision(precision);
    validate_inputs(state, environment);
    if (state.fall_orientation != 0u) {
        return ObjectMovementStatus::UnsupportedFallOrientation;
    }
    if (state.slope_parameters.has_value()) {
        validate_slope_parameters(*state.slope_parameters);
    }
    const bool slope_operation = state.hitstop_timer == 0.0f && slope_acceleration_would_run(state);
    if (slope_operation && !state.slope_parameters.has_value()) {
        return ObjectMovementStatus::UnsupportedSlopeAcceleration;
    }

    const Arithmetic arithmetic{precision};
    state.previous_position = state.position;
    if (has_move_flag(state, kMoveClearFlow)) {
        state.flow = {0.0f, 0.0f, 0.0f};
    }

    if (state.hitstop_timer != 0.0f) {
        state.movement.x = scale(state.flow.x, environment.time_scale, arithmetic);
        state.movement.y = scale(state.flow.y, environment.time_scale, arithmetic);
        state.movement.z = scale(state.flow.z, environment.time_scale, arithmetic);
    } else {
        if (!has_move_flag(state, kMoveGrounded) && has_move_flag(state, kMoveGravity)) {
            const double gravity_delta = arithmetic.multiply(
                static_cast<double>(state.gravity),
                static_cast<double>(environment.time_scale));
            state.velocity.y = arithmetic.spill(arithmetic.add(
                gravity_delta, static_cast<double>(state.velocity.y)));
            if (state.velocity.y > state.maximum_fall_speed) {
                state.velocity.y = state.maximum_fall_speed;
            }
        }

        if (slope_operation) {
            const ObjectSlopeParameters& parameters = *state.slope_parameters;
            const float slope_delta = object_slope_speed_delta(state, precision);
            if (slope_delta != 0.0f) {
                state.ground_speed = object_speed_up(
                    state.ground_speed,
                    slope_delta,
                    parameters.maximum_speed,
                    environment.time_scale,
                    object_speed_precision(precision));
            } else {
                state.ground_speed = clamp_slope_speed(state.ground_speed, parameters.maximum_speed);
            }
        }

        float projected_x = 0.0f;
        float projected_y = 0.0f;
        if (has_move_flag(state, kMoveSurfaceMotion) &&
            !has_move_flag(state, kMoveSuppressGroundProjection)) {
            const auto rotation = nn_sin_cos(state.surface_angle, camera_precision(precision));
            const float cosine = rotation.cosine;
            const float sine = rotation.sine;
            projected_x = arithmetic.spill(arithmetic.multiply(
                static_cast<double>(cosine), static_cast<double>(state.ground_speed)));
            projected_y = arithmetic.spill(arithmetic.multiply(
                static_cast<double>(sine), static_cast<double>(state.ground_speed)));
        }

        double movement_x = arithmetic.add(
            static_cast<double>(state.velocity.x),
            static_cast<double>(projected_x));
        movement_x = arithmetic.add(movement_x, static_cast<double>(state.flow.x));
        if (!has_move_flag(state, kMoveSuppressScroll)) {
            movement_x = arithmetic.add(movement_x, static_cast<double>(environment.scroll_x));
        }
        state.movement.x = arithmetic.spill(arithmetic.multiply(
            movement_x,
            static_cast<double>(environment.time_scale)));

        double movement_y = arithmetic.add(
            static_cast<double>(state.velocity.y),
            static_cast<double>(projected_y));
        movement_y = arithmetic.add(movement_y, static_cast<double>(state.flow.y));
        if (!has_move_flag(state, kMoveSuppressScroll)) {
            movement_y = arithmetic.add(movement_y, static_cast<double>(environment.scroll_y));
        }
        state.movement.y = arithmetic.spill(arithmetic.multiply(
            movement_y,
            static_cast<double>(environment.time_scale)));

        double movement_z = arithmetic.add(static_cast<double>(state.velocity.z), 0.0);
        movement_z = arithmetic.add(movement_z, static_cast<double>(state.flow.z));
        state.movement.z = arithmetic.spill(arithmetic.multiply(
            movement_z,
            static_cast<double>(environment.time_scale)));
    }

    state.position.x = arithmetic.spill(arithmetic.add(
        static_cast<double>(state.movement.x),
        static_cast<double>(state.position.x)));
    state.position.y = arithmetic.spill(arithmetic.add(
        static_cast<double>(state.movement.y),
        static_cast<double>(state.position.y)));
    state.position.z = arithmetic.spill(arithmetic.add(
        static_cast<double>(state.movement.z),
        static_cast<double>(state.position.z)));

    state.velocity.x = arithmetic.spill(arithmetic.add(
        static_cast<double>(state.velocity_add.x),
        static_cast<double>(state.velocity.x)));
    state.velocity.y = arithmetic.spill(arithmetic.add(
        static_cast<double>(state.velocity_add.y),
        static_cast<double>(state.velocity.y)));
    state.velocity.z = arithmetic.spill(arithmetic.add(
        static_cast<double>(state.velocity_add.z),
        static_cast<double>(state.velocity.z)));
    state.flow = {0.0f, 0.0f, 0.0f};
    return ObjectMovementStatus::Applied;
}
