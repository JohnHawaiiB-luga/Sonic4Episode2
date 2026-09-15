#pragma once

#include "nn_motion_frame.h"

#include <cstdint>
#include <string_view>

enum class SonicGroundAnimationStatus {
    Applied,
    UnsupportedCharacter,
    UnsupportedVariant,
    UnsupportedSequence17,
    UnsupportedAirborne,
    UnsupportedDamage,
    UnsupportedTeam,
    UnsupportedIdleChain,
};

enum class SonicGroundAnimationSequence : std::uint8_t {
    Grounded,
    Sequence17,
    Airborne,
    Damage,
    Team,
};

enum class SonicGroundIdleBehavior : std::uint8_t {
    Loop,
    CharacterChain,
};

enum class SonicGroundAction : std::uint8_t {
    Idle = 0u,
    IdleWait0_01 = 2u,
    IdleWait0_02 = 3u,
    IdleWait1_01 = 4u,
    IdleWait1_02 = 5u,
    IdleWait2_01 = 6u,
    IdleWait2_02 = 7u,
    CrouchStart = 14u,
    Crouch = 15u,
    CrouchEnd = 16u,
    Walk = 19u,
    Run = 20u,
    Dash1 = 21u,
    Dash2 = 22u,
    Rolling = 27u,
    ChargeStart = 28u,
    Charge = 29u,
    ChargeHold = 30u,
    Jump = 39u,
    Fall = 40u,
    FallTurn = 41u,
    FallRotated = 42u,
    FallRotatedTurn = 43u,
};

enum class SonicGroundMotion : std::uint8_t {
    IdleRight,
    IdleLeft,
    IdleWait0_01,
    IdleWait0_02,
    IdleWait1_01,
    IdleWait1_02,
    IdleWait2_01,
    IdleWait2_02,
    Walk,
    Run,
    Dash1,
    Dash2Right,
    Dash2Left,
    Spin,
    FallRight,
    FallLeft,
    FallRotatedRight,
    FallRotatedLeft,
    CrouchRight,
    CrouchLeft,
    CrouchEndRight,
    CrouchEndLeft,
    Rolling,
    Charge,
    FallTurnRight,
    FallTurnLeft,
    FallRotatedTurnRight,
    FallRotatedTurnLeft,
};

struct SonicGroundMotionLayer {
    SonicGroundMotion motion = SonicGroundMotion::IdleRight;
    float start_frame = 0.0f;
    float end_frame = 60.0f;
    float relative_frame = 0.0f;
};

struct SonicGroundAnimationState {
    bool initialized = false;
    SonicGroundAction action = SonicGroundAction::Idle;
    SonicGroundAction previous_action = SonicGroundAction::Idle;
    SonicGroundMotionLayer primary;
    SonicGroundMotionLayer secondary;
    float primary_speed = 1.0f;
    float secondary_speed = 1.0f;
    bool loop = true;
    bool blend_active = false;
    float blend_weight = 0.0f;
    float blend_weight_decrement = 0.0f;
    float dash2_timer = 0.0f;
    bool facing_left = false;
    SonicGroundIdleBehavior idle_behavior = SonicGroundIdleBehavior::Loop;
    std::uint32_t idle_wrap_count = 0u;
    bool idle_chain_terminal = false;
};

struct SonicGroundAnimationSelectionInput {
    std::uint8_t character_id = 0u;
    std::uint8_t variant_id = 0u;
    SonicGroundAnimationSequence sequence = SonicGroundAnimationSequence::Grounded;
    float ground_speed = 0.0f;
    std::int16_t ground_angle = 0;
    bool facing_left = false;
    bool dash2_slope_lock = false;
    bool super_idle_route = false;
    bool primary_ended = false;
};

struct SonicGroundFrameAdvanceResult {
    bool primary_ended;
};

SonicGroundAnimationStatus start_sonic_ground_idle(
    SonicGroundAnimationState& state,
    const SonicGroundAnimationSelectionInput& input,
    SonicGroundIdleBehavior behavior);

SonicGroundAnimationStatus update_sonic_ground_idle(
    SonicGroundAnimationState& state,
    const SonicGroundAnimationSelectionInput& input);

SonicGroundAnimationStatus start_sonic_ground_walk(
    SonicGroundAnimationState& state,
    const SonicGroundAnimationSelectionInput& input);

SonicGroundAnimationStatus update_sonic_ground_walk(
    SonicGroundAnimationState& state,
    const SonicGroundAnimationSelectionInput& input);

void advance_sonic_ground_dash2_timer(
    SonicGroundAnimationState& state,
    float time_scale,
    CameraPrecision precision);

void set_sonic_ground_playback_speed(
    SonicGroundAnimationState& state,
    float ground_speed,
    CameraPrecision precision);

void set_sonic_ground_facing(SonicGroundAnimationState& state, bool facing_left);

void start_sonic_jump_animation(SonicGroundAnimationState& state, bool facing_left);

void start_sonic_crouch_animation(
    SonicGroundAnimationState& state,
    SonicGroundAction action,
    bool facing_left);

void start_sonic_rolling_animation(SonicGroundAnimationState& state, bool facing_left);

void start_sonic_spindash_animation(
    SonicGroundAnimationState& state,
    SonicGroundAction action,
    bool facing_left);

void start_sonic_fall_animation(
    SonicGroundAnimationState& state,
    bool facing_left,
    bool rotated);

void start_sonic_fall_turn_animation(
    SonicGroundAnimationState& state,
    bool facing_left,
    bool rotated,
    std::uint32_t frame);

void set_sonic_jump_playback_speed(
    SonicGroundAnimationState& state,
    float velocity_x,
    float velocity_y,
    CameraPrecision precision);

void advance_sonic_ground_blend(
    SonicGroundAnimationState& state,
    float blend_weight_decrement,
    CameraPrecision precision);

void advance_sonic_ground_blend(
    SonicGroundAnimationState& state,
    CameraPrecision precision);

SonicGroundFrameAdvanceResult advance_sonic_ground_frames(
    SonicGroundAnimationState& state,
    float time_scale,
    CameraPrecision precision);

std::string_view sonic_ground_motion_name(SonicGroundMotion motion);

float sonic_ground_motion_sample_frame(
    const SonicGroundMotionLayer& layer,
    CameraPrecision precision);
