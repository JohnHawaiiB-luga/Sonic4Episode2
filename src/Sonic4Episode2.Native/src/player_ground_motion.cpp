#include "player_ground_motion.h"

namespace {

enum class MotionKind { Idle, Walk, Air };

PlayerGroundMotionStatus advance_motion(
    PlayerGroundMotionState& state,
    const PlayerGroundWalkParameters& parameters,
    PlayerWalkDirection direction,
    std::int32_t input_magnitude,
    const ObjectMovementEnvironment& movement_environment,
    const ObjectContactEnvironment& contact_environment,
    const TerrainCollision& terrain,
    ObjectMovementPrecision precision,
    MotionKind kind,
    const PlayerAirParameters& air_parameters = {}) {
    if (kind == MotionKind::Air && (state.object.move_flags & 1u) != 0u) {
        return PlayerGroundMotionStatus::UnsupportedGrounded;
    }
    if (kind != MotionKind::Air && (state.object.move_flags & 1u) == 0u) {
        return PlayerGroundMotionStatus::UnsupportedAirborne;
    }
    PlayerGroundMotionState next = state;
    if (kind == MotionKind::Walk) {
        next.walking.ground_speed = next.object.ground_speed;
        next.walking.horizontal_velocity = next.object.velocity.x;
        next.walking.surface_angle = next.object.surface_angle;
        next.walking.facing_left = next.contact.flipped_horizontal;
        advance_flat_player_walk(next.walking, parameters, direction, input_magnitude,
            movement_environment.time_scale, precision == ObjectMovementPrecision::Single
                ? ObjectSpeedPrecision::Single : ObjectSpeedPrecision::Double);
        next.object.ground_speed = next.walking.ground_speed;
        next.object.velocity.x = next.walking.horizontal_velocity;
    } else if (kind == MotionKind::Air) {
        PlayerAirState air;
        air.vx = next.object.velocity.x;
        air.ground_speed = next.object.ground_speed;
        air.working_maximum = next.walking.working_maximum;
        air.deceleration_delay = next.walking.deceleration_delay;
        air.surface_angle = next.object.surface_angle;
        air.speed_pool = next.walking.spin_charge;
        advance_ordinary_player_air(air, air_parameters, direction,
            movement_environment.time_scale, precision == ObjectMovementPrecision::Single
                ? ObjectSpeedPrecision::Single : ObjectSpeedPrecision::Double);
        next.object.ground_speed = air.ground_speed;
        next.object.velocity.x = air.vx;
        next.walking.working_maximum = air.working_maximum;
        next.walking.spin_charge = air.speed_pool;
        const int angle = next.object.surface_angle <= 0x8000u
            ? next.object.surface_angle : static_cast<int>(next.object.surface_angle) - 0x10000;
        next.object.surface_angle = static_cast<std::uint16_t>(angle > 0
            ? (angle > 0x200 ? angle - 0x200 : 0)
            : (angle < -0x200 ? angle + 0x200 : 0));
    }
    if (update_object_movement(next.object, movement_environment, precision) != ObjectMovementStatus::Applied) {
        return PlayerGroundMotionStatus::UnsupportedObjectMovement;
    }
    next.contact.position_x = next.object.position.x;
    next.contact.position_y = next.object.position.y;
    next.contact.velocity_x = next.object.velocity.x;
    next.contact.velocity_y = next.object.velocity.y;
    next.contact.movement_x = next.object.movement.x;
    next.contact.movement_y = next.object.movement.y;
    next.contact.ground_speed = next.object.ground_speed;
    next.contact.orientation = next.object.surface_angle;
    next.contact.fall_direction = next.object.fall_orientation;
    next.contact.move_flags = next.object.move_flags;
    if (resolve_ordinary_object_contact(next.contact, contact_environment, terrain,
            precision == ObjectMovementPrecision::Single ? TerrainCollisionPrecision::Single
                : TerrainCollisionPrecision::Double) != ObjectContactStatus::Applied) {
        return PlayerGroundMotionStatus::UnsupportedContact;
    }
    next.object.position.x = next.contact.position_x;
    next.object.position.y = next.contact.position_y;
    next.object.velocity.x = next.contact.velocity_x;
    next.object.velocity.y = next.contact.velocity_y;
    next.object.ground_speed = next.contact.ground_speed;
    next.object.surface_angle = next.contact.orientation;
    next.object.move_flags = next.contact.move_flags;
    next.walking.ground_speed = next.object.ground_speed;
    next.walking.horizontal_velocity = next.object.velocity.x;
    state = next;
    return PlayerGroundMotionStatus::Applied;
}

}

PlayerGroundMotionStatus advance_flat_player_motion(
    PlayerGroundMotionState& state,
    const PlayerGroundWalkParameters& parameters,
    PlayerWalkDirection direction,
    std::int32_t input_magnitude,
    const ObjectMovementEnvironment& movement_environment,
    const ObjectContactEnvironment& contact_environment,
    const TerrainCollision& terrain,
    ObjectMovementPrecision precision) {
    return advance_motion(state, parameters, direction, input_magnitude,
        movement_environment, contact_environment, terrain, precision, MotionKind::Walk);
}

PlayerGroundMotionStatus advance_flat_idle_player_motion(
    PlayerGroundMotionState& state,
    const ObjectMovementEnvironment& movement_environment,
    const ObjectContactEnvironment& contact_environment,
    const TerrainCollision& terrain,
    ObjectMovementPrecision precision) {
    return advance_motion(state, {}, PlayerWalkDirection::None, 0,
        movement_environment, contact_environment, terrain, precision, MotionKind::Idle);
}

PlayerGroundMotionStatus advance_flat_air_player_motion(
    PlayerGroundMotionState& state,
    const PlayerAirParameters& parameters,
    PlayerWalkDirection direction,
    const ObjectMovementEnvironment& movement_environment,
    const ObjectContactEnvironment& contact_environment,
    const TerrainCollision& terrain,
    ObjectMovementPrecision precision) {
    return advance_motion(state, {}, direction, 0, movement_environment,
        contact_environment, terrain, precision, MotionKind::Air, parameters);
}
