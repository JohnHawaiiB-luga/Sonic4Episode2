#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <stdexcept>
#include <variant>
#include <vector>

struct AmeEffectVector4 {
    float x;
    float y;
    float z;
    float w;
};

struct AmeEffectQuaternion {
    float x;
    float y;
    float z;
    float w;
};

struct AmeEffectTextureAnimationKey {
    float time;
    float left;
    float top;
    float right;
    float bottom;
};

struct AmeEffectOmniNode {
    AmeEffectVector4 translation;
    AmeEffectQuaternion rotation;
    float inheritance_rate;
    float life;
    float start_time;
    float offset;
    float offset_chaos;
    float speed;
    float speed_chaos;
    float max_count;
    float frequency;
};

struct AmeEffectSpriteNode {
    AmeEffectVector4 translation;
    AmeEffectQuaternion rotation;
    float z_bias;
    float inheritance_rate;
    float life;
    float start_time;
    float size;
    float size_chaos;
    float scale_x_start;
    float scale_x_end;
    float scale_y_start;
    float scale_y_end;
    float twist_angle;
    float twist_angle_chaos;
    float twist_angle_speed;
    std::uint32_t color_start;
    std::uint32_t color_end;
    std::int32_t blend;
    std::int16_t texture_slot;
    std::int16_t texture_id;
    float crop_left;
    float crop_top;
    float crop_right;
    float crop_bottom;
    float scroll_u;
    float scroll_v;
    float texture_animation_time;
    std::int32_t texture_key_count;
    std::vector<AmeEffectTextureAnimationKey> texture_keys;
};

struct AmeEffectCircleNode {
    AmeEffectOmniNode emitter;
    float spread;
    float spread_variation;
    float radius;
    float radius_variation;
};

struct AmeEffectLineNode {
    AmeEffectVector4 translation;
    AmeEffectQuaternion rotation;
    float z_bias;
    float inheritance_rate;
    float life;
    float start_time;
    float length_start;
    float length_end;
    float inside_width_start;
    float inside_width_end;
    float outside_width_start;
    float outside_width_end;
    std::uint32_t inside_color_start;
    std::uint32_t inside_color_end;
    std::uint32_t outside_color_start;
    std::uint32_t outside_color_end;
    std::int32_t blend;
    std::int16_t texture_slot;
    std::int16_t texture_id;
    float crop_left;
    float crop_top;
    float crop_right;
    float crop_bottom;
    float scroll_u;
    float scroll_v;
    float texture_animation_time;
    std::int32_t texture_key_count;
    std::vector<AmeEffectTextureAnimationKey> texture_keys;
};

struct AmeEffectRadialNode {
    AmeEffectVector4 position;
    float magnitude;
    float attenuation;
};

using AmeEffectNodePayload = std::variant<
    AmeEffectOmniNode,
    AmeEffectSpriteNode,
    AmeEffectCircleNode,
    AmeEffectLineNode,
    AmeEffectRadialNode>;

struct AmeEffectNode {
    std::int16_t id;
    std::uint16_t type;
    std::uint32_t flags;
    std::array<std::uint8_t, 12u> name_bytes;
    std::optional<std::size_t> child_index;
    std::optional<std::size_t> sibling_index;
    std::optional<std::size_t> parent_index;
    AmeEffectNodePayload payload;
};

struct AmeEffectHeader {
    std::array<std::uint8_t, 4u> file_id;
    std::uint32_t file_version;
    std::int32_t declared_node_count;
    AmeEffectVector4 bounding_center;
    float bounding_radius;
    float bounding_radius_squared;
    std::array<float, 2u> bounding_reserved;
    std::array<std::uint32_t, 4u> reserved_words;
    std::optional<std::size_t> root_node_index;
};

struct AmeEffectData {
    AmeEffectHeader header;
    std::vector<AmeEffectNode> nodes;
};

class AmeEffectError : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

AmeEffectData parse_pc_ame_effect(const std::uint8_t* data, std::size_t size);
