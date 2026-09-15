#include "sonic_ground_animation.h"

#include <cmath>
#include <stdexcept>

namespace {

constexpr float kWalkThreshold = 1.35f;
constexpr float kRunThreshold = 2.7f;
constexpr float kDash1Threshold = 3.6f;
constexpr float kInitialDash2Threshold = 4.05f;
constexpr float kDash2Threshold = 4.5f;
constexpr float kDash2Timer = 30.0f;
constexpr float kMinimumPlaybackSpeed = 1.0f;
constexpr float kMaximumPlaybackSpeed = 8.0f;
constexpr std::uint32_t kIdleWrapLimit = 8u;
constexpr std::uint32_t kIdleWait0WrapLimit = 10u;
constexpr std::uint32_t kIdleWait1WrapLimit = 3u;

[[noreturn]] void fail(const char* message) {
    throw std::invalid_argument(message);
}

void require_precision(CameraPrecision precision) {
    if (precision != CameraPrecision::Single && precision != CameraPrecision::Double) {
        fail("ground animation precision is unsupported");
    }
}

void require_finite(float value, const char* message) {
    if (!std::isfinite(value)) {
        fail(message);
    }
}

void require_nonnegative_finite(float value, const char* message) {
    require_finite(value, message);
    if (value < 0.0f) {
        fail(message);
    }
}

struct Arithmetic {
    CameraPrecision precision;

    double rounded(double value) const {
        if (precision == CameraPrecision::Double || !std::isfinite(value)) {
            return value;
        }
        int exponent = 0;
        const float mantissa = static_cast<float>(std::frexp(value, &exponent));
        return std::ldexp(static_cast<double>(mantissa), exponent);
    }

    double add(double left, double right) const {
        return rounded(left + right);
    }

    double subtract(double left, double right) const {
        return rounded(left - right);
    }

    double multiply(double left, double right) const {
        return rounded(left * right);
    }

