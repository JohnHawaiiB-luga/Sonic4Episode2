#pragma once

#include "ame_runtime.h"
#include "native_player_effect.h"
#include "camera_matrix.h"
#include "camera_view.h"
#include "d3d9_device.h"
#include "sonic_ground_animation.h"

#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace sonic4ep2::app {

class NativeStageRenderer final {
public:
    NativeStageRenderer(
        d3d9::D3d9Device& device,
        const std::filesystem::path& data_root,
        bool character_inspection = false,
        std::optional<float> character_frame = std::nullopt,
        bool player_scene = false);
    ~NativeStageRenderer();

    NativeStageRenderer(const NativeStageRenderer&) = delete;
    NativeStageRenderer& operator=(const NativeStageRenderer&) = delete;

    void render(
        float horizontal_offset,
        float vertical_offset,
        float distance_factor,
        float delta_seconds = 0.0f);
    void set_player_motion(const std::string& motion_name);
    void set_ring_instances(const std::vector<CameraMatrix>& worlds);
    void set_ring_effect_sprites(const std::vector<AmeRuntimeSprite>& sprites);
    void set_player_effects(const std::vector<NativePlayerEffectFrame>& effects);
    void render_player_scene(
        const CameraMatrix& world,
        const CameraViewInput& camera,
        float motion_frame);
    void render_player_scene(
        const CameraMatrix& world,
        const CameraViewInput& camera,
        const SonicGroundAnimationState& animation);
    std::string report_json() const;

private:
    struct Implementation;
    std::unique_ptr<Implementation> implementation_;
};

}
