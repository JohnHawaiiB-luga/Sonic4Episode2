#include "player_jump.h"
#include "nn_trig.h"

#include <cmath>
#include <stdexcept>

namespace {

constexpr std::uint32_t kMoveJumpInitializeMask = 0xffbffffeu;
constexpr std::uint32_t kMoveJumpInitializeBits = 0x00008010u;
constexpr std::uint32_t kMoveLandingMask = 0xffff7fefu;
constexpr std::uint32_t kMoveLandingBits = 0x00000080u;
constexpr std::uint32_t kPlayerJumpReleaseMask = 0x00000005u;
constexpr std::uint32_t kPlayerJumpReleaseBit = 0x00000004u;
constexpr std::uint32_t kPlayerAutomaticBit = 0x00008000u;
constexpr std::uint32_t kPlayerTruckBit = 0x00040000u;
constexpr float kJumpSpeed = 5.64697265625f;
constexpr float kReleaseVelocity = -0.25f;
constexpr float kLandingSpeedLimit = 15.0f;
constexpr float kLandingWorkingMaximum = 13.0f;
constexpr float kFallWaitTime = 24.0f;

[[noreturn]] void fail(const char* message) {
    throw std::invalid_argument(message);
}

void require_precision(ObjectMovementPrecision precision) {
    if (precision != ObjectMovementPrecision::Single && precision != ObjectMovementPrecision::Double) {
        fail("player jump precision is unsupported");
    }
}

void require_finite(float value, const char* message) {
    if (!std::isfinite(value)) {
        fail(message);
    }
}

void require_vector(const ObjectMovementVector& value, const char* message) {
    require_finite(value.x, message);
    require_finite(value.y, message);
    require_finite(value.z, message);
}

void validate_normal_state(
    const ObjectMovementState& object,
    const PlayerJumpSequenceState& sequence,
    ObjectMovementPrecision precision) {
    require_precision(precision);
    require_vector(object.position, "player jump position is nonfinite");
    require_vector(object.previous_position, "player jump previous position is nonfinite");
    require_vector(object.velocity, "player jump velocity is nonfinite");
    require_vector(object.velocity_add, "player jump velocity addition is nonfinite");
    require_vector(object.flow, "player jump flow is nonfinite");
    require_vector(object.movement, "player jump movement is nonfinite");
    require_finite(object.ground_speed, "player jump ground speed is nonfinite");
    require_finite(object.gravity, "player jump gravity is nonfinite");
    require_finite(object.maximum_fall_speed, "player jump fall-speed maximum is nonfinite");
    require_finite(object.hitstop_timer, "player jump hitstop timer is nonfinite");
    require_finite(sequence.working_maximum, "player jump working maximum is nonfinite");
    require_finite(sequence.fall_timer, "player fall timer is nonfinite");
    if (object.fall_orientation != 0u) {
        fail("player jump surface is unsupported");
    }
    if ((sequence.player_flags & (kPlayerAutomaticBit | kPlayerTruckBit)) != 0u) {
        fail("player jump route is unsupported");
    }
}

void validate_flat_state(
    const ObjectMovementState& object,
    const PlayerJumpSequenceState& sequence,
    ObjectMovementPrecision precision) {
    validate_normal_state(object, sequence, precision);
    if ((static_cast<std::uint16_t>(object.surface_angle + 0x2000u) & 0xc000u) != 0u) {
        fail("player jump surface is unsupported");
    }
}

struct Arithmetic {
    ObjectMovementPrecision precision;