    float spill(double value) const {
        return static_cast<float>(value);
    }
};

SonicGroundAnimationStatus validate_supported(
    const SonicGroundAnimationSelectionInput& input) {
    if (input.character_id != std::uint8_t{0}) {
        return SonicGroundAnimationStatus::UnsupportedCharacter;
    }
    if (input.variant_id != std::uint8_t{0}) {
        return SonicGroundAnimationStatus::UnsupportedVariant;
    }
    switch (input.sequence) {
    case SonicGroundAnimationSequence::Grounded:
        return SonicGroundAnimationStatus::Applied;
    case SonicGroundAnimationSequence::Sequence17:
        return SonicGroundAnimationStatus::UnsupportedSequence17;
    case SonicGroundAnimationSequence::Airborne:
        return SonicGroundAnimationStatus::UnsupportedAirborne;
    case SonicGroundAnimationSequence::Damage:
        return SonicGroundAnimationStatus::UnsupportedDamage;
    case SonicGroundAnimationSequence::Team:
        return SonicGroundAnimationStatus::UnsupportedTeam;
    }
    fail("ground animation sequence is unsupported");
}

bool is_walk_action(SonicGroundAction action) {
    return action == SonicGroundAction::Walk ||
        action == SonicGroundAction::Run ||
        action == SonicGroundAction::Dash1 ||
        action == SonicGroundAction::Dash2;
}

bool is_idle_action(SonicGroundAction action) {
    return action == SonicGroundAction::Idle ||
        action == SonicGroundAction::IdleWait0_01 ||
        action == SonicGroundAction::IdleWait0_02 ||
        action == SonicGroundAction::IdleWait1_01 ||
        action == SonicGroundAction::IdleWait1_02 ||
        action == SonicGroundAction::IdleWait2_01 ||
        action == SonicGroundAction::IdleWait2_02;
}

bool is_looping_idle_action(SonicGroundAction action) {
    return action == SonicGroundAction::Idle ||
        action == SonicGroundAction::IdleWait0_02 ||
        action == SonicGroundAction::IdleWait1_02 ||
        action == SonicGroundAction::IdleWait2_02;
}

SonicGroundMotionLayer make_motion_layer(SonicGroundAction action, bool facing_left) {
    switch (action) {
    case SonicGroundAction::Idle:
        return {
            facing_left ? SonicGroundMotion::IdleLeft : SonicGroundMotion::IdleRight,
            0.0f,
            60.0f,
            0.0f,
        };
    case SonicGroundAction::IdleWait0_01:
        return {SonicGroundMotion::IdleWait0_01, 0.0f, 30.0f, 0.0f};
    case SonicGroundAction::IdleWait0_02:
        return {SonicGroundMotion::IdleWait0_02, 30.0f, 150.0f, 0.0f};
    case SonicGroundAction::IdleWait1_01:
        return {SonicGroundMotion::IdleWait1_01, 0.0f, 60.0f, 0.0f};
    case SonicGroundAction::IdleWait1_02:
        return {SonicGroundMotion::IdleWait1_02, 60.0f, 440.0f, 0.0f};
    case SonicGroundAction::IdleWait2_01:
        return {SonicGroundMotion::IdleWait2_01, 130.0f, 200.0f, 0.0f};
    case SonicGroundAction::IdleWait2_02:
        return {SonicGroundMotion::IdleWait2_02, 200.0f, 360.0f, 0.0f};
    case SonicGroundAction::Walk:
        return {SonicGroundMotion::Walk, 0.0f, 60.0f, 0.0f};
    case SonicGroundAction::CrouchStart:
    case SonicGroundAction::Crouch:
        return {facing_left ? SonicGroundMotion::CrouchLeft : SonicGroundMotion::CrouchRight,
            15.0f, 35.0f, 0.0f};
    case SonicGroundAction::CrouchEnd:
        return {facing_left ? SonicGroundMotion::CrouchEndLeft : SonicGroundMotion::CrouchEndRight,
            35.0f, 60.0f, 0.0f};
    case SonicGroundAction::Rolling:
        return {SonicGroundMotion::Rolling, 0.0f, 20.0f, 0.0f};
    case SonicGroundAction::ChargeStart:
    case SonicGroundAction::Charge:
    case SonicGroundAction::ChargeHold:
        return {SonicGroundMotion::Charge, 0.0f, 60.0f, 0.0f};
    case SonicGroundAction::Jump:
        return {SonicGroundMotion::Spin, 0.0f, 20.0f, 0.0f};
    case SonicGroundAction::Fall:
        return {facing_left ? SonicGroundMotion::FallLeft : SonicGroundMotion::FallRight, 50.0f, 70.0f, 0.0f};
    case SonicGroundAction::FallTurn:
        return {facing_left ? SonicGroundMotion::FallTurnLeft : SonicGroundMotion::FallTurnRight,
            facing_left ? 50.0f : 60.0f, facing_left ? 60.0f : 70.0f, 0.0f};
    case SonicGroundAction::FallRotatedTurn:
        return {facing_left ? SonicGroundMotion::FallRotatedTurnLeft : SonicGroundMotion::FallRotatedTurnRight,
            facing_left ? 50.0f : 60.0f, facing_left ? 60.0f : 70.0f, 0.0f};
    case SonicGroundAction::FallRotated:
        return {facing_left ? SonicGroundMotion::FallRotatedLeft : SonicGroundMotion::FallRotatedRight,
            50.0f, 70.0f, 0.0f};
    case SonicGroundAction::Run:
        return {SonicGroundMotion::Run, 0.0f, 60.0f, 0.0f};
    case SonicGroundAction::Dash1:
        return {SonicGroundMotion::Dash1, 0.0f, 60.0f, 0.0f};
    case SonicGroundAction::Dash2:
        return {
            facing_left ? SonicGroundMotion::Dash2Left : SonicGroundMotion::Dash2Right,
            0.0f,
            20.0f,
            0.0f,
        };
    }
    fail("ground animation action is unsupported");
}

float blend_weight_decrement_for(SonicGroundAction previous_action) {
    return previous_action == SonicGroundAction::Walk ||
            previous_action == SonicGroundAction::Run ||
            previous_action == SonicGroundAction::Dash1
        ? 0.125f
        : 0.25f;
}

bool is_charge_action(SonicGroundAction action) {
    return action == SonicGroundAction::ChargeStart || action == SonicGroundAction::Charge
        || action == SonicGroundAction::ChargeHold;
}

void apply_action(
    SonicGroundAnimationState& state,
    SonicGroundAction action,
    bool facing_left,
    bool preserve_relative_frames) {
    const SonicGroundMotionLayer next = make_motion_layer(action, facing_left);
    if (!state.initialized) {
        state.initialized = true;
        state.action = action;
        state.previous_action = action;
        state.primary = next;
        state.secondary = next;
        state.primary_speed = 1.0f;
        state.secondary_speed = 1.0f;
        state.loop = true;
        state.blend_active = false;
        state.blend_weight = 0.0f;
        state.blend_weight_decrement = 0.0f;
        state.facing_left = facing_left;
        state.idle_wrap_count = 0u;
        state.idle_chain_terminal = false;
        return;
    }

    if (state.action == action) {
        if (state.primary.motion != next.motion) {
            const SonicGroundMotionLayer previous = state.primary;
            state.previous_action = state.action;
            state.secondary = previous;
            state.primary = next;
            state.primary.relative_frame = previous.relative_frame;
            state.blend_active = false;
            state.blend_weight = 0.0f;
            state.blend_weight_decrement = blend_weight_decrement_for(state.previous_action);
        }
        state.facing_left = facing_left;
        return;
    }

    const SonicGroundMotionLayer previous = state.primary;
    state.previous_action = state.action;
    state.action = action;
    state.secondary = previous;
    state.primary = next;
    if (preserve_relative_frames) {
        state.primary.relative_frame = previous.relative_frame;
        state.secondary.relative_frame = previous.relative_frame;
    }
    const bool blend = !is_charge_action(action) && !is_charge_action(state.previous_action)
        && action != SonicGroundAction::FallTurn && action != SonicGroundAction::FallRotatedTurn
        && state.previous_action != SonicGroundAction::FallTurn
        && state.previous_action != SonicGroundAction::FallRotatedTurn;
    state.blend_active = blend;
    state.blend_weight = blend ? 1.0f : 0.0f;
    if (!blend) state.secondary = next;
    state.blend_weight_decrement = blend_weight_decrement_for(state.previous_action);
    state.facing_left = facing_left;
    if (!is_idle_action(action)) {
        state.idle_wrap_count = 0u;
        state.idle_chain_terminal = false;
    }
}

bool initial_dash2_eligible(const SonicGroundAnimationSelectionInput& input) {
    if (!input.facing_left) {
        return input.ground_angle > 0;
    }
    return input.ground_angle < 0 && !input.dash2_slope_lock;
}

bool ongoing_dash2_eligible(const SonicGroundAnimationSelectionInput& input) {
    if (input.dash2_slope_lock) {
        return false;
    }
    return !input.facing_left ? input.ground_angle >= 0 : input.ground_angle <= 0;
}

SonicGroundAction initial_walk_action(
    const SonicGroundAnimationState& state,
    const SonicGroundAnimationSelectionInput& input) {
    const float speed = std::fabs(input.ground_speed);
    if (speed < kWalkThreshold) {
        return SonicGroundAction::Walk;
    }
    if (speed < kRunThreshold) {
        return SonicGroundAction::Run;
    }
    if (speed < kDash1Threshold) {
        return SonicGroundAction::Dash1;
    }
    if (speed < kInitialDash2Threshold &&
        (initial_dash2_eligible(input) || state.dash2_timer != 0.0f)) {
        return SonicGroundAction::Dash2;
    }
    return SonicGroundAction::Dash1;
}

void require_initialized(const SonicGroundAnimationState& state) {
    if (!state.initialized) {
        fail("ground animation state is uninitialized");
    }
}

void require_layer(const SonicGroundMotionLayer& layer, const char* message) {
    require_finite(layer.start_frame, message);
    require_finite(layer.end_frame, message);
    require_finite(layer.relative_frame, message);
    if (layer.end_frame <= layer.start_frame) {
        fail(message);
    }
}

float advance_frame(float frame, float speed, float time_scale, const Arithmetic& arithmetic) {
    const float increment = arithmetic.spill(arithmetic.multiply(
        static_cast<double>(speed),
        static_cast<double>(time_scale)));
    return arithmetic.spill(arithmetic.add(static_cast<double>(frame), increment));
}

float relative_duration(const SonicGroundMotionLayer& layer, const Arithmetic& arithmetic) {
    return arithmetic.spill(arithmetic.subtract(
        static_cast<double>(layer.end_frame),
        static_cast<double>(layer.start_frame)));
}

float loop_frame(float frame, float duration, const Arithmetic& arithmetic) {
    while (frame >= duration) {
        const float next = arithmetic.spill(arithmetic.subtract(frame, duration));
        if (next == frame) fail("ground animation frame cannot advance through its loop");
        frame = next;
    }
    return frame;
}

float clamp_frame(float frame, float duration) {
    return frame >= duration - 1.0f ? duration - 1.0f : frame;
}

}

