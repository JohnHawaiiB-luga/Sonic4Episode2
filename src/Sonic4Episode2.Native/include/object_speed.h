#pragma once

enum class ObjectSpeedPrecision {
    Single,
    Double,
};

float object_speed_up(
    float speed,
    float acceleration,
    float maximum,
    float time_scale,
    ObjectSpeedPrecision precision);

float object_speed_down(
    float speed,
    float deceleration,
    float time_scale,
    ObjectSpeedPrecision precision);