    double rounded(double value) const {
        if (precision == ObjectMovementPrecision::Double || !std::isfinite(value)) {
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

float absolute(float value, const Arithmetic& arithmetic) {
    if (value < 0.0f) {
        return arithmetic.spill(-static_cast<double>(value));
    }
    return arithmetic.spill(value);
}

float clamp_landing_speed(float value, const Arithmetic& arithmetic) {
    if (value > kLandingSpeedLimit) {
        return arithmetic.spill(kLandingSpeedLimit);
    }
    if (value < -kLandingSpeedLimit) {
        return arithmetic.spill(-kLandingSpeedLimit);
    }
    return arithmetic.spill(value);
}

}

std::uint32_t begin_ordinary_player_fall_turn(
    PlayerFallTurnState& turn,
    PlayerJumpSequenceState& sequence,
    std::uint32_t action,
    bool& facing_left) {
    if (action < 40u || action > 43u || turn.frame >= 10u) {
        fail("player fall turn state is unsupported");
    }
    const auto previous_frame = (sequence.player_flags & 0x80000000u) != 0u ? turn.frame : 0u;
    if ((sequence.player_flags & 0x80000000u) == 0u) turn.saved_action = action;
    turn.from_left = facing_left;
    facing_left = !facing_left;
    turn.angle = 0u;
    sequence.player_flags = (sequence.player_flags & 0x7ffffeefu) | 0x80000010u;
    turn.frame = previous_frame == 0u ? 0u : 10u - previous_frame;
    return action == 42u || action == 43u ? 43u : 41u;
}

bool advance_ordinary_player_fall_turn(
    PlayerFallTurnState& turn,
    PlayerJumpSequenceState& sequence) {
    if ((sequence.player_flags & 0x80000000u) == 0u) return false;
    if (turn.frame >= 10u) fail("player fall turn frame is unsupported");
    constexpr std::uint16_t from_right[]{28503u, 24238u, 19973u, 15708u, 11443u,
        7178u, 2912u, 1942u, 972u, 0u};
    constexpr std::uint16_t from_left[]{37033u, 41298u, 45563u, 49828u, 54093u,
        58358u, 62624u, 63594u, 64564u, 0u};
    turn.angle = turn.from_left ? from_left[turn.frame] : from_right[turn.frame];
    ++turn.frame;
    if (turn.frame < 10u) return false;
    turn.frame = 9u;
    turn.angle = 0u;
    sequence.player_flags &= 0x7fffffefu;
    return true;
}

bool check_ordinary_player_fall(
    const ObjectMovementState& object,
    PlayerJumpSequenceState& sequence,
    float time_scale,
    ObjectMovementPrecision precision) {
    validate_normal_state(object, sequence, precision);
    require_finite(time_scale, "player fall time scale is nonfinite");
    if (time_scale < 0.0f) fail("player fall time scale is negative");
    if ((object.move_flags & 1u) == 0u) return true;
    if (sequence.fall_timer != 0.0f) {
        const Arithmetic arithmetic{precision};
        const float remaining = arithmetic.spill(arithmetic.subtract(sequence.fall_timer, time_scale));
        sequence.fall_timer = remaining < 0.0f ? 0.0f : remaining;
        return false;
    }
    if (object.surface_angle >= 0x4000u && object.surface_angle <= 0xc000u
            && std::fabs(object.ground_speed) < 2.0f) {
        sequence.fall_timer = kFallWaitTime;
        return true;
    }
    return false;
}

void initialize_ordinary_seq16_fall(
    ObjectMovementState& object,
    PlayerJumpSequenceState& sequence,
    ObjectMovementPrecision precision) {
    initialize_ordinary_seq16_fall(object, sequence, precision, 0u);
}

void initialize_ordinary_seq16_fall(
    ObjectMovementState& object,
    PlayerJumpSequenceState& sequence,
    ObjectMovementPrecision precision,
    std::uint16_t stage_id) {
    validate_normal_state(object, sequence, precision);
    ObjectMovementState next_object = object;
    PlayerJumpSequenceState next_sequence = sequence;
    const Arithmetic arithmetic{precision};
    const auto rotation = nn_sin_cos(next_object.surface_angle,
        precision == ObjectMovementPrecision::Single ? CameraPrecision::Single : CameraPrecision::Double);
    next_object.move_flags = (next_object.move_flags & 0xfffffffeu) | 0x8090u;
    next_object.velocity.x = arithmetic.spill(arithmetic.multiply(rotation.cosine, next_object.ground_speed));
    next_object.velocity.y = arithmetic.spill(arithmetic.multiply(rotation.sine, next_object.ground_speed));
    next_object.ground_speed = 0.0f;
    const auto angle = next_object.surface_angle;
    if (stage_id < 28u && (static_cast<std::uint16_t>(angle - 0x3c00u) <= 0x800u
            || static_cast<std::uint16_t>(angle + 0x4400u) <= 0x800u)) {
        const float velocity = next_object.velocity.y;
        if (velocity < -9.0f) {
            next_object.velocity.y = arithmetic.spill(arithmetic.subtract(arithmetic.multiply(
                arithmetic.subtract(velocity, -9.0), 0.3f), 8.2f));
        } else if (velocity < -8.5f) {
            next_object.velocity.y = arithmetic.spill(arithmetic.subtract(arithmetic.multiply(
                arithmetic.subtract(velocity, -8.5), 0.4f), 8.0));
        } else if (velocity < -8.0f) {
            next_object.velocity.y = arithmetic.spill(arithmetic.subtract(arithmetic.multiply(
                arithmetic.subtract(velocity, -8.0), 0.5), 7.75));
        } else if (velocity < -7.5f) {
            next_object.velocity.y = arithmetic.spill(arithmetic.subtract(arithmetic.multiply(
                arithmetic.subtract(velocity, -7.5), 0.6f), 7.45f));
        } else if (velocity < -7.0f) {
            next_object.velocity.y = arithmetic.spill(arithmetic.subtract(arithmetic.multiply(
                arithmetic.subtract(velocity, -7.0), 0.9f), 7.0));
        }
    }
    next_sequence.player_flags = (next_sequence.player_flags & 0xfffffff1u) | 1u;
    object = next_object;
    sequence = next_sequence;
}

void initialize_flat_seq17_jump(
    ObjectMovementState& object,
    PlayerJumpSequenceState& sequence,
    ObjectMovementPrecision precision) {
    validate_flat_state(object, sequence, precision);
    initialize_ordinary_seq17_jump(object, sequence, precision);
}

void initialize_ordinary_seq17_jump(
    ObjectMovementState& object,
    PlayerJumpSequenceState& sequence,
    ObjectMovementPrecision precision) {
    validate_normal_state(object, sequence, precision);

    ObjectMovementState next_object = object;
    PlayerJumpSequenceState next_sequence = sequence;
    next_object.move_flags = (next_object.move_flags & kMoveJumpInitializeMask) | kMoveJumpInitializeBits;
    const Arithmetic arithmetic{precision};
    const auto angle = next_object.surface_angle;
    auto tangent_angle = angle;
    const auto shifted_angle = static_cast<std::uint16_t>(angle + 0x100u);
    if ((shifted_angle & 0x2000u) != 0u && (shifted_angle & 0x0fffu) <= 0x400u) {
        if (next_object.ground_speed > 0.0f && angle < 0x8000u) {
            tangent_angle = static_cast<std::uint16_t>(angle - 0x480u);
        } else if (next_object.ground_speed < 0.0f && angle > 0x8000u) {
            tangent_angle = static_cast<std::uint16_t>(angle + 0x480u);
        }
    }
    const auto camera_precision = precision == ObjectMovementPrecision::Single
        ? CameraPrecision::Single : CameraPrecision::Double;
    const auto tangent = nn_sin_cos(tangent_angle, camera_precision);
    const auto rotation = nn_sin_cos(angle, camera_precision);
    const float horizontal_velocity = arithmetic.spill(arithmetic.multiply(
        tangent.cosine, next_object.ground_speed));
    const float vertical_velocity = arithmetic.spill(arithmetic.multiply(
        tangent.sine, next_object.ground_speed));
    next_object.velocity.x = arithmetic.spill(arithmetic.add(
        arithmetic.multiply(rotation.sine, kJumpSpeed), horizontal_velocity));
    next_object.velocity.y = arithmetic.spill(arithmetic.subtract(
        vertical_velocity, arithmetic.multiply(rotation.cosine, kJumpSpeed)));
    next_sequence.player_flags &= 0xfffffff0u;
    next_sequence.player_flags &= 0xffffffdfu;
    next_sequence.player_flags &= 0xffffff70u;
    object = next_object;
    sequence = next_sequence;
}

bool can_start_ordinary_player_jump_dash(
    const PlayerJumpSequenceState& sequence,
    bool jump_pushed,
    float homing_timer,
    bool target_present,
    bool ending) {
    require_finite(homing_timer, "player homing timer is nonfinite");
    return jump_pushed && homing_timer == 0.0f && !target_present && !ending
        && (sequence.player_flags & 0x80u) == 0u;
}

void initialize_ordinary_seq21_jump_dash(
    ObjectMovementState& object,
    PlayerJumpSequenceState& sequence,
    PlayerJumpDashState& dash,
    float& deceleration_delay,
    bool facing_left,
    ObjectMovementPrecision precision) {
    validate_normal_state(object, sequence, precision);
    require_finite(deceleration_delay, "player jump-dash deceleration delay is nonfinite");
    ObjectMovementState next = object;
    const Arithmetic arithmetic{precision};
    const auto rotation = nn_sin_cos(facing_left ? 0x8800u : 0xf800u,
        precision == ObjectMovementPrecision::Single ? CameraPrecision::Single : CameraPrecision::Double);
    const float horizontal = arithmetic.spill(arithmetic.multiply(rotation.cosine, 4.0));
    const float vertical = arithmetic.spill(arithmetic.multiply(rotation.sine, 4.0));
    next.velocity.x = arithmetic.spill(arithmetic.add(next.velocity.x, horizontal));
    next.velocity.y = arithmetic.spill(arithmetic.subtract(0.0, vertical));
    next.surface_angle = 0u;
    next.move_flags = (next.move_flags & 0xfffffffeu) | 0x8010u;
    object = next;
    sequence.player_flags |= 0xa0u;
    dash.timer = 20u;
    deceleration_delay = 1.0f;
}

PlayerJumpDashTransition advance_ordinary_player_jump_dash(
    ObjectMovementState& object,
    PlayerJumpSequenceState& sequence,
    PlayerJumpDashState& dash,
    ObjectMovementPrecision precision) {
    validate_normal_state(object, sequence, precision);
    if ((object.move_flags & 1u) != 0u) {
        sequence.player_flags &= ~0x20u;
        return PlayerJumpDashTransition::Landing;
    }
    if (dash.timer == 0u) {
        const auto velocity = object.velocity;
        initialize_ordinary_seq16_fall(object, sequence, precision);
        object.velocity = velocity;
        sequence.player_flags &= ~0x20u;
        return PlayerJumpDashTransition::Fall;
    }
    --dash.timer;
    return PlayerJumpDashTransition::None;
}

void update_flat_player_jump_release(
    ObjectMovementState& object,
    PlayerJumpSequenceState& sequence,
    bool jump_held,
    ObjectMovementPrecision precision) {
    validate_flat_state(object, sequence, precision);
    update_ordinary_player_jump_release(object, sequence, jump_held, precision);
}

void update_ordinary_player_jump_release(
    ObjectMovementState& object,
    PlayerJumpSequenceState& sequence,
    bool jump_held,
    ObjectMovementPrecision precision) {
    validate_normal_state(object, sequence, precision);

    const Arithmetic arithmetic{precision};
    ObjectMovementState next_object = object;
    PlayerJumpSequenceState next_sequence = sequence;
    if ((next_sequence.player_flags & kPlayerJumpReleaseMask) == 0u && !jump_held &&
        next_object.velocity.y < kReleaseVelocity) {
        next_sequence.player_flags |= kPlayerJumpReleaseBit;
    }
    if ((next_sequence.player_flags & kPlayerJumpReleaseBit) != 0u && next_object.velocity.y < 0.0f) {
        next_object.velocity.y = arithmetic.spill(arithmetic.add(
            static_cast<double>(next_object.gravity), static_cast<double>(next_object.velocity.y)));
    }
    object = next_object;
    sequence = next_sequence;
}

void apply_flat_player_landing(
    ObjectMovementState& object,
    PlayerJumpSequenceState& sequence,
    ObjectMovementPrecision precision) {
    validate_flat_state(object, sequence, precision);
    apply_ordinary_player_landing(object, sequence, precision);
}

void apply_ordinary_player_landing(
    ObjectMovementState& object,
    PlayerJumpSequenceState& sequence,
    ObjectMovementPrecision precision) {
    validate_normal_state(object, sequence, precision);

    const Arithmetic arithmetic{precision};
    ObjectMovementState next_object = object;
    PlayerJumpSequenceState next_sequence = sequence;
    next_sequence.fall_timer = kFallWaitTime;
    next_sequence.player_flags &= 0xffffff5fu;
    next_object.move_flags = (next_object.move_flags & kMoveLandingMask) | kMoveLandingBits;

    if (absolute(next_object.ground_speed, arithmetic) < absolute(next_object.velocity.x, arithmetic)) {
        next_object.ground_speed = next_object.velocity.x;
    }
    const auto rotation = nn_sin_cos(next_object.surface_angle,
        precision == ObjectMovementPrecision::Single ? CameraPrecision::Single : CameraPrecision::Double);
    const double landing_addition = arithmetic.multiply(
        rotation.sine, absolute(next_object.velocity.x, arithmetic));
    next_object.ground_speed = arithmetic.spill(arithmetic.add(
        next_object.ground_speed, landing_addition));
    next_object.ground_speed = clamp_landing_speed(next_object.ground_speed, arithmetic);
    next_sequence.working_maximum = absolute(next_object.ground_speed, arithmetic);
    if (kLandingWorkingMaximum < next_sequence.working_maximum) {
        next_sequence.working_maximum = kLandingWorkingMaximum;
    }
    next_object.velocity.x = 0.0f;
    next_object.velocity.y = 0.0f;
    object = next_object;
    sequence = next_sequence;
}
