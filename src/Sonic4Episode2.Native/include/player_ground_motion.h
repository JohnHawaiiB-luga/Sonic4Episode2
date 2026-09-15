#pragma once

#include "object_contact.h"
#include "object_movement.h"
#include "player_air.h"
#include "player_ground_walk.h"

enum class PlayerGroundMotionStatus {
    Applied,
    UnsupportedAirborne,
    UnsupportedSurfaceAngle,
    UnsupportedObjectMovement,
    UnsupportedContact,
    UnsupportedGrounded,
    UnsupportedSequence,
};

struct PlayerGroundMotionState {
    ObjectMovementState object;
    PlayerGroundWalkState walking;
    ObjectContactState contact;
};

PlayerGroundMotionStatus advance_flat_player_motion(
    PlayerGroundMotionState& state,
    const PlayerGroundWalkParameters& parameters,
    PlayerWalkDirection direction,
    std::int32_t input_magnitude,
    const ObjectMovementEnvironment& movement_environment,
    const ObjectContactEnvironment& contact_environment,
    const TerrainCollision& terrain,
    ObjectMovementPrecision precision);

PlayerGroundMotionStatus advance_flat_idle_player_motion(
    PlayerGroundMotionState& state,
    const ObjectMovementEnvironment& movement_environment,
    const ObjectContactEnvironment& contact_environment,
    const TerrainCollision& terrain,
    ObjectMovementPrecision precision);

PlayerGroundMotionStatus advance_flat_air_player_motion(
    PlayerGroundMotionState& state,
    const PlayerAirParameters& parameters,
    PlayerWalkDirection direction,
    const ObjectMovementEnvironment& movement_environment,
    const ObjectContactEnvironment& contact_environment,
    const TerrainCollision& terrain,
    ObjectMovementPrecision precision);
