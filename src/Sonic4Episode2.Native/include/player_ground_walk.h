#pragma once

#include "object_speed.h"

#include <cstdint>

enum class PlayerWalkDirection {
    None,
    Left,
    Right,
};

struct PlayerGroundWalkParameters {
    float acceleration;
    float maximum_speed;
    float deceleration;
    float attenuation_speed;
    float slope_cap_addition = 0.0f;
    float slope_acceleration = 0.0f;
    float slope_speed_cap = 0.0f;
    std::uint16_t slope_acceleration_start_angle = 0u;
};

struct PlayerGroundWalkState {
    float ground_speed = 0.0f;
    float horizontal_velocity = 0.0f;
    float previous_maximum = 0.0f;
    float working_maximum = 0.0f;
    float deceleration_delay = 0.0f;
    std::uint16_t spin_charge = 0u;
    bool inhibit_release_deceleration = false;
    std::uint16_t surface_angle = 0u;
    bool facing_left = false;
};

void advance_flat_player_walk(
    PlayerGroundWalkState& state,
    const PlayerGroundWalkParameters& parameters,
    PlayerWalkDirection direction,
    std::int32_t input_magnitude,
    float time_scale,
    ObjectSpeedPrecision precision);
