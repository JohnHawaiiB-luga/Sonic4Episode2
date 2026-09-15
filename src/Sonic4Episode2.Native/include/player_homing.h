#pragma once

#include "player_jump.h"

#include <cstdint>
#include <optional>
#include <vector>

struct PlayerHomingTarget {
    ObjectMovementVector position{};
    std::int16_t left = 0;
    std::int16_t top = 0;
    std::int16_t right = 0;
    std::int16_t bottom = 0;
    std::uint32_t display_flags = 0u;
};

struct PlayerHomingState {
    float timer = 0.0f;
    float homing_timer = 0.0f;
    float boost_timer = 0.0f;
    std::uint32_t gimmick_flags = 0u;
    std::uint32_t secondary_player_flags = 0u;
    std::uint32_t display_flags = 0u;
    std::uint32_t user_work = 0u;
    std::uint32_t player_timer = 0u;
};

struct PlayerHomingCandidate {
    std::uint32_t id = 0u;
    std::uint16_t type = 0u;
    PlayerHomingTarget target{};
    std::uint32_t object_flags = 0u;
    std::uint32_t entity_flags = 0u;
    std::uint32_t secondary_player_flags = 0u;
    bool registered_player = false;
    float player_timer = 0.0f;
    std::uint16_t event_id = 0u;
    std::uint8_t event_parameter = 0u;
};

struct PlayerHomingSearchInput {
    PlayerHomingTarget player{};
    std::uint32_t object_flags = 0u;
    std::uint32_t player_flags = 0u;
    std::uint32_t sequence_flags = 0u;
    std::uint32_t sequence_attributes = 0u;
    bool require_same_plane = false;
    std::uint16_t fall_orientation = 0u;
    float boost_timer = 0.0f;
    float range_extension = 0.0f;
    float time_scale = 1.0f;
    std::optional<std::uint32_t> cursor_target;
};

struct PlayerHomingSearchResult {
    std::optional<std::uint32_t> target;
    std::optional<std::uint32_t> cursor_target;
    float boost_timer = 0.0f;
};

PlayerHomingSearchResult select_ordinary_player_homing_target(
    const PlayerHomingSearchInput& input,
    const std::vector<PlayerHomingCandidate>& candidates,
    ObjectMovementPrecision precision);

enum class PlayerHomingTransition {
    None,
    Fall,
    Landing,
    JumpDash,
};

struct PlayerHomingInitialization {
    PlayerHomingTransition transition = PlayerHomingTransition::None;
    std::optional<std::uint32_t> action;
};

PlayerHomingInitialization initialize_ordinary_seq19_homing(
    ObjectMovementState& object,
    PlayerJumpSequenceState& sequence,
    PlayerHomingState& homing,
    bool target_present,
    ObjectMovementPrecision precision);

PlayerHomingTransition advance_ordinary_player_homing(
    ObjectMovementState& object,
    PlayerJumpSequenceState& sequence,
    PlayerHomingState& homing,
    const std::optional<PlayerHomingTarget>& target,
    float time_scale,
    ObjectMovementPrecision precision);

std::optional<std::uint32_t> initialize_ordinary_seq20_homing_rebound(
    ObjectMovementState& object,
    PlayerJumpSequenceState& sequence,
    PlayerHomingState& homing,
    ObjectMovementPrecision precision);

PlayerHomingTransition advance_ordinary_player_homing_rebound(
    ObjectMovementState& object,
    PlayerJumpSequenceState& sequence,
    PlayerHomingState& homing,
    ObjectMovementPrecision precision);