SonicGroundAnimationStatus start_sonic_ground_idle(
    SonicGroundAnimationState& state,
    const SonicGroundAnimationSelectionInput& input,
    SonicGroundIdleBehavior behavior) {
    const SonicGroundAnimationStatus status = validate_supported(input);
    if (status != SonicGroundAnimationStatus::Applied) {
        return status;
    }
    if (behavior != SonicGroundIdleBehavior::Loop &&
        behavior != SonicGroundIdleBehavior::CharacterChain) {
        fail("ground animation idle behavior is unsupported");
    }

    apply_action(state, SonicGroundAction::Idle, input.facing_left, false);
    state.loop = true;
    state.primary_speed = 1.0f;
    state.secondary_speed = 1.0f;
    state.idle_behavior = behavior;
    state.idle_wrap_count = 0u;
    state.idle_chain_terminal = false;
    return SonicGroundAnimationStatus::Applied;
}

SonicGroundAnimationStatus update_sonic_ground_idle(
    SonicGroundAnimationState& state,
    const SonicGroundAnimationSelectionInput& input) {
    const SonicGroundAnimationStatus status = validate_supported(input);
    if (status != SonicGroundAnimationStatus::Applied) {
        return status;
    }
    require_initialized(state);
    if (!is_idle_action(state.action)) {
        fail("ground animation idle action is unsupported");
    }
    if (state.idle_behavior != SonicGroundIdleBehavior::CharacterChain) {
        return SonicGroundAnimationStatus::Applied;
    }
    if (state.action == SonicGroundAction::IdleWait2_02) {
        state.loop = true;
        state.idle_chain_terminal = true;
        return SonicGroundAnimationStatus::Applied;
    }
    if (!input.primary_ended) {
        return SonicGroundAnimationStatus::Applied;
    }

    switch (state.action) {
    case SonicGroundAction::Idle:
        ++state.idle_wrap_count;
        if (state.idle_wrap_count >= kIdleWrapLimit) {
            apply_action(
                state,
                input.super_idle_route
                    ? SonicGroundAction::IdleWait1_01
                    : SonicGroundAction::IdleWait0_01,
                input.facing_left,
                false);
            state.idle_wrap_count = 0u;
        }
        break;
    case SonicGroundAction::IdleWait0_01:
        apply_action(state, SonicGroundAction::IdleWait0_02, input.facing_left, false);
        state.idle_wrap_count = 0u;
        break;
    case SonicGroundAction::IdleWait0_02:
        ++state.idle_wrap_count;
        if (state.idle_wrap_count >= kIdleWait0WrapLimit) {
            apply_action(state, SonicGroundAction::IdleWait1_01, input.facing_left, false);
            state.idle_wrap_count = 0u;
        }
        break;
    case SonicGroundAction::IdleWait1_01:
        apply_action(state, SonicGroundAction::IdleWait1_02, input.facing_left, false);
        state.idle_wrap_count = 0u;
        break;
    case SonicGroundAction::IdleWait1_02:
        ++state.idle_wrap_count;
        if (state.idle_wrap_count >= kIdleWait1WrapLimit && !input.super_idle_route) {
            apply_action(state, SonicGroundAction::IdleWait2_01, input.facing_left, false);
            state.idle_wrap_count = 0u;
        }
        break;
    case SonicGroundAction::IdleWait2_01:
        apply_action(state, SonicGroundAction::IdleWait2_02, input.facing_left, false);
        state.idle_wrap_count = 0u;
        state.idle_chain_terminal = true;
        break;
    case SonicGroundAction::IdleWait2_02:
        fail("ground animation idle terminal action did not return");
    case SonicGroundAction::Walk:
    case SonicGroundAction::Run:
    case SonicGroundAction::Dash1:
    case SonicGroundAction::Dash2:
        fail("ground animation idle action did not normalize");
    }

    state.loop = is_looping_idle_action(state.action);
    return SonicGroundAnimationStatus::Applied;
}

