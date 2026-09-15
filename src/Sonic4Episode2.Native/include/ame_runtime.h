#pragma once

#include "ame_effect.h"
#include "camera_math.h"
#include "camera_matrix.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <stdexcept>
#include <vector>

struct AmeRuntimeInput {
    AmeEffectVector4 position{0.0f, 0.0f, 0.0f, 1.0f};
    AmeEffectVector4 velocity{0.0f, 0.0f, 0.0f, 1.0f};
    AmeEffectVector4 parent_position{0.0f, 0.0f, 0.0f, 1.0f};
    AmeEffectVector4 parent_velocity{0.0f, 0.0f, 0.0f, 1.0f};
    std::int32_t transparency{256};
    float size_rate{1.0f};
};

struct AmeRuntimeTransform {
    AmeEffectVector4 position{0.0f, 0.0f, 0.0f, 1.0f};
    AmeEffectQuaternion rotation{0.0f, 0.0f, 0.0f, 1.0f};
};

struct AmeRuntimeSprite {
    std::size_t node_index;
    std::uint16_t type;
    std::uint32_t node_flags;
    AmeEffectVector4 position;
    AmeEffectVector4 velocity;
    float time;
    float size_x;
    float size_y;
    float base_size;
    float twist;
    float twist_speed;
    std::uint32_t runtime_flags;
    std::uint32_t color;
    float texture_time;
    std::size_t texture_key;
    float texture_left;
    float texture_top;
    float texture_right;
    float texture_bottom;
    std::int16_t texture_slot;
    std::int16_t texture_id;
    std::int32_t blend;
    float z_bias;
};

struct AmeRuntimeNodeState {
    std::size_t node_index;
    std::uint32_t state;
    float amount;
    std::uint32_t emitted_count;
    std::size_t waiting_count;
    std::size_t active_count;
};

struct AmeRuntimeLine {
    std::size_t node_index;
    std::uint32_t node_flags;
    AmeEffectVector4 position;
    AmeEffectVector4 velocity;
    float time;
    float length;
    float inside_width;
    float outside_width;
    std::uint32_t inside_color;
    std::uint32_t outside_color;
    std::uint32_t runtime_flags;
    float texture_time;
    std::size_t texture_key;
    float texture_left;
    float texture_top;
    float texture_right;
    float texture_bottom;
    std::int16_t texture_slot;
    std::int16_t texture_id;
    std::int32_t blend;
    float z_bias;
};

struct AmeRuntimeLifecycle {
    std::int32_t entry_count;
    std::size_t waiting_count;
    std::size_t active_count;
    bool complete;
};

class AmeRuntimeError : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

class AmeRuntime {
public:
    AmeRuntime(
        const AmeEffectData& effect,
        const AmeRuntimeInput& input = AmeRuntimeInput{},
        CameraPrecision precision = CameraPrecision::Single);
    AmeRuntime(AmeRuntime&&) noexcept;
    AmeRuntime& operator=(AmeRuntime&&) noexcept;
    ~AmeRuntime();

    AmeRuntime(const AmeRuntime&) = delete;
    AmeRuntime& operator=(const AmeRuntime&) = delete;

    void advance(std::uint32_t& crt_rng_state, float unit_frame, float unit_time);
    void set_transform(const AmeRuntimeTransform& transform, const CameraMatrixBackend& backend);

    const std::vector<AmeRuntimeSprite>& active_sprites() const noexcept;
    const std::vector<AmeRuntimeLine>& active_lines() const noexcept;
    const std::vector<AmeRuntimeNodeState>& nodes() const noexcept;
    AmeRuntimeLifecycle lifecycle() const noexcept;

private:
    class Implementation;
    std::unique_ptr<Implementation> implementation_;
};
