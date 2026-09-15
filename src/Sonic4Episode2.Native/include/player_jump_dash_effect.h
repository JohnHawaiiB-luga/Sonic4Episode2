#pragma once

#include "ame_runtime.h"

#include <cstdint>

struct PlayerJumpDashEffect {
    AmeRuntimeTransform transform;
    std::uint16_t display_rotation_x;
    std::int32_t pause_level{2};
};

PlayerJumpDashEffect make_player_jump_dash_effect(
    const AmeEffectVector4& position,
    const AmeEffectVector4& velocity,
    CameraPrecision precision);

bool player_jump_dash_effect_paused(bool globally_paused, std::int32_t pause_level) noexcept;