SonicGroundAnimationStatus start_sonic_ground_walk(
    SonicGroundAnimationState& state,
    const SonicGroundAnimationSelectionInput& input) {
    const SonicGroundAnimationStatus status = validate_supported(input);
    if (status != SonicGroundAnimationStatus::Applied) {
        return status;
    }
    require_finite(input.ground_speed, "ground animation speed is nonfinite");
    require_nonnegative_finite(state.dash2_timer, "ground animation dash2 timer is invalid");

    if (initial_dash2_eligible(input)) {
        state.dash2_timer = kDash2Timer;
    }
    apply_action(state, initial_walk_action(state, input), input.facing_left, false);
    state.loop = true;
    return SonicGroundAnimationStatus::Applied;
}

SonicGroundAnimationStatus update_sonic_ground_walk(
    SonicGroundAnimationState& state,
    const SonicGroundAnimationSelectionInput& input) {
    const SonicGroundAnimationStatus status = validate_supported(input);
    if (status != SonicGroundAnimationStatus::Applied) {
        return status;
    }
    require_finite(input.ground_speed, "ground animation speed is nonfinite");
    require_nonnegative_finite(state.dash2_timer, "ground animation dash2 timer is invalid");
    if (!state.initialized) {
        return start_sonic_ground_walk(state, input);
    }

    if (ongoing_dash2_eligible(input)) {
        state.dash2_timer = kDash2Timer;
    }
    if (!is_walk_action(state.action)) {
        apply_action(state, SonicGroundAction::Walk, input.facing_left, false);
    }

    const float speed = std::fabs(input.ground_speed);
    switch (state.action) {
    case SonicGroundAction::Walk:
        if (speed >= kRunThreshold) {
            apply_action(state, SonicGroundAction::Run, input.facing_left, true);
        }
        break;
    case SonicGroundAction::Run:
        if (speed >= kDash1Threshold) {
            apply_action(state, SonicGroundAction::Dash1, input.facing_left, true);
        } else if (speed < kRunThreshold) {
            apply_action(state, SonicGroundAction::Walk, input.facing_left, true);
        }
        break;
    case SonicGroundAction::Dash1:
        if (speed >= kDash2Threshold &&
            (ongoing_dash2_eligible(input) || state.dash2_timer != 0.0f) &&
            input.primary_ended) {
            apply_action(state, SonicGroundAction::Dash2, input.facing_left, false);
        } else if (speed < kDash1Threshold) {
            apply_action(state, SonicGroundAction::Run, input.facing_left, true);
        }
        break;
    case SonicGroundAction::Dash2:
        if ((speed < kDash2Threshold ||
             (!ongoing_dash2_eligible(input) && state.dash2_timer == 0.0f)) &&
            input.primary_ended) {
            apply_action(state, SonicGroundAction::Dash1, input.facing_left, false);
        }
        break;
    case SonicGroundAction::Idle:
    case SonicGroundAction::IdleWait0_01:
    case SonicGroundAction::IdleWait0_02:
    case SonicGroundAction::IdleWait1_01:
    case SonicGroundAction::IdleWait1_02:
    case SonicGroundAction::IdleWait2_01:
    case SonicGroundAction::IdleWait2_02:
        fail("ground walk state did not normalize");
    }

    state.loop = true;
    return SonicGroundAnimationStatus::Applied;
}

