#include "native_player_scene.h"

#include "stage_data.h"
#include "terrain_data.h"
#include "nn_trig.h"
#include "normal_camera_view.h"
#include "player_spindash.h"
#include "damage_ring_pickup.h"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <stdexcept>
#include <utility>
#include <vector>

namespace sonic4ep2::app {
namespace {

std::vector<std::uint8_t> read_member(
    const std::filesystem::path& path,
    const std::string& name) {
    std::ifstream input(path, std::ios::binary | std::ios::ate);
    if (!input) throw std::runtime_error("Cannot open original terrain archive: " + path.u8string());
    const auto length = input.tellg();
    if (length <= 0 || length > 512 * 1024 * 1024) {
        throw std::runtime_error("Original terrain archive size is unsupported.");
    }
    std::vector<std::uint8_t> bytes(static_cast<std::size_t>(length));
    input.seekg(0);
    input.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    if (!input) throw std::runtime_error("Cannot read original terrain archive: " + path.u8string());
    const auto entries = parse_pc_stage_archive(bytes.data(), bytes.size());
    const StageArchiveEntry* found = nullptr;
    for (const auto& entry : entries) {
        const auto separator = entry.name.find_last_of("/\\");
        const auto basename = entry.name.substr(separator == std::string::npos ? 0u : separator + 1u);
        if (basename != name) continue;
        if (found != nullptr) throw std::runtime_error("Original terrain member is ambiguous.");
        found = &entry;
    }
    if (found == nullptr) throw std::runtime_error("Original terrain member is missing: " + name);
    const auto* begin = bytes.data() + found->offset;
    return {begin, begin + found->length};
}

TerrainCollision load_terrain(const std::filesystem::path& data_root) {
    const auto map_archive = data_root / "G_ZONE1/MAP/ZONE11_MAP.AMB";
    const auto attribute_archive = data_root / "G_ZONE1/MAP/ZONE1_ATTR.AMB";
    const auto first = read_member(map_archive, "ZONE11_ATTR_A.MP");
    const auto second = read_member(map_archive, "ZONE11_ATTR_B.MP");
    const auto height = read_member(attribute_archive, "ZONE1.DF");
    const auto angle = read_member(attribute_archive, "ZONE1.DI");
    const auto attribute = read_member(attribute_archive, "ZONE1.AT");
    return TerrainCollision{
        parse_stage_grid(first.data(), first.size(), 2u),
        parse_stage_grid(second.data(), second.size(), 2u),
        parse_terrain_table(height.data(), height.size(), TerrainRecordKind::Height),
        parse_terrain_table(angle.data(), angle.size(), TerrainRecordKind::Angle),
        parse_terrain_table(attribute.data(), attribute.size(), TerrainRecordKind::Attribute),
        {512, 512, 32128, 3968}};
}

constexpr PlayerGroundWalkParameters kNormalParameters{
    0.035400390625f, 9.0f, 0.125f, 3.6f, 2.0f, 0.0625f, 13.0f, 0x2000u};
constexpr ObjectSlopeParameters kNormalSlopeParameters{
    kNormalParameters.slope_acceleration, kNormalParameters.slope_acceleration,
    kNormalParameters.slope_speed_cap};
constexpr PlayerAirParameters kAirParameters{0.0625f, 9.0f, 0.0625f, 2.7f};
constexpr ObjectContactEnvironment kContactEnvironment{false, false, 0u, 6};
constexpr float kDrawScale = 3.2f;
constexpr float kRingDepth = -16.0f;

bool is_air_sequence(std::uint32_t sequence) {
    return sequence == 16u || sequence == 17u || sequence == 21u;
}

StageRingPlacements load_rings(const std::filesystem::path& data_root) {
    const auto bytes = read_member(data_root / "G_ZONE1/MAP/ZONE11_MAP.AMB", "ZONE11.RG");
    return parse_stage_ring_placements(bytes.data(), bytes.size());
}

AmeEffectData load_player_effect(const std::filesystem::path& data_root, const std::string& name) {
    const auto bytes = read_member(data_root / "G_COM/EFF/EP2_EFF_CMN.AMB", name);
    return parse_pc_ame_effect(bytes.data(), bytes.size());
}

void write_rectangle(std::ostream& output, const PlayerRectangleState& rectangle) {
    const auto& bounds = rectangle.bounds;
    output << "{\"flags\":" << rectangle.flags << ",\"bounds\":["
        << bounds.left << ',' << bounds.top << ',' << bounds.back << ','
        << bounds.right << ',' << bounds.bottom << ',' << bounds.front
        << "],\"position\":[" << rectangle.position[0] << ',' << rectangle.position[1]
        << ',' << rectangle.position[2] << "],\"attack_mask\":" << rectangle.attack_mask
        << ",\"defense_mask\":" << rectangle.defense_mask
        << ",\"attack_power\":" << rectangle.attack_power
        << ",\"defense_power\":" << rectangle.defense_power
        << ",\"group\":" << static_cast<unsigned>(rectangle.group)
        << ",\"target_groups\":" << static_cast<unsigned>(rectangle.target_groups) << '}';
}

void rotate_z(CameraMatrix& matrix, std::uint16_t angle) {
    if (angle == 0u) return;
    const auto rotation = nn_sin_cos(angle, CameraPrecision::Single);
    for (std::size_t index = 0u; index < 3u; ++index) {
        const float old_x = matrix[index];
        const float old_y = matrix[index + 4u];
        const float x_cosine = old_x * rotation.cosine;
        const float y_sine = old_y * rotation.sine;
        const float x_sine = old_x * rotation.sine;
        const float y_cosine = old_y * rotation.cosine;
        matrix[index] = x_cosine + y_sine;
        matrix[index + 4u] = y_cosine - x_sine;
    }
}

void rotate_y(CameraMatrix& matrix, std::uint16_t angle) {
    if (angle == 0u) return;
    const auto rotation = nn_sin_cos(angle, CameraPrecision::Single);
    for (std::size_t index = 0u; index < 3u; ++index) {
        const float old_x = matrix[index];
        const float old_z = matrix[index + 8u];
        const float x_cosine = old_x * rotation.cosine;
        const float z_sine = old_z * rotation.sine;
        const float x_sine = old_x * rotation.sine;
        const float z_cosine = old_z * rotation.cosine;
        matrix[index] = x_cosine - z_sine;
        matrix[index + 8u] = x_sine + z_cosine;
    }
}

}

NativePlayerScene::NativePlayerScene(const std::filesystem::path& data_root)
    : terrain_(load_terrain(data_root)), stage_rings_(load_rings(data_root)),
      rings_taken_(stage_rings_.rings.size(), 0u), ring_draw_positions_(stage_rings_.rings),
      ring_effect_data_(load_player_effect(data_root, "EFF_RING.AME")),
      jump_dash_effect_data_(load_player_effect(data_root, "EFF_H_ATTACK_03.AME")) {
    state_.object.position = {3904.0f, 2803.0f, -16.0f};
    state_.object.gravity = 0.166015625f;
    state_.object.maximum_fall_speed = 15.0f;
    state_.object.slope_acceleration_start_angle = 0x2000u;
    state_.object.slope_parameters = kNormalSlopeParameters;
    state_.object.move_flags = 0x402e02c1u;
    state_.walking.previous_maximum = 9.0f;
    state_.contact.field = {-6, -12, 6, 13};
    state_.contact.adjustments = {2, 4, 2, 4, 1, 1, 2, 4, 2, 4, 1, 1, 1, 1, 2, 2};
    state_.contact.use_secondary_terrain = true;
    const auto result = advance_flat_idle_player_motion(state_,
        {1.0f, 0.0f, 0.0f}, kContactEnvironment, terrain_, ObjectMovementPrecision::Single);
    if (result != PlayerGroundMotionStatus::Applied) {
        throw std::runtime_error("Original first-act player contact is unsupported.");
    }
    start_sonic_ground_idle(animation_, {}, SonicGroundIdleBehavior::CharacterChain);
    camera_.internal_position = {state_.object.position.x, 6.0f - state_.object.position.y, 50.0f};
}

PlayerGroundMotionStatus NativePlayerScene::advance_ground_motion(
    PlayerWalkDirection direction,
    std::int32_t input_magnitude,
    std::uint16_t jump_buttons,
    bool crouch_held,
    bool up_held) {
    sound_requests_.clear();
    ring_pickups_.clear();
    if ((jump_buttons & ~0x3000u) != 0u) {
        throw std::invalid_argument("Player jump buttons are unsupported.");
    }
    const bool steep = (static_cast<std::uint16_t>(
        state_.object.surface_angle + 0x2000u) & 0xc000u) != 0u;
    if (steep && ((sequence_ != 0u && sequence_ != 1u && sequence_ != 10u && !is_air_sequence(sequence_))
            || (!is_air_sequence(sequence_) && (crouch_held || up_held)))) {
        return last_status_ = PlayerGroundMotionStatus::UnsupportedSurfaceAngle;
    }
    auto next = state_;
    auto rectangles = rectangles_;
    auto animation = animation_;
    auto sequence = sequence_;
    auto jump = jump_;
    auto jump_dash = jump_dash_;
    auto fall_turn = fall_turn_;
    auto turn_angle = turn_angle_;
    auto spindash_power = spindash_power_;
    auto spindash_start_timer = spindash_start_timer_;
    auto spindash_camera_timer = spindash_camera_timer_;
    std::vector<SonicSoundCue> sound_requests;
    bool charged = false;
    bool launched = false;
    bool started_jump = false;
    bool started_jump_dash = false;
    std::optional<PlayerJumpDashEffect> jump_dash_effect;
    bool started_fall = false;
    bool started_crouch = false;
    bool landed = false;
    bool primary_ended = primary_ended_;
    advance_sonic_ground_dash2_timer(animation, 1.0f, CameraPrecision::Single);
    if (next.walking.deceleration_delay > 0.0f) {
        next.walking.deceleration_delay = std::fmax(next.walking.deceleration_delay - 1.0f, 0.0f);
    }
    const auto change_sequence = [&](std::uint32_t value) {
        reset_player_attack_for_sequence(rectangles.attack, 0u);
        if ((jump.player_flags & 0x80000100u) != 0u) {
            if ((jump.player_flags & 0x100u) != 0u) {
                next.contact.flipped_horizontal = !next.contact.flipped_horizontal;
                set_sonic_ground_facing(animation, next.contact.flipped_horizontal);
            }
            jump.player_flags &= 0x7ffffeefu;
            turn_angle = 0u;
        }
        sequence = value;
    };
    const auto start_roll = [&](bool preserve_speed) {
        if (sequence != 0x6du && (jump.player_flags & 0x20000u) == 0u) {
            sound_requests.push_back(SonicSoundCue::Spin);
        }
        change_sequence(10u);
        enable_player_attack(rectangles.attack);
        next.object.move_flags &= ~0x10u;
        if (preserve_speed) next.object.move_flags |= 0x4000u;
        start_sonic_rolling_animation(animation, next.contact.flipped_horizontal);
        primary_ended = false;
    };
    const auto start_charge = [&] {
        const bool continuing = sequence == 11u || sequence == 12u;
        const bool charge_action = animation.action == SonicGroundAction::ChargeStart
            || animation.action == SonicGroundAction::Charge
            || animation.action == SonicGroundAction::ChargeHold;
        spindash_start_timer = charge_action ? 0.0f : 17.0f;
        spindash_power = initialize_ordinary_spindash_power(spindash_power, continuing,
            1.0f, ObjectSpeedPrecision::Single);
        next.object.velocity = {0.0f, 0.0f, 0.0f};
        next.object.ground_speed = 0.0f;
        next.object.move_flags &= ~0x10u;
        change_sequence(11u);
        enable_player_attack(rectangles.attack);
        resize_player_spindash_attack(rectangles.attack, jump.player_flags);
        start_sonic_spindash_animation(animation, spindash_start_timer > 0.0f
            ? SonicGroundAction::ChargeStart : SonicGroundAction::Charge,
            next.contact.flipped_horizontal);
        primary_ended = false;
        charged = true;
        sound_requests.push_back(SonicSoundCue::Dash1);
        sound_requests.push_back(SonicSoundCue::Dash2);
    };
    if (!is_air_sequence(sequence) && check_ordinary_player_fall(
            next.object, jump, 1.0f, ObjectMovementPrecision::Single)) {
        const bool rolling = sequence == 10u;
        if ((next.object.move_flags & 1u) != 0u) {
            next.object.slope_acceleration_start_angle = 0x2000u;
            next.object.slope_parameters = kNormalSlopeParameters;
        }
        jump.working_maximum = next.walking.working_maximum;
        initialize_ordinary_seq16_fall(next.object, jump, ObjectMovementPrecision::Single);
        const bool rotated = static_cast<std::uint16_t>(next.object.surface_angle - 0x2000u) <= 0xc000u;
        if (rolling) {
            start_sonic_jump_animation(animation, next.contact.flipped_horizontal);
            animation.primary_speed = 1.0f;
            animation.secondary_speed = 1.0f;
        } else {
            start_sonic_fall_animation(animation, next.contact.flipped_horizontal, rotated);
        }
        change_sequence(16u);
        if (rolling) enable_player_attack(rectangles.attack);
        primary_ended = false;
        started_fall = true;
    }
    if (sequence == 16u || sequence == 17u) {
        const bool reverse_facing = next.contact.flipped_horizontal
            ? direction == PlayerWalkDirection::Right : direction == PlayerWalkDirection::Left;
        if (!started_fall && reverse_facing
                && ((next.object.move_flags & 0x10u) != 0u || std::fabs(next.object.ground_speed) < 4.0f)) {
            if (animation.action == SonicGroundAction::Jump) {
                next.contact.flipped_horizontal = !next.contact.flipped_horizontal;
                set_sonic_ground_facing(animation, next.contact.flipped_horizontal);
                jump.player_flags = (jump.player_flags & 0x7ffffeefu) | 0x10u;
                turn_angle = 0x8000u;
            } else {
                const auto action = begin_ordinary_player_fall_turn(fall_turn, jump,
                    static_cast<std::uint32_t>(animation.action), next.contact.flipped_horizontal);
                start_sonic_fall_turn_animation(animation, next.contact.flipped_horizontal,
                    action == 43u, fall_turn.frame);
                primary_ended = false;
            }
        }
    }
    if (sequence == 7u && !crouch_held) {
        change_sequence(8u);
        next.object.move_flags &= ~0x10u;
        start_sonic_crouch_animation(animation, SonicGroundAction::CrouchEnd, next.contact.flipped_horizontal);
        primary_ended = false;
    }
    if (sequence == 10u) {
        const bool slope_active = (next.object.move_flags & 0x40u) != 0u
            && static_cast<std::uint16_t>(next.object.surface_angle
                + next.object.slope_acceleration_start_angle)
                >= static_cast<std::uint32_t>(next.object.slope_acceleration_start_angle) * 2u;
        const float slope_delta = slope_active
            ? object_slope_speed_delta(next.object, ObjectMovementPrecision::Single) : 0.0f;
        const float stop_speed = next.object.ground_speed + slope_delta;
        const float stop_threshold = slope_active ? 0.2f : 0.5f;
        if (std::fabs(stop_speed) < stop_threshold) {
            next.object.ground_speed = 0.0f;
            next.object.slope_acceleration_start_angle = 0x2000u;
            next.object.slope_parameters = kNormalSlopeParameters;
            change_sequence(0u);
            start_sonic_ground_idle(animation, {0u, 0u, SonicGroundAnimationSequence::Grounded,
                0.0f, static_cast<std::int16_t>(next.object.surface_angle),
                next.contact.flipped_horizontal}, SonicGroundIdleBehavior::CharacterChain);
            primary_ended = false;
        }
    }
    const bool jump_pushed = (jump_buttons & ~previous_jump_buttons_) != 0u;
    if ((sequence == 16u || sequence == 17u)
            && can_start_ordinary_player_jump_dash(jump, jump_pushed, 0.0f, false, false)) {
        change_sequence(21u);
        initialize_ordinary_seq21_jump_dash(next.object, jump, jump_dash,
            next.walking.deceleration_delay, next.contact.flipped_horizontal, ObjectMovementPrecision::Single);
        start_sonic_jump_animation(animation, next.contact.flipped_horizontal);
        enable_player_attack(rectangles.attack);
        primary_ended = false;
        started_jump_dash = true;
        jump_dash_effect = make_player_jump_dash_effect(
            {next.object.position.x, next.object.position.y, next.object.position.z, 1.0f},
            {next.object.velocity.x, next.object.velocity.y, next.object.velocity.z, 1.0f},
            CameraPrecision::Single);
    }
    bool input_transition = false;
    if ((sequence == 0u || sequence == 1u) && crouch_held
            && std::fabs(next.object.ground_speed) > 0.5f) {
        start_roll(false);
        input_transition = true;
    } else if ((sequence == 6u || sequence == 7u || sequence == 11u || sequence == 12u)
            && jump_pushed) {
        start_charge();
        input_transition = true;
    }
    if (!input_transition && !is_air_sequence(sequence) && sequence != 6u && sequence != 7u
            && sequence != 11u && sequence != 12u && jump_pushed) {
        jump.working_maximum = next.walking.working_maximum;
        initialize_ordinary_seq17_jump(next.object, jump, ObjectMovementPrecision::Single);
        start_sonic_jump_animation(animation, next.contact.flipped_horizontal);
        change_sequence(17u);
        started_jump = true;
        enable_player_attack(rectangles.attack);
        primary_ended = false;
        sound_requests.push_back(SonicSoundCue::Jump);
    }
    if ((sequence == 0u || sequence == 1u || sequence == 8u) && crouch_held
            && std::fabs(next.object.ground_speed) <= 0.5f
            && ((sequence == 1u && next.object.ground_speed != 0.0f)
                || (next.object.ground_speed == 0.0f && direction == PlayerWalkDirection::None))) {
        change_sequence(7u);
        next.object.move_flags &= ~0x10u;
        next.object.ground_speed = 0.0f;
        start_sonic_crouch_animation(animation, SonicGroundAction::Crouch, next.contact.flipped_horizontal);
        primary_ended = false;
        started_crouch = true;
    }
    if (sequence == 8u && (next.object.ground_speed != 0.0f || direction != PlayerWalkDirection::None)) {
        change_sequence(1u);
        start_sonic_ground_walk(animation, {0u, 0u, SonicGroundAnimationSequence::Grounded,
            next.object.ground_speed, static_cast<std::int16_t>(next.object.surface_angle),
            next.contact.flipped_horizontal});
        primary_ended = false;
    }
    if (sequence == 6u && primary_ended) {
        change_sequence(7u);
        next.object.move_flags &= ~0x10u;
        if (std::fabs(next.object.ground_speed) < 4096.0f) next.object.ground_speed = 0.0f;
        start_sonic_crouch_animation(animation, SonicGroundAction::Crouch, next.contact.flipped_horizontal);
        primary_ended = false;
    }
    if (sequence == 7u && next.object.ground_speed != 0.0f) {
        start_roll(true);
    }
    if (sequence == 8u && primary_ended) {
        change_sequence(0u);
        start_sonic_ground_idle(animation, {0u, 0u, SonicGroundAnimationSequence::Grounded,
            next.object.ground_speed, static_cast<std::int16_t>(next.object.surface_angle),
            next.contact.flipped_horizontal}, SonicGroundIdleBehavior::CharacterChain);
        primary_ended = false;
    }
    if (sequence == 11u || sequence == 12u) {
        spindash_start_timer = std::fmax(spindash_start_timer - 1.0f, 0.0f);
        const bool startup_ended = animation.action == SonicGroundAction::ChargeStart
            && spindash_start_timer == 0.0f;
        if (startup_ended || (animation.action == SonicGroundAction::Charge && primary_ended)) {
            change_sequence(12u);
            enable_player_attack(rectangles.attack);
            start_sonic_spindash_animation(animation, SonicGroundAction::ChargeHold,
                next.contact.flipped_horizontal);
            primary_ended = false;
        } else if (!crouch_held || next.object.ground_speed != 0.0f) {
            next.walking.deceleration_delay = 1.0f;
            spindash_camera_timer = 8.0f;
            next.object.ground_speed = ordinary_spindash_release_speed(spindash_power,
                next.object.ground_speed, next.contact.flipped_horizontal);
            start_roll(true);
            launched = true;
        } else {
            spindash_power = decay_ordinary_spindash_power(spindash_power,
                1.0f, ObjectSpeedPrecision::Single);
            if (up_held) {
                change_sequence(0u);
                start_sonic_ground_idle(animation, {0u, 0u, SonicGroundAnimationSequence::Grounded,
                    0.0f, static_cast<std::int16_t>(next.object.surface_angle),
                    next.contact.flipped_horizontal}, SonicGroundIdleBehavior::CharacterChain);
                primary_ended = false;
            }
        }
    }
    if (is_air_sequence(sequence)) {
        if (sequence == 21u) {
            const auto transition = advance_ordinary_player_jump_dash(
                next.object, jump, jump_dash, ObjectMovementPrecision::Single);
            if (transition == PlayerJumpDashTransition::Fall) {
                change_sequence(16u);
                start_sonic_fall_animation(animation, next.contact.flipped_horizontal, false);
                primary_ended = false;
            }
        } else {
            update_ordinary_player_jump_release(next.object, jump, jump_buttons != 0u,
                ObjectMovementPrecision::Single);
        }
        if ((next.object.move_flags & 1u) != 0u) {
            if ((next.contact.collision_flags & 0x31u) != 0u) {
                next.object.surface_angle = 0u;
            }
            apply_ordinary_player_landing(next.object, jump, ObjectMovementPrecision::Single);
            next.walking.working_maximum = jump.working_maximum;
            next.object.slope_acceleration_start_angle = 0x2000u;
            next.object.slope_parameters = kNormalSlopeParameters;
            change_sequence(next.object.ground_speed != 0.0f ? 1u : 0u);
            SonicGroundAnimationSelectionInput landing_selection;
            landing_selection.ground_speed = next.object.ground_speed;
            landing_selection.ground_angle = static_cast<std::int16_t>(next.object.surface_angle);
            landing_selection.facing_left = next.contact.flipped_horizontal;
            if (next.object.ground_speed != 0.0f) {
                start_sonic_ground_walk(animation, landing_selection);
            } else {
                start_sonic_ground_idle(animation, landing_selection,
                    SonicGroundIdleBehavior::CharacterChain);
            }
            primary_ended = false;
            jump_dash.timer = 0u;
            landed = true;
        }
    }
    SonicGroundAnimationSelectionInput selection;
    selection.ground_speed = next.object.ground_speed;
    selection.ground_angle = static_cast<std::int16_t>(next.object.surface_angle);
    selection.facing_left = next.contact.flipped_horizontal;
    selection.primary_ended = primary_ended;
    if ((sequence == 0u || sequence == 1u) && !landed) {
        bool walking = sequence == 1u;
        if (walking && next.object.ground_speed == 0.0f && next.object.velocity.z == 0.0f) {
            walking = false;
            next.object.move_flags &= ~0x10u;
            start_sonic_ground_idle(animation, selection, SonicGroundIdleBehavior::CharacterChain);
            selection.primary_ended = false;
        } else if (!walking && (next.object.ground_speed != 0.0f || direction != PlayerWalkDirection::None)) {
            walking = true;
            start_sonic_ground_walk(animation, selection);
            selection.primary_ended = false;
        }
        if (walking) {
            const bool turn_left = next.object.ground_speed < 0.0f
                && direction == PlayerWalkDirection::Left && !selection.facing_left;
            const bool turn_right = next.object.ground_speed > 0.0f
                && direction == PlayerWalkDirection::Right && selection.facing_left;
            if (turn_left || turn_right) {
                selection.facing_left = !selection.facing_left;
                next.contact.flipped_horizontal = selection.facing_left;
                set_sonic_ground_facing(animation, selection.facing_left);
                turn_angle = 0x8000u;
            }
            update_sonic_ground_walk(animation, selection);
        } else {
            const auto previous_action = animation.action;
            update_sonic_ground_idle(animation, selection);
            if (previous_action == SonicGroundAction::Idle && animation.action != SonicGroundAction::Idle
                    && selection.facing_left) {
                selection.facing_left = false;
                next.contact.flipped_horizontal = false;
                set_sonic_ground_facing(animation, false);
                turn_angle = 0x8000u;
            }
        }
        const auto ground_sequence = walking ? 1u : 0u;
        if (sequence != ground_sequence) change_sequence(ground_sequence);
    }
    if (sequence == 0u || sequence == 1u) {
        set_sonic_ground_playback_speed(animation, next.object.ground_speed, CameraPrecision::Single);
    } else if (sequence == 16u || sequence == 21u) {
        animation.primary_speed = 1.0f;
        animation.secondary_speed = 1.0f;
    } else if (sequence == 17u) {
        set_sonic_jump_playback_speed(animation, next.object.velocity.x,
            next.object.velocity.y, CameraPrecision::Single);
    } else if (sequence == 10u) {
        set_sonic_ground_playback_speed(animation, next.object.ground_speed, CameraPrecision::Single);
    }
    if ((jump.player_flags & 0x80000000u) != 0u) {
        if (advance_ordinary_player_fall_turn(fall_turn, jump)) {
            start_sonic_fall_animation(animation, next.contact.flipped_horizontal, fall_turn.saved_action == 42u);
            primary_ended = false;
        }
        turn_angle = fall_turn.angle;
    } else if (turn_angle != 0u) {
        const int advanced = static_cast<int>(turn_angle) + (selection.facing_left ? -0x1000 : 0x1000);
        turn_angle = static_cast<std::uint16_t>(advanced <= 0 || advanced >= 0x10000 ? 0 : advanced);
        if (turn_angle == 0u) jump.player_flags &= ~0x10u;
    }
    if (is_air_sequence(sequence)) {
        last_status_ = advance_flat_air_player_motion(next, kAirParameters, direction,
            {1.0f, 0.0f, 0.0f}, kContactEnvironment, terrain_, ObjectMovementPrecision::Single);
    } else if (sequence == 1u) {
        last_status_ = advance_flat_player_motion(next, kNormalParameters, direction, input_magnitude,
            {1.0f, 0.0f, 0.0f}, kContactEnvironment, terrain_, ObjectMovementPrecision::Single);
    } else {
        if (sequence == 10u) {
            if (next.walking.deceleration_delay == 0.0f) {
                next.object.slope_acceleration_start_angle = 0x1000u;
                next.object.slope_parameters = ObjectSlopeParameters{
                    0.15625f, 0.078125f, kNormalParameters.slope_speed_cap};
            } else {
                next.object.slope_parameters = ObjectSlopeParameters{
                    0.0f, 0.0f, kNormalParameters.slope_speed_cap};
            }
            next.object.ground_speed = advance_ordinary_rolling_speed(next.object.ground_speed,
                next.walking.deceleration_delay, direction, 1.0f, ObjectSpeedPrecision::Single);
        }
        last_status_ = advance_flat_idle_player_motion(next, {1.0f, 0.0f, 0.0f},
            kContactEnvironment, terrain_, ObjectMovementPrecision::Single);
    }
    if (last_status_ == PlayerGroundMotionStatus::Applied) {
        auto taken = rings_taken_;
        auto counters = ring_counters_;
        auto sound_right = ring_sound_right_;
        std::uint16_t ring_sound_count = 0u;
        std::vector<StageRingPlacement> ring_draws;
        std::vector<StageRingPickupEvent> pickups;
        ring_draws.reserve(stage_rings_.rings.size());
        pickups.reserve(stage_rings_.rings.size());
        const auto& bounds = rectangles.interaction.bounds;
        const RingPickupCandidate candidate{true, jump.player_flags, 0u,
            {next.object.position.x, next.object.position.y,
             bounds.left, bounds.top, bounds.right, bounds.bottom}};
        const bool pickup_enabled = damage_ring_pickup_enabled({0u, 0u, true, jump.player_flags});
        for (std::size_t index = 0u; index < stage_rings_.rings.size(); ++index) {
            if (taken[index] != 0u) continue;
            const auto& position = stage_rings_.rings[index];
            ring_draws.push_back(position);
            if (find_static_ring_pickup({static_cast<float>(position.x),
                    static_cast<float>(position.y), 0u, 0u, pickup_enabled}, &candidate, 1u) < 0) continue;
            const auto awards = add_player_rings(counters, 1, 0u, false);
            const bool request_sound = ring_sound_count < 2u;
            pickups.push_back({index, position, request_sound, sound_right, awards});
            if (request_sound) {
                sound_right = !sound_right;
                ++ring_sound_count;
            }
            taken[index] = 1u;
        }
        auto camera = camera_;
        update_normal_camera_follow(camera,
            {next.object.position.x, next.object.position.y, next.object.position.z},
            (next.object.move_flags & 0x10u) != 0u, 1.0f, CameraPrecision::Single);
        advance_sonic_ground_blend(animation, CameraPrecision::Single);
        primary_ended_ = advance_sonic_ground_frames(animation, 1.0f,
            CameraPrecision::Single).primary_ended;
        state_ = next;
        rectangles_ = rectangles;
        rings_taken_ = std::move(taken);
        ring_counters_ = counters;
        ring_sound_right_ = sound_right;
        ring_rotation_ = static_cast<std::uint16_t>(ring_rotation_ + 0x2d8u);
        ring_draw_positions_ = std::move(ring_draws);
        ring_pickups_ = std::move(pickups);
        collected_rings_ += ring_pickups_.size();
        for (const auto& pickup : ring_pickups_) extra_life_requests_ += pickup.extra_lives;
        animation_ = animation;
        camera_ = camera;
        sequence_ = sequence;
        jump_ = jump;
        jump_dash_ = jump_dash;
        fall_turn_ = fall_turn;
        turn_angle_ = turn_angle;
        spindash_power_ = spindash_power;
        spindash_start_timer_ = spindash_start_timer;
        spindash_camera_timer_ = spindash_camera_timer;
        sound_requests_ = std::move(sound_requests);
        previous_jump_buttons_ = jump_buttons;
        ++ticks_;
        if (started_jump) ++jumps_;
        if (started_jump_dash) ++jump_dashes_;
        if (started_fall) ++falls_;
        if (started_crouch) ++crouches_;
        if (charged) ++spindash_charges_;
        if (launched) ++spindash_launches_;
        if (landed) ++landings_;
        if (direction == PlayerWalkDirection::Left) ++left_input_ticks_;
        if (direction == PlayerWalkDirection::Right) ++right_input_ticks_;
        advance_effects(jump_dash_effect);
    }
    return last_status_;
}

const PlayerGroundMotionState& NativePlayerScene::state() const noexcept {
    return state_;
}

const SonicGroundAnimationState& NativePlayerScene::animation() const noexcept {
    return animation_;
}

const PlayerRectangles& NativePlayerScene::rectangles() const noexcept {
    return rectangles_;
}

const std::vector<StageRingPlacement>& NativePlayerScene::ring_draw_positions() const noexcept {
    return ring_draw_positions_;
}

std::vector<CameraMatrix> NativePlayerScene::ring_world_matrices(std::int32_t camera_roll) const {
    CameraMatrix orientation{{
        1.0f, 0.0f, 0.0f, 0.0f,
        0.0f, 1.0f, 0.0f, 0.0f,
        0.0f, 0.0f, 1.0f, 0.0f,
        0.0f, 0.0f, kRingDepth, 1.0f}};
    rotate_z(orientation, static_cast<std::uint16_t>(camera_roll));
    rotate_y(orientation, ring_rotation_);
    rotate_y(orientation, 0x4000u);
    for (std::size_t column = 0u; column < 3u; ++column) {
        for (std::size_t row = 0u; row < 3u; ++row) orientation[column * 4u + row] *= kDrawScale;
    }
    std::vector<CameraMatrix> result;
    result.reserve(ring_draw_positions_.size());
    for (const auto& position : ring_draw_positions_) {
        auto matrix = orientation;
        matrix[12u] = static_cast<float>(position.x);
        matrix[13u] = -static_cast<float>(position.y);
        result.push_back(matrix);
    }
    return result;
}

const std::vector<StageRingPickupEvent>& NativePlayerScene::ring_pickups() const noexcept {
    return ring_pickups_;
}

const std::vector<AmeRuntimeSprite>& NativePlayerScene::ring_effect_sprites() const noexcept {
    return ring_effect_sprites_;
}

const std::vector<NativePlayerEffectFrame>& NativePlayerScene::effects() const noexcept {
    return effect_frames_;
}

void NativePlayerScene::advance_effects(const std::optional<PlayerJumpDashEffect>& jump_dash_effect) {
    if (jump_dash_effect.has_value()) {
        AmeRuntime runtime(jump_dash_effect_data_, {}, CameraPrecision::Single);
        runtime.set_transform(jump_dash_effect->transform, effect_matrix_backend_);
        effects_.push_back({NativePlayerEffectKind::JumpDash, next_effect_id_++, std::move(runtime)});
    }
    for (const auto& pickup : ring_pickups_) {
        AmeRuntimeInput input;
        input.position = {static_cast<float>(pickup.position.x),
                          -static_cast<float>(pickup.position.y), 24.0f, 1.0f};
        effects_.push_back({NativePlayerEffectKind::Ring, next_effect_id_++,
            AmeRuntime(ring_effect_data_, input, CameraPrecision::Single)});
    }
    ring_effect_sprites_.clear();
    effect_frames_.clear();
    for (auto& effect : effects_) {
        effect.runtime.advance(effect_random_state_, 1.0f, 1.0f / 60.0f);
        if (effect.runtime.lifecycle().complete) {
            if (effect.kind == NativePlayerEffectKind::Ring) ++completed_ring_effects_;
            else ++completed_jump_dash_effects_;
            continue;
        }
        const auto& sprites = effect.runtime.active_sprites();
        const auto& lines = effect.runtime.active_lines();
        if (effect.kind == NativePlayerEffectKind::Ring) {
            ring_effect_sprites_.insert(ring_effect_sprites_.end(), sprites.begin(), sprites.end());
        }
        effect_frames_.push_back({effect.kind, effect.id, sprites, lines});
    }
    const auto completed = std::remove_if(effects_.begin(), effects_.end(),
        [](const Effect& effect) { return effect.runtime.lifecycle().complete; });
    effects_.erase(completed, effects_.end());
}

const std::vector<SonicSoundCue>& NativePlayerScene::sound_requests() const noexcept {
    return sound_requests_;
}

CameraMatrix NativePlayerScene::world_matrix() const {
    CameraMatrix matrix{{
        1.0f, 0.0f, 0.0f, 0.0f,
        0.0f, 1.0f, 0.0f, 0.0f,
        0.0f, 0.0f, 1.0f, 0.0f,
        state_.object.position.x, -state_.object.position.y, state_.object.position.z, 1.0f}};
    rotate_z(matrix, static_cast<std::uint16_t>(0u - state_.object.surface_angle));
    rotate_y(matrix, turn_angle_);
    rotate_y(matrix, state_.contact.flipped_horizontal ? 0xc000u : 0x4000u);
    for (std::size_t column = 0u; column < 3u; ++column) {
        for (std::size_t row = 0u; row < 3u; ++row) matrix[column * 4u + row] *= kDrawScale;
    }
    const float local_offset = -15.0f / kDrawScale;
    for (std::size_t row = 0u; row < 3u; ++row) {
        const float translated = matrix[4u + row] * local_offset;
        matrix[12u + row] += translated;
    }
    return matrix;
}

CameraViewInput NativePlayerScene::initial_camera(float aspect) const {
    if (!std::isfinite(aspect) || aspect <= 0.0f) {
        throw std::invalid_argument("Player scene aspect ratio is invalid.");
    }
    return {0x0e0f, aspect, 1.0f, 60000.0f,
        {3904.0f, -2746.419921875f, 601.7836303710938f},
        {3904.0f, -2750.419921875f, 551.7836303710938f}, std::nullopt, 0};
}

CameraViewInput NativePlayerScene::camera_view(std::int32_t width, std::int32_t height) const {
    return make_normal_camera_view(camera_.internal_position, width, height,
        {512, 512, 32128, 3968}, CameraPrecision::Single);
}

std::string NativePlayerScene::report_json() const {
    std::ostringstream output;
    output << std::setprecision(9)
        << "{\"ticks\":" << ticks_ << ",\"status\":" << static_cast<unsigned>(last_status_)
        << ",\"precision_bits\":24,\"position\":[" << state_.object.position.x << ','
        << state_.object.position.y << ',' << state_.object.position.z << ']'
        << ",\"ground_speed\":" << state_.object.ground_speed
        << ",\"input_ticks\":{\"left\":" << left_input_ticks_ << ",\"right\":" << right_input_ticks_ << '}'
        << ",\"surface_angle\":" << state_.object.surface_angle
        << ",\"move_flags\":" << state_.object.move_flags
        << ",\"facing_left\":" << (state_.contact.flipped_horizontal ? "true" : "false")
        << ",\"sequence\":" << sequence_
        << ",\"jumps\":" << jumps_
        << ",\"jump_dashes\":" << jump_dashes_
        << ",\"jump_dash_timer\":" << jump_dash_.timer
        << ",\"falls\":" << falls_ << ",\"landings\":" << landings_
        << ",\"crouches\":" << crouches_
        << ",\"spindash_charges\":" << spindash_charges_
        << ",\"spindash_launches\":" << spindash_launches_
        << ",\"spindash_power\":" << spindash_power_
        << ",\"spindash_start_timer\":" << spindash_start_timer_
        << ",\"spindash_camera_timer\":" << spindash_camera_timer_
        << ",\"player_flags\":" << jump_.player_flags
        << ",\"fall_timer\":" << jump_.fall_timer
        << ",\"velocity\":[" << state_.object.velocity.x << ',' << state_.object.velocity.y
        << ',' << state_.object.velocity.z << ']'
        << ",\"action\":" << static_cast<unsigned>(animation_.action)
        << ",\"motion\":\"" << sonic_ground_motion_name(animation_.primary.motion) << '\"'
        << ",\"motion_frame\":" << sonic_ground_motion_sample_frame(animation_.primary, CameraPrecision::Single)
        << ",\"blend_weight\":" << animation_.blend_weight
        << ",\"turn_angle\":" << turn_angle_
        << ",\"camera\":\"ordinary_follow\",\"host_tick_rate\":60"
        << ",\"camera_internal_position\":[" << camera_.internal_position[0u] << ','
        << camera_.internal_position[1u] << ',' << camera_.internal_position[2u] << ']'
        << ",\"camera_speed\":[" << camera_.speed[0u] << ',' << camera_.speed[1u] << ','
        << camera_.speed[2u] << ']'
        << ",\"camera_allowance\":[" << camera_.allowance[0u] << ',' << camera_.allowance[1u]
        << ',' << camera_.allowance[2u] << ']'
        << ",\"full_scheduler_accepted\":false"
        << ",\"world_matrix\":[";
    const auto matrix = world_matrix();
    for (std::size_t index = 0u; index < matrix.size(); ++index) {
        if (index != 0u) output << ',';
        output << matrix[index];
    }
    output << "],\"sound_requests\":[";
    for (std::size_t index = 0u; index < sound_requests_.size(); ++index) {
        if (index != 0u) output << ',';
        output << '\"' << sonic_sound_cue_name(sound_requests_[index]) << '\"';
    }
    output << "],\"combat_rectangles\":{\"defense\":";
    write_rectangle(output, rectangles_.defense);
    output << ",\"attack\":";
    write_rectangle(output, rectangles_.attack);
    output << ",\"interaction\":";
    write_rectangle(output, rectangles_.interaction);
    output << "},\"stage_rings\":{\"loaded\":" << stage_rings_.rings.size()
        << ",\"collected\":" << collected_rings_
        << ",\"remaining\":" << stage_rings_.rings.size() - collected_rings_
        << ",\"carried\":" << ring_counters_.carried
        << ",\"total\":" << ring_counters_.total
        << ",\"next_extra_life\":" << ring_counters_.next_extra_life
        << ",\"extra_life_requests\":" << extra_life_requests_
        << ",\"draw_count\":" << ring_draw_positions_.size()
        << ",\"rotation\":" << ring_rotation_
        << ",\"draw_depth\":" << kRingDepth << ",\"draw_scale\":" << kDrawScale
        << ",\"pickups\":[";
    for (std::size_t index = 0u; index < ring_pickups_.size(); ++index) {
        if (index != 0u) output << ',';
        const auto& pickup = ring_pickups_[index];
        output << "{\"index\":" << pickup.index << ",\"position\":["
            << pickup.position.x << ',' << pickup.position.y << "],\"sound\":";
        if (pickup.sound_requested) output << '\"' << (pickup.sound_right ? "Ring1R" : "Ring1L") << '\"';
        else output << "null";
        output << ",\"extra_lives\":" << pickup.extra_lives << '}';
    }
    output << "],\"effects\":{\"active\":" << std::count_if(effects_.begin(), effects_.end(),
            [](const Effect& effect) { return effect.kind == NativePlayerEffectKind::Ring; })
        << ",\"completed\":" << completed_ring_effects_
        << ",\"sprites\":" << ring_effect_sprites_.size()
        << ",\"random_seed\":1,\"random_state\":" << effect_random_state_
        << ",\"global_random_history_accepted\":false},"
        << "\"renderer_connected\":true,\"audio_connected\":true,"
        << "\"pickup_effect_connected\":true,\"life_awards_connected\":false,"
        << "\"full_ring_scheduler_accepted\":false},\"object_hit_dispatch_accepted\":false,"
        << "\"jump_dash_effects\":{\"created\":" << jump_dashes_
        << ",\"completed\":" << completed_jump_dash_effects_
        << ",\"active\":" << std::count_if(effects_.begin(), effects_.end(),
            [](const Effect& effect) { return effect.kind == NativePlayerEffectKind::JumpDash; })
        << ",\"shared_random_state\":" << effect_random_state_
        << ",\"renderer_connected\":true},"
        << "\"complete_player_accepted\":false}";
    return output.str();
}

}
