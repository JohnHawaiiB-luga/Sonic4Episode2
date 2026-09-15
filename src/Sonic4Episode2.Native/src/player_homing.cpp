#include "player_homing.h"

#include "nn_trig.h"

#include <cmath>
#include <stdexcept>
#include <unordered_set>

namespace {

void require_number(float value) {
    if (!std::isfinite(value) || std::fabs(value) > 16777216.0f) {
        throw std::invalid_argument("player homing input is invalid");
    }
}

void validate(
    const ObjectMovementState& object,
    const PlayerJumpSequenceState& sequence,
    ObjectMovementPrecision precision) {
    if (precision != ObjectMovementPrecision::Single && precision != ObjectMovementPrecision::Double) {
        throw std::invalid_argument("player homing precision is unsupported");
    }
    if (object.fall_orientation != 0u || (sequence.player_flags & 0x48000u) != 0u) {
        throw std::invalid_argument("player homing movement context is unsupported");
    }
    for (const auto& vector : {object.position, object.velocity, object.velocity_add}) {
        require_number(vector.x);
        require_number(vector.y);
        require_number(vector.z);
    }
    for (float value : {object.ground_speed, sequence.working_maximum, sequence.fall_timer}) {
        require_number(value);
    }
}

void validate(const PlayerHomingState& homing) {
    for (float timer : {homing.timer, homing.homing_timer, homing.boost_timer}) {
        require_number(timer);
        if (timer < 0.0f) throw std::invalid_argument("player homing timer is negative");
    }
}

struct Arithmetic {
    ObjectMovementPrecision precision;

    double rounded(double value) const {
        if (precision == ObjectMovementPrecision::Double) return value;
        int exponent = 0;
        const float mantissa = static_cast<float>(std::frexp(value, &exponent));
        return std::ldexp(static_cast<double>(mantissa), exponent);
    }

    float add(float left, double right) const {
        return static_cast<float>(rounded(static_cast<double>(left) + right));
    }

    float subtract(float left, double right) const {
        return static_cast<float>(rounded(static_cast<double>(left) - right));
    }
};

void land(
    ObjectMovementState& object,
    PlayerJumpSequenceState& sequence,
    PlayerHomingState& homing,
    ObjectMovementPrecision precision) {
    apply_ordinary_player_landing(object, sequence, precision);
    homing.display_flags &= 0xffffffdfu;
    homing.gimmick_flags &= 0xfeffffffu;
    homing.secondary_player_flags &= 0xfeffffffu;
}

bool is_homing_event(std::uint16_t event, std::uint8_t parameter) {
    if (event >= 63u && event <= 67u && parameter == 0u) return true;
    if ((event >= 70u && event <= 79u) || (event >= 91u && event <= 92u) ||
        (event >= 129u && event <= 131u)) return true;
    switch (event) {
    case 100u:
    case 111u:
    case 116u:
    case 165u:
    case 248u:
    case 295u:
    case 474u:
        return true;
    default:
        return false;
    }
}

}

