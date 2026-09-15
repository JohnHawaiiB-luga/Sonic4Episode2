#pragma once

#include "ame_runtime.h"
#include "camera_matrix_d3dx.h"
#include "native_player_effect.h"
#include "player_jump_dash_effect.h"

#include <optional>
#include "camera_matrix.h"
#include "normal_camera_follow.h"
#include "player_ground_motion.h"
#include "player_jump.h"
#include "player_rectangles.h"
#include "player_ring_counters.h"
#include "sonic_ground_animation.h"
#include "sonic_sound.h"
#include "stage_data.h"

#include <filesystem>
#include <string>
#include <vector>

namespace sonic4ep2::app {

struct StageRingPickupEvent {
    std::size_t index;
    StageRingPlacement position;
    bool sound_requested;
    bool sound_right;
    std::uint16_t extra_lives;
};

class NativePlayerScene final {
public:
    explicit NativePlayerScene(const std::filesystem::path& data_root);

    PlayerGroundMotionStatus advance_ground_motion(
        PlayerWalkDirection direction,
        std::int32_t input_magnitude,
        std::uint16_t jump_buttons = 0u,
        bool crouch_held = false,
        bool up_held = false);
    const PlayerGroundMotionState& state() const noexcept;
    const SonicGroundAnimationState& animation() const noexcept;
    const PlayerRectangles& rectangles() const noexcept;
    const std::vector<StageRingPlacement>& ring_draw_positions() const noexcept;
    std::vector<CameraMatrix> ring_world_matrices(std::int32_t camera_roll) const;
    const std::vector<StageRingPickupEvent>& ring_pickups() const noexcept;
    const std::vector<AmeRuntimeSprite>& ring_effect_sprites() const noexcept;
    const std::vector<NativePlayerEffectFrame>& effects() const noexcept;
    const std::vector<SonicSoundCue>& sound_requests() const noexcept;
    CameraMatrix world_matrix() const;
    CameraViewInput initial_camera(float aspect) const;
    CameraViewInput camera_view(std::int32_t width, std::int32_t height) const;
    std::string report_json() const;

private:
    struct Effect {
        NativePlayerEffectKind kind;
        std::uint64_t id;
        AmeRuntime runtime;
    };

    void advance_effects(const std::optional<PlayerJumpDashEffect>& jump_dash_effect);

    TerrainCollision terrain_;
    PlayerGroundMotionState state_{};
    PlayerRectangles rectangles_ = make_ordinary_player_rectangles();
    StageRingPlacements stage_rings_;
    std::vector<std::uint8_t> rings_taken_;
    std::vector<StageRingPlacement> ring_draw_positions_;
    std::vector<StageRingPickupEvent> ring_pickups_;
    AmeEffectData ring_effect_data_;
    AmeEffectData jump_dash_effect_data_;
    D3dxCameraMatrixBackend effect_matrix_backend_;
    std::vector<Effect> effects_;
    std::vector<NativePlayerEffectFrame> effect_frames_;
    std::uint64_t next_effect_id_ = 0u;
    std::uint64_t completed_jump_dash_effects_ = 0u;
    std::vector<AmeRuntimeSprite> ring_effect_sprites_;
    std::uint32_t effect_random_state_ = 1u;
    std::uint64_t completed_ring_effects_ = 0u;
    PlayerRingCounters ring_counters_;
    std::uint64_t collected_rings_ = 0u;
    std::uint64_t extra_life_requests_ = 0u;
    bool ring_sound_right_ = false;
    std::uint16_t ring_rotation_ = 0u;
    SonicGroundAnimationState animation_{};
    std::vector<SonicSoundCue> sound_requests_;
    NormalCameraFollowState camera_{};
    std::uint32_t sequence_ = 0u;
    PlayerJumpSequenceState jump_{};
    PlayerJumpDashState jump_dash_{};
    PlayerFallTurnState fall_turn_{};
    std::uint16_t previous_jump_buttons_ = 0u;
    std::uint64_t jumps_ = 0u;
    std::uint64_t jump_dashes_ = 0u;
    std::uint64_t falls_ = 0u;
    std::uint64_t crouches_ = 0u;
    std::uint64_t spindash_charges_ = 0u;
    std::uint64_t spindash_launches_ = 0u;
    float spindash_power_ = 0.0f;
    float spindash_start_timer_ = 0.0f;
    float spindash_camera_timer_ = 0.0f;
    std::uint64_t landings_ = 0u;
    bool primary_ended_ = false;
    std::uint16_t turn_angle_ = 0u;
    std::uint64_t ticks_ = 0u;
    std::uint64_t left_input_ticks_ = 0u;
    std::uint64_t right_input_ticks_ = 0u;
    PlayerGroundMotionStatus last_status_ = PlayerGroundMotionStatus::Applied;
};

}