void advance_sonic_ground_dash2_timer(
    SonicGroundAnimationState& state,
    float time_scale,
    CameraPrecision precision) {
    require_precision(precision);
    require_initialized(state);
    require_nonnegative_finite(time_scale, "ground animation time scale is invalid");
    require_nonnegative_finite(state.dash2_timer, "ground animation dash2 timer is invalid");
    if (state.dash2_timer == 0.0f) {
        return;
    }

    const Arithmetic arithmetic{precision};
    const float remaining = arithmetic.spill(arithmetic.subtract(
        static_cast<double>(state.dash2_timer),
        static_cast<double>(time_scale)));
    state.dash2_timer = remaining > 0.0f ? remaining : 0.0f;
}

void set_sonic_ground_playback_speed(
    SonicGroundAnimationState& state,
    float ground_speed,
    CameraPrecision precision) {
    require_precision(precision);
    require_initialized(state);
    require_finite(ground_speed, "ground animation speed is nonfinite");
    if (is_idle_action(state.action) || state.action == SonicGroundAction::Dash2) {
        state.primary_speed = 1.0f;
        state.secondary_speed = 1.0f;
        return;
    }
    if (!is_walk_action(state.action) && state.action != SonicGroundAction::Rolling) {
        fail("ground animation action is unsupported");
    }

    const Arithmetic arithmetic{precision};
    const double eighth = arithmetic.multiply(static_cast<double>(ground_speed), 0.125);
    const double quarter = arithmetic.multiply(static_cast<double>(ground_speed), 0.25);
    const float combined = arithmetic.spill(arithmetic.add(eighth, quarter));
    const float absolute = std::fabs(combined);
    const float speed = absolute < kMinimumPlaybackSpeed
        ? kMinimumPlaybackSpeed
        : (absolute > kMaximumPlaybackSpeed ? kMaximumPlaybackSpeed : absolute);
    const float playback = state.action == SonicGroundAction::Rolling && state.blend_active ? 1.0f : speed;
    state.primary_speed = playback;
    state.secondary_speed = playback;
}