PlayerHomingSearchResult select_ordinary_player_homing_target(
    const PlayerHomingSearchInput& input,
    const std::vector<PlayerHomingCandidate>& candidates,
    ObjectMovementPrecision precision) {
    if (precision != ObjectMovementPrecision::Single && precision != ObjectMovementPrecision::Double) {
        throw std::invalid_argument("player homing search precision is unsupported");
    }
    if (input.fall_orientation != 0u) {
        throw std::invalid_argument("player homing search orientation is unsupported");
    }
    for (float value : {input.player.position.x, input.player.position.y, input.boost_timer,
                        input.range_extension, input.time_scale}) {
        require_number(value);
    }
    if (input.time_scale < 0.0f || input.cursor_target == 0u) {
        throw std::invalid_argument("player homing search context is invalid");
    }
    std::unordered_set<std::uint32_t> identities;
    for (const auto& candidate : candidates) {
        if (candidate.id == 0u || !identities.insert(candidate.id).second) {
            throw std::invalid_argument("player homing candidate identity is invalid");
        }
    }

    const Arithmetic arithmetic{precision};
    PlayerHomingSearchResult result{std::nullopt, input.cursor_target, input.boost_timer};
    const double radius = arithmetic.rounded(static_cast<double>(input.range_extension) + 192.0);
    float closest_distance = static_cast<float>(arithmetic.rounded(radius * radius));
    float vertical_weight = 1.5f;
    if (input.boost_timer != 0.0f) {
        vertical_weight = 1.0f;
        result.boost_timer = arithmetic.subtract(input.boost_timer, input.time_scale);
        if (result.boost_timer < 0.0f) result.boost_timer = 0.0f;
    }
    const double player_offset_y = static_cast<double>(static_cast<std::int32_t>(input.player.top) +
                                                       input.player.bottom) * 0.5;
    const float player_center_y = arithmetic.add(input.player.position.y, player_offset_y);
    const bool facing_left = (input.player.display_flags & 1u) != 0u;
    const std::int32_t minimum_angle = facing_left ? 0x471d : 0;
    const std::int32_t maximum_angle = facing_left ? 0x8000 : 0x38e3;
    const PlayerHomingTarget* rectangle = &input.player;

    for (const auto& candidate : candidates) {
        if ((candidate.target.display_flags & 0x20u) != 0u) continue;
        if (input.require_same_plane && ((candidate.object_flags ^ input.object_flags) & 1u) != 0u) continue;
        if (candidate.type == 1u) {
            if (candidate.registered_player || (candidate.secondary_player_flags & 0x400000u) != 0u) continue;
            require_number(candidate.player_timer);
            if (candidate.player_timer > 0.0f) continue;
            rectangle = &candidate.target;
        } else if (candidate.type == 2u) {
            if ((candidate.entity_flags & 0x8000u) != 0u) continue;
            rectangle = &candidate.target;
        } else if (candidate.type == 3u) {
            if ((candidate.entity_flags & 0x8000u) != 0u) continue;
            if ((candidate.entity_flags & 0x2000u) == 0u &&
                !is_homing_event(candidate.event_id, candidate.event_parameter)) continue;
        } else {
            continue;
        }
        require_number(candidate.target.position.x);
        require_number(candidate.target.position.y);
        const double offset_x = static_cast<double>(static_cast<std::int32_t>(rectangle->left) + rectangle->right) * 0.5;
        const double offset_y = static_cast<double>(static_cast<std::int32_t>(rectangle->top) + rectangle->bottom) * 0.5;
        const float center_x = (candidate.target.display_flags & 1u) != 0u
            ? arithmetic.subtract(candidate.target.position.x, offset_x)
            : arithmetic.add(candidate.target.position.x, offset_x);
        const float center_y = arithmetic.add(candidate.target.position.y, offset_y);
        const float delta_x = arithmetic.subtract(center_x, input.player.position.x);
        const float delta_y = arithmetic.subtract(center_y, player_center_y);
        const double angle = std::atan2(static_cast<double>(delta_y), static_cast<double>(delta_x)) * 10430.3779296875;
        const auto direction = static_cast<std::int32_t>(arithmetic.rounded(angle));
        if (direction < minimum_angle || direction > maximum_angle) continue;

        const float weighted_y = static_cast<float>(arithmetic.rounded(static_cast<double>(delta_y) * vertical_weight));
        const double distance_x = arithmetic.rounded(static_cast<double>(delta_x) * delta_x);
        const double distance_y = arithmetic.rounded(static_cast<double>(weighted_y) * weighted_y);
        const float distance = static_cast<float>(arithmetic.rounded(distance_x + distance_y));
        if (distance < closest_distance) {
            closest_distance = distance;
            result.target = candidate.id;
        }
    }
    if (result.cursor_target != result.target || (input.sequence_flags & 0x20000u) != 0u ||
        (input.sequence_attributes & 0x10u) == 0u || (input.player_flags & 0x80u) != 0u) {
        result.cursor_target.reset();
    }
    return result;
}

PlayerHomingInitialization initialize_ordinary_seq19_homing(
    ObjectMovementState& object,
    PlayerJumpSequenceState& sequence,
    PlayerHomingState& homing,
    bool target_present,
    ObjectMovementPrecision precision) {
    validate(object, sequence, precision);
    validate(homing);
    if (!target_present) return {PlayerHomingTransition::JumpDash, std::nullopt};

    PlayerHomingInitialization result;
    if ((sequence.player_flags & 0x20000u) == 0u) {
        result.action = 31u;
        homing.display_flags |= 4u;
    }
    object.move_flags = (object.move_flags & 0xffffff7eu) | 0x8010u;
    object.surface_angle = 0u;
    sequence.player_flags |= 0x80u;
    homing.gimmick_flags &= 0xfdfff7fcu;
    homing.timer = 32.0f;
    homing.homing_timer = 24.0f;
    homing.boost_timer = 64.0f;
    return result;
}

