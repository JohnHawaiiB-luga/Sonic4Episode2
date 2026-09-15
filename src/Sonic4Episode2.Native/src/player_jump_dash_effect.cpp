#include "player_jump_dash_effect.h"

#include "nn_quaternion.h"

#include <cmath>
#include <stdexcept>

PlayerJumpDashEffect make_player_jump_dash_effect(
    const AmeEffectVector4& position,
    const AmeEffectVector4& velocity,
    CameraPrecision precision) {
    if (precision != CameraPrecision::Single && precision != CameraPrecision::Double) {
        throw std::invalid_argument("jump-dash effect precision is unsupported");
    }
    for (float value : {position.x, position.y, position.z, velocity.x, velocity.y}) {
        if (!std::isfinite(value) || std::fabs(value) > 16777216.0f) {
            throw std::invalid_argument("jump-dash effect input is invalid");
        }
    }
    const double radians = std::atan2(-static_cast<double>(velocity.y), static_cast<double>(velocity.x));
    const double angle = radians * 10430.3779296875;
    const auto integral_angle = static_cast<std::int32_t>(
        precision == CameraPrecision::Single ? static_cast<double>(static_cast<float>(angle)) : angle);
    const auto display_x = static_cast<std::uint16_t>(0xc001u - static_cast<std::uint32_t>(integral_angle));
    const NnQuaternion identity{{0.0f, 0.0f, 0.0f, 1.0f}};
    const auto facing = nn_quaternion_xyz({{0, 0x3fff, 0}}, precision);
    const auto display = nn_quaternion_xyz({{display_x, 0, 0}}, precision);
    auto rotation = nn_quaternion_multiply(facing, identity, precision);
    rotation = nn_quaternion_multiply(identity, rotation, precision);
    rotation = nn_quaternion_multiply(rotation, display, precision);
    return {{{position.x, -position.y, position.z, 1.0f},
        {rotation[0u], rotation[1u], rotation[2u], rotation[3u]}}, display_x, 2};
}

bool player_jump_dash_effect_paused(bool globally_paused, std::int32_t pause_level) noexcept {
    return globally_paused && 2 <= pause_level;
}
