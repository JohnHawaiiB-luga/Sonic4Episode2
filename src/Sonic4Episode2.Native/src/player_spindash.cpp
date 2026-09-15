#include "player_spindash.h"

#include <cmath>
#include <stdexcept>

namespace {

void require_power(float power) {
    if (!std::isfinite(power) || power < 0.0f) {
        throw std::invalid_argument("spindash power is invalid");
    }
}

void require_environment(float time_scale, ObjectSpeedPrecision precision) {
    if (!std::isfinite(time_scale) || time_scale < 0.0f) {
        throw std::invalid_argument("spindash time scale is invalid");
    }
    if (precision != ObjectSpeedPrecision::Single && precision != ObjectSpeedPrecision::Double) {
        throw std::invalid_argument("spindash precision is unsupported");
    }
}

}

float initialize_ordinary_spindash_power(
    float power,
    bool continuing_charge,
    float time_scale,
    ObjectSpeedPrecision precision) {
    require_power(power);
    require_environment(time_scale, precision);
    return continuing_charge ? object_speed_up(power, 2.0f, 10.0f, time_scale, precision) : 3.0f;
}

float decay_ordinary_spindash_power(
    float power,
    float time_scale,
    ObjectSpeedPrecision precision) {
    require_power(power);
    require_environment(time_scale, precision);
    const float deceleration = power * 0.03125f;
    return object_speed_down(power, deceleration, time_scale, precision);
}

float ordinary_spindash_release_speed(
    float power,
    float ground_speed,
    bool facing_left) {
    require_power(power);
    if (!std::isfinite(ground_speed)) {
        throw std::invalid_argument("spindash ground speed is nonfinite");
    }
    const float speed = static_cast<float>(8.0 + static_cast<double>(power) * 0.5);
    const float directed_speed = facing_left ? -speed : speed;
    return std::fabs(speed) > std::fabs(ground_speed) ? directed_speed : ground_speed;
}

float advance_ordinary_rolling_speed(
    float ground_speed,
    float deceleration_delay,
    PlayerWalkDirection direction,
    float time_scale,
    ObjectSpeedPrecision precision) {
    require_environment(time_scale, precision);
    if (!std::isfinite(deceleration_delay) || deceleration_delay < 0.0f) {
        throw std::invalid_argument("rolling deceleration delay is invalid");
    }
    if (direction != PlayerWalkDirection::None && direction != PlayerWalkDirection::Left
            && direction != PlayerWalkDirection::Right) {
        throw std::invalid_argument("rolling direction is unsupported");
    }
    const bool following = (ground_speed > 0.0f && direction == PlayerWalkDirection::Right)
        || (ground_speed < 0.0f && direction == PlayerWalkDirection::Left);
    const float deceleration = deceleration_delay > 0.0f ? 0.0f
        : (following ? 0.015625f : 0.0625f);
    return object_speed_down(ground_speed, deceleration, time_scale, precision);
}