PlayerHomingTransition advance_ordinary_player_homing(
    ObjectMovementState& object,
    PlayerJumpSequenceState& sequence,
    PlayerHomingState& homing,
    const std::optional<PlayerHomingTarget>& target,
    float time_scale,
    ObjectMovementPrecision precision) {
    validate(object, sequence, precision);
    validate(homing);
    require_number(time_scale);
    if (time_scale < 0.0f) throw std::invalid_argument("player homing time scale is negative");
    if (target.has_value()) {
        require_number(target->position.x);
        require_number(target->position.y);
        require_number(target->position.z);
    }
    if (homing.timer == 0.0f && !std::signbit(homing.timer)) return PlayerHomingTransition::Fall;

    const Arithmetic arithmetic{precision};
    auto next_object = object;
    auto next_sequence = sequence;
    auto next_homing = homing;
    next_homing.timer = arithmetic.subtract(next_homing.timer, time_scale);
    if (next_homing.timer < 0.0f) next_homing.timer = 0.0f;
    auto transition = PlayerHomingTransition::None;
    if ((next_object.move_flags & 1u) != 0u) {
        land(next_object, next_sequence, next_homing, precision);
        transition = PlayerHomingTransition::Landing;
    } else if (target.has_value()) {
        const double offset_x = static_cast<double>(static_cast<std::int32_t>(target->left) + target->right) * 0.5;
        const double offset_y = static_cast<double>(static_cast<std::int32_t>(target->top) + target->bottom) * 0.5;
        const float center_x = (target->display_flags & 1u) != 0u
            ? arithmetic.subtract(target->position.x, offset_x) : arithmetic.add(target->position.x, offset_x);
        const float center_y = (target->display_flags & 2u) != 0u
            ? arithmetic.subtract(target->position.y, offset_y) : arithmetic.add(target->position.y, offset_y);
        const float delta_x = arithmetic.subtract(center_x, next_object.position.x);
        const float delta_y = arithmetic.subtract(center_y, next_object.position.y);
        const double angle = std::atan2(static_cast<double>(delta_y), static_cast<double>(delta_x)) * 10430.3779296875;
        const auto direction = static_cast<std::uint16_t>(static_cast<std::int32_t>(arithmetic.rounded(angle)));
        const auto rotation = nn_sin_cos(direction,
            precision == ObjectMovementPrecision::Single ? CameraPrecision::Single : CameraPrecision::Double);
        next_object.velocity.x = static_cast<float>(arithmetic.rounded(rotation.cosine * 15.0));
        next_object.velocity.y = static_cast<float>(arithmetic.rounded(rotation.sine * 15.0));
        if (next_object.velocity.x < 0.0f) next_homing.display_flags |= 1u;
        else if (next_object.velocity.x > 0.0f) next_homing.display_flags &= ~1u;
        if (next_homing.timer <= 30.0f && std::fabs(next_object.velocity.x) > 0.0625f &&
            (next_object.move_flags & 4u) != 0u) {
            land(next_object, next_sequence, next_homing, precision);
            transition = PlayerHomingTransition::Landing;
        }
    }
    object = next_object;
    sequence = next_sequence;
    homing = next_homing;
    return transition;
}

std::optional<std::uint32_t> initialize_ordinary_seq20_homing_rebound(
    ObjectMovementState& object,
    PlayerJumpSequenceState& sequence,
    PlayerHomingState& homing,
    ObjectMovementPrecision precision) {
    validate(object, sequence, precision);
    validate(homing);
    std::optional<std::uint32_t> action;
    if ((sequence.player_flags & 0x20000u) == 0u) {
        action = (homing.secondary_player_flags & 0x100u) != 0u ? 58u : 32u;
        homing.secondary_player_flags ^= 0x100u;
    }
    sequence.player_flags &= 0xffffff70u;
    homing.display_flags |= 4u;
    object.move_flags = (object.move_flags & 0xfffffffeu) | 0x8090u;
    object.velocity.x = 0.0f;
    object.velocity.y = (sequence.player_flags & 0x4000000u) != 0u ? -3.75f : -5.0f;
    object.velocity_add.x = object.velocity_add.y = 0.0f;
    object.ground_speed = 0.0f;
    homing.timer = 0.0f;
    homing.user_work = homing.player_timer = 0u;
    return action;
}

PlayerHomingTransition advance_ordinary_player_homing_rebound(
    ObjectMovementState& object,
    PlayerJumpSequenceState& sequence,
    PlayerHomingState& homing,
    ObjectMovementPrecision precision) {
    validate(object, sequence, precision);
    validate(homing);
    if (object.velocity.y >= 0.0f) return PlayerHomingTransition::Fall;
    if ((object.move_flags & 1u) == 0u) return PlayerHomingTransition::None;
    land(object, sequence, homing, precision);
    return PlayerHomingTransition::Landing;
}