void set_sonic_ground_facing(SonicGroundAnimationState& state, bool facing_left) {
    require_initialized(state);
    apply_action(state, state.action, facing_left, true);
}

void start_sonic_jump_animation(SonicGroundAnimationState& state, bool facing_left) {
    apply_action(state, SonicGroundAction::Jump, facing_left, false);
    state.loop = true;
}

void start_sonic_crouch_animation(
    SonicGroundAnimationState& state,
    SonicGroundAction action,
    bool facing_left) {
    if (action != SonicGroundAction::CrouchStart && action != SonicGroundAction::Crouch
            && action != SonicGroundAction::CrouchEnd) {
        fail("crouch animation action is unsupported");
    }
    apply_action(state, action, facing_left, false);
    state.loop = action == SonicGroundAction::Crouch;
    state.primary_speed = 1.0f;
    state.secondary_speed = 1.0f;
}

void start_sonic_rolling_animation(SonicGroundAnimationState& state, bool facing_left) {
    apply_action(state, SonicGroundAction::Rolling, facing_left, false);
    if (state.blend_active) state.blend_weight_decrement = 0.125f;
    state.loop = true;
}

void start_sonic_spindash_animation(
    SonicGroundAnimationState& state,
    SonicGroundAction action,
    bool facing_left) {
    if (!is_charge_action(action)) {
        fail("spindash animation action is unsupported");
    }
    const auto previous = state.action;
    apply_action(state, action, facing_left, false);
    state.previous_action = previous;
    state.primary = make_motion_layer(action, facing_left);
    state.secondary = state.primary;
    state.blend_active = false;
    state.blend_weight = 0.0f;
    state.loop = action == SonicGroundAction::ChargeHold;
    state.primary_speed = 1.0f;
    state.secondary_speed = 1.0f;
}

void start_sonic_fall_animation(SonicGroundAnimationState& state, bool facing_left, bool rotated) {
    apply_action(state, rotated ? SonicGroundAction::FallRotated : SonicGroundAction::Fall, facing_left, false);
    state.loop = true;
    state.primary_speed = 1.0f;
    state.secondary_speed = 1.0f;
}

