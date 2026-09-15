#pragma once

#include "object_movement.h"

#include <cstdint>

struct PlayerJumpSequenceState {
    std::uint32_t player_flags = 0u;
    float working_maximum = 0.0f;
    float fall_timer = 24.0f;
};

struct PlayerFallTurnState {
    std::uint32_t frame = 0u;
    std::uint32_t saved_action = 40u;
    std::uint16_t angle = 0u;
    bool from_left = false;
};

struct PlayerJumpDashState {
    std::uint32_t timer = 0u;
};

enum class PlayerJumpDashTransition {
    None,
    Fall,
    Landing
};

bool can_start_ordinary_player_jump_dash(
    const PlayerJumpSequenceState& sequence,
    bool jump_pushed,
    float homing_timer,
    bool target_present,
    bool ending);

void initialize_ordinary_seq21_jump_dash(
    ObjectMovementState& object,
    PlayerJumpSequenceState& sequence,
    PlayerJumpDashState& dash,
    float& deceleration_delay,
    bool facing_left,
    ObjectMovementPrecision precision);

PlayerJumpDashTransition advance_ordinary_player_jump_dash(
    ObjectMovementState& object,
    PlayerJumpSequenceState& sequence,
    PlayerJumpDashState& dash,
    ObjectMovementPrecision precision);

std::uint32_t begin_ordinary_player_fall_turn(
    PlayerFallTurnState& turn,
    PlayerJumpSequenceState& sequence,
    std::uint32_t action,
    bool& facing_left);

bool advance_ordinary_player_fall_turn(
    PlayerFallTurnState& turn,
    PlayerJumpSequenceState& sequence);

bool check_ordinary_player_fall(
    const ObjectMovementState& object,
    PlayerJumpSequenceState& sequence,
    float time_scale,
    ObjectMovementPrecision precision);

void initialize_ordinary_seq16_fall(
    ObjectMovementState& object,
    PlayerJumpSequenceState& sequence,
    ObjectMovementPrecision precision);

void initialize_ordinary_seq16_fall(
    ObjectMovementState& object,
    PlayerJumpSequenceState& sequence,
    ObjectMovementPrecision precision,
    std::uint16_t stage_id);

void update_ordinary_player_jump_release(
    ObjectMovementState& object,
    PlayerJumpSequenceState& sequence,
    bool jump_held,
    ObjectMovementPrecision precision);

void apply_ordinary_player_landing(
    ObjectMovementState& object,
    PlayerJumpSequenceState& sequence,
    ObjectMovementPrecision precision);

void initialize_flat_seq17_jump(
    ObjectMovementState& object,
    PlayerJumpSequenceState& sequence,
    ObjectMovementPrecision precision);

void initialize_ordinary_seq17_jump(
    ObjectMovementState& object,
    PlayerJumpSequenceState& sequence,
    ObjectMovementPrecision precision);

void update_flat_player_jump_release(
    ObjectMovementState& object,
    PlayerJumpSequenceState& sequence,
    bool jump_held,
    ObjectMovementPrecision precision);

void apply_flat_player_landing(
    ObjectMovementState& object,
    PlayerJumpSequenceState& sequence,
    ObjectMovementPrecision precision);
