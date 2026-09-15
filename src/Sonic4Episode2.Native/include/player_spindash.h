#pragma once

#include "player_ground_walk.h"

float initialize_ordinary_spindash_power(
    float power,
    bool continuing_charge,
    float time_scale,
    ObjectSpeedPrecision precision);

float decay_ordinary_spindash_power(
    float power,
    float time_scale,
    ObjectSpeedPrecision precision);

float ordinary_spindash_release_speed(
    float power,
    float ground_speed,
    bool facing_left);

float advance_ordinary_rolling_speed(
    float ground_speed,
    float deceleration_delay,
    PlayerWalkDirection direction,
    float time_scale,
    ObjectSpeedPrecision precision);