void start_sonic_fall_turn_animation(
    SonicGroundAnimationState& state,
    bool facing_left,
    bool rotated,
    std::uint32_t frame) {
    if (frame >= 10u) fail("fall turn animation frame is unsupported");
    const auto action = rotated ? SonicGroundAction::FallRotatedTurn : SonicGroundAction::FallTurn;
    apply_action(state, action, facing_left, false);
    state.primary = make_motion_layer(action, facing_left);
    state.primary.relative_frame = static_cast<float>(frame);
    state.secondary = state.primary;
    state.blend_active = false;
    state.blend_weight = 0.0f;
    state.loop = false;
    state.primary_speed = 1.0f;
    state.secondary_speed = 1.0f;
}

void set_sonic_jump_playback_speed(
    SonicGroundAnimationState& state,
    float velocity_x,
    float velocity_y,
    CameraPrecision precision) {
    require_precision(precision);
    require_initialized(state);
    require_finite(velocity_x, "jump animation horizontal speed is nonfinite");
    require_finite(velocity_y, "jump animation vertical speed is nonfinite");
    if (state.action != SonicGroundAction::Jump) {
        fail("jump animation action is unsupported");
    }
    const Arithmetic arithmetic{precision};
    const float squared = arithmetic.spill(arithmetic.add(
        arithmetic.multiply(velocity_x, velocity_x),
        arithmetic.multiply(velocity_y, velocity_y)));
    require_nonnegative_finite(squared, "jump animation squared speed is invalid");
    const float length = squared > 0.0f
        ? arithmetic.spill(arithmetic.rounded(std::sqrt(static_cast<double>(squared))))
        : squared;
    const float combined = arithmetic.spill(arithmetic.add(
        arithmetic.multiply(length, 0.125), arithmetic.multiply(length, 0.25)));
    const float speed = combined < kMinimumPlaybackSpeed ? kMinimumPlaybackSpeed
        : (combined > kMaximumPlaybackSpeed ? kMaximumPlaybackSpeed : combined);
    state.primary_speed = speed;
    state.secondary_speed = speed;
}

void advance_sonic_ground_blend(
    SonicGroundAnimationState& state,
    float blend_weight_decrement,
    CameraPrecision precision) {
    require_precision(precision);
    require_initialized(state);
    require_nonnegative_finite(
        blend_weight_decrement,
        "ground animation blend decrement is invalid");
    require_nonnegative_finite(state.blend_weight, "ground animation blend weight is invalid");
    if (!state.blend_active) {
        return;
    }

    const Arithmetic arithmetic{precision};
    const float remaining = arithmetic.spill(arithmetic.subtract(
        static_cast<double>(state.blend_weight),
        static_cast<double>(blend_weight_decrement)));
    if (remaining <= 0.0f) {
        state.blend_weight = 0.0f;
        state.blend_active = false;
        return;
    }
    state.blend_weight = remaining;
}

void advance_sonic_ground_blend(
    SonicGroundAnimationState& state,
    CameraPrecision precision) {
    advance_sonic_ground_blend(state, state.blend_weight_decrement, precision);
}

SonicGroundFrameAdvanceResult advance_sonic_ground_frames(
    SonicGroundAnimationState& state,
    float time_scale,
    CameraPrecision precision) {
    require_precision(precision);
    require_initialized(state);
    require_nonnegative_finite(time_scale, "ground animation time scale is invalid");
    require_layer(state.primary, "ground animation primary layer is invalid");
    require_layer(state.secondary, "ground animation secondary layer is invalid");
    require_nonnegative_finite(state.primary_speed, "ground animation primary speed is invalid");
    require_nonnegative_finite(state.secondary_speed, "ground animation secondary speed is invalid");

    const Arithmetic arithmetic{precision};
    const float primary_duration = relative_duration(state.primary, arithmetic);
    const float secondary_duration = relative_duration(state.secondary, arithmetic);
    const float next_primary = state.blend_weight < 1.0f || state.blend_active ? advance_frame(
        state.primary.relative_frame,
        state.primary_speed,
        time_scale,
        arithmetic) : state.primary.relative_frame;
    const float next_secondary = !state.blend_active && state.blend_weight > 0.0f ? advance_frame(
        state.secondary.relative_frame,
        state.secondary_speed,
        time_scale,
        arithmetic) : state.secondary.relative_frame;
    const bool primary_ended = next_primary >= (state.loop ? primary_duration : primary_duration - 1.0f);
    if (state.loop) {
        state.primary.relative_frame = loop_frame(next_primary, primary_duration, arithmetic);
        state.secondary.relative_frame = loop_frame(next_secondary, secondary_duration, arithmetic);
    } else {
        state.primary.relative_frame = clamp_frame(next_primary, primary_duration);
        state.secondary.relative_frame = clamp_frame(next_secondary, secondary_duration);
    }
    return {primary_ended};
}

