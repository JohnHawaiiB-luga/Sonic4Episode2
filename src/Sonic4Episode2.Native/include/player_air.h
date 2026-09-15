#pragma once

#include "player_ground_walk.h"

#include <cstdint>

struct PlayerAirParameters {
    float acceleration;
    float maximum_speed;
    float deceleration;
    float attenuation_threshold;
};

struct PlayerAirState {
    float vx = 0.0f;
    float ground_speed = 0.0f;
    float working_maximum = 0.0f;
    float deceleration_delay = 0.0f;
    std::uint16_t surface_angle = 0u;
    std::uint16_t speed_pool = 0u;
    bool half_acceleration = false;
};

void advance_ordinary_player_air(
    PlayerAirState& state,
    const PlayerAirParameters& parameters,
    PlayerWalkDirection direction,
    float time_scale,
    ObjectSpeedPrecision precision);
