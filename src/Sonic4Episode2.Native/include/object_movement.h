#pragma once

#include <cstdint>
#include <optional>

enum class ObjectMovementPrecision {
    Single,
    Double,
};

enum class ObjectMovementStatus {
    Applied,
    UnsupportedFallOrientation,
    UnsupportedSlopeAcceleration,
};

struct ObjectMovementVector {
    float x;
    float y;
    float z;
};

struct ObjectMovementEnvironment {
    float time_scale;
    float scroll_x;
    float scroll_y;
};

struct ObjectSlopeParameters {
    float downhill_acceleration;
    float uphill_acceleration;
    float maximum_speed;
};

struct ObjectMovementState {
    ObjectMovementVector position;
    ObjectMovementVector previous_position;
    ObjectMovementVector velocity;
    ObjectMovementVector velocity_add;
    ObjectMovementVector flow;
    ObjectMovementVector movement;
    float ground_speed;
    float gravity;
    float maximum_fall_speed;
    float hitstop_timer;
    std::uint16_t surface_angle;
    std::uint16_t slope_acceleration_start_angle;
    std::uint16_t fall_orientation;
    std::uint32_t move_flags;
    std::optional<ObjectSlopeParameters> slope_parameters;
};

float object_slope_speed_delta(
    const ObjectMovementState& state,
    ObjectMovementPrecision precision);

ObjectMovementStatus update_object_movement(
    ObjectMovementState& state,
    const ObjectMovementEnvironment& environment,
    ObjectMovementPrecision precision);