std::string_view sonic_ground_motion_name(SonicGroundMotion motion) {
    switch (motion) {
    case SonicGroundMotion::IdleRight:
        return "SON_FW.ZNM";
    case SonicGroundMotion::IdleLeft:
        return "SON_FW_L.ZNM";
    case SonicGroundMotion::IdleWait0_01:
        return "SON_FWWAIT0_01.ZNM";
    case SonicGroundMotion::IdleWait0_02:
        return "SON_FWWAIT0_02.ZNM";
    case SonicGroundMotion::IdleWait1_01:
        return "SON_FWWAIT1_01.ZNM";
    case SonicGroundMotion::IdleWait1_02:
        return "SON_FWWAIT1_02.ZNM";
    case SonicGroundMotion::IdleWait2_01:
        return "SON_FWWAIT2_01.ZNM";
    case SonicGroundMotion::IdleWait2_02:
        return "SON_FWWAIT2_02.ZNM";
    case SonicGroundMotion::Walk:
        return "SON_WALK.ZNM";
    case SonicGroundMotion::Run:
        return "SON_RUN.ZNM";
    case SonicGroundMotion::Dash1:
        return "SON_DASH1.ZNM";
    case SonicGroundMotion::Dash2Right:
        return "SON_DASH2.ZNM";
    case SonicGroundMotion::Dash2Left:
        return "SON_DASH2_L.ZNM";
    case SonicGroundMotion::Spin:
        return "SON_SPIN_N.ZNM";
    case SonicGroundMotion::FallRight:
        return "SON_FALL.ZNM";
    case SonicGroundMotion::FallLeft:
        return "SON_FALL_L.ZNM";
    case SonicGroundMotion::FallRotatedRight:
        return "SON_FALL_R.ZNM";
    case SonicGroundMotion::FallRotatedLeft:
        return "SON_FALL_R_L.ZNM";
    case SonicGroundMotion::FallTurnRight:
        return "SON_FALL_TURN_L.ZNM";
    case SonicGroundMotion::FallTurnLeft:
        return "SON_FALL_TURN.ZNM";
    case SonicGroundMotion::FallRotatedTurnRight:
        return "SON_FALL_R_TURN_L.ZNM";
    case SonicGroundMotion::FallRotatedTurnLeft:
        return "SON_FALL_R_TURN.ZNM";
    case SonicGroundMotion::CrouchRight:
        return "SON_SQUAT_02.ZNM";
    case SonicGroundMotion::CrouchLeft:
        return "SON_SQUAT_L_02.ZNM";
    case SonicGroundMotion::CrouchEndRight:
        return "SON_SQUAT_03.ZNM";
    case SonicGroundMotion::CrouchEndLeft:
        return "SON_SQUAT_L_03.ZNM";
    case SonicGroundMotion::Rolling:
        return "SON_SPIN_B.ZNM";
    case SonicGroundMotion::Charge:
        return "SON_SPIN01.ZNM";
    }
    fail("ground animation motion is unsupported");
}

float sonic_ground_motion_sample_frame(
    const SonicGroundMotionLayer& layer,
    CameraPrecision precision) {
    require_precision(precision);
    require_layer(layer, "ground animation motion layer is invalid");
    const Arithmetic arithmetic{precision};
    return arithmetic.spill(arithmetic.add(
        static_cast<double>(layer.start_frame),
        static_cast<double>(layer.relative_frame)));
}
