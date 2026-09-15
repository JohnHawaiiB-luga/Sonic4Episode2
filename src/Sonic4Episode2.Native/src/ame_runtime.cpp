#include "ame_runtime.h"

#include "nn_trig.h"

#include <cmath>
#include <cstdint>
#include <limits>
#include <optional>
#include <utility>

namespace {

constexpr std::uint16_t kOmniNodeType = 0x0100u;
constexpr std::uint16_t kCircleNodeType = 0x0103u;
constexpr std::uint16_t kSimpleSpriteNodeType = 0x0200u;
constexpr std::uint16_t kSpriteNodeType = 0x0201u;
constexpr std::uint16_t kLineNodeType = 0x0202u;
constexpr std::uint16_t kRadialNodeType = 0x0302u;
constexpr std::uint32_t kRuntimeOwner = 0x2000u;
constexpr std::uint32_t kRuntimeFinished = 0x4000u;
constexpr std::uint32_t kRuntimeDelete = 0x8000u;
constexpr std::uint32_t kTextureFrozen = 0x2u;
constexpr std::uint32_t kTwistReverse = 0x4u;
constexpr std::uint32_t kHorizontalFlip = 0x8u;
constexpr std::uint32_t kVerticalFlip = 0x10u;
constexpr std::uint32_t kRandomTwist = 0x4u;
constexpr std::uint32_t kCropTexture = 0x2000u;
constexpr std::uint32_t kScrollTexture = 0x4000u;
constexpr std::uint32_t kAnimateTexture = 0x8000u;
constexpr std::uint32_t kLoopTexture = 0x10000u;
constexpr std::uint32_t kRandomHorizontalFlip = 0x20000u;
constexpr std::uint32_t kRandomVerticalFlip = 0x40000u;
constexpr std::uint32_t kRandomTextureKey = 0x80000u;
constexpr std::uint32_t kForceHorizontalFlip = 0x100000u;
constexpr std::uint32_t kForceVerticalFlip = 0x200000u;
constexpr std::uint32_t kSpriteDrawFlags = 0x0b003001u;
constexpr std::uint32_t kSupportedSpriteFlags =
    kSpriteDrawFlags |
    kRandomTwist |
    kScrollTexture |
    kAnimateTexture |
    kLoopTexture |
    kRandomHorizontalFlip |
    kRandomVerticalFlip |
    kRandomTextureKey |
    kForceHorizontalFlip |
    kForceVerticalFlip;
constexpr float kInputLimit = 16777216.0f;
constexpr double kRandomScale = 0x1.0p-15;

[[noreturn]] void fail(const char* message) {
    throw AmeRuntimeError(message);
}

void require_supported_precision(CameraPrecision precision) {
    if (precision != CameraPrecision::Single && precision != CameraPrecision::Double) {
        fail("AME runtime precision is unsupported");
    }
}

void require_finite_bounded(float value, const char* message) {
    if (!std::isfinite(value) || std::fabs(value) > kInputLimit) {
        fail(message);
    }
}

void require_vector(const AmeEffectVector4& value, const char* message) {
    require_finite_bounded(value.x, message);
    require_finite_bounded(value.y, message);
    require_finite_bounded(value.z, message);
    require_finite_bounded(value.w, message);
}

bool is_sprite_type(std::uint16_t type) {
    return type == kSimpleSpriteNodeType || type == kSpriteNodeType;
}

bool is_particle_type(std::uint16_t type) {
    return is_sprite_type(type) || type == kLineNodeType;
}

bool is_emitter_type(std::uint16_t type) {
    return type == kOmniNodeType || type == kCircleNodeType;
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

    double divide(double left, double right) const {
        return rounded(left / right);
    }

    float spill(double value) const {
        return static_cast<float>(value);
    }
};

AmeEffectVector4 add_vectors(
    const AmeEffectVector4& left,
    const AmeEffectVector4& right,
    const Arithmetic& arithmetic) {
    return {
        arithmetic.spill(arithmetic.add(left.x, right.x)),
        arithmetic.spill(arithmetic.add(left.y, right.y)),
        arithmetic.spill(arithmetic.add(left.z, right.z)),
        left.w,
    };
}

void add_vector_in_place(
    AmeEffectVector4& target,
    const AmeEffectVector4& value,
    const Arithmetic& arithmetic) {
    target = add_vectors(target, value, arithmetic);
}

AmeEffectVector4 scale_vector(
    const AmeEffectVector4& value,
    float scale,
    const Arithmetic& arithmetic) {
    return {
        arithmetic.spill(arithmetic.multiply(value.x, scale)),
        arithmetic.spill(arithmetic.multiply(value.y, scale)),
        arithmetic.spill(arithmetic.multiply(value.z, scale)),
        value.w,
    };
}

std::uint32_t next_crt_rand(std::uint32_t& state) {
    state = state * 0x343fdu + 0x269ec3u;
    return (state >> 16u) & 0x7fffu;
}

float vector_length(const AmeEffectVector4& value, const Arithmetic& arithmetic) {
    const float squared = arithmetic.spill(arithmetic.add(
        arithmetic.add(arithmetic.multiply(value.x, value.x), arithmetic.multiply(value.y, value.y)),
        arithmetic.multiply(value.z, value.z)));
    return arithmetic.spill(arithmetic.rounded(std::sqrt(static_cast<double>(squared))));
}

AmeEffectVector4 normalize_vector(const AmeEffectVector4& value, const Arithmetic& arithmetic) {
    const float length = vector_length(value, arithmetic);
    return length == 0.0f ? value : scale_vector(
        value, arithmetic.spill(arithmetic.divide(1.0, length)), arithmetic);
}

AmeEffectQuaternion multiply_quaternions(
    const AmeEffectQuaternion& left,
    const AmeEffectQuaternion& right,
    const Arithmetic& arithmetic) {
    return {
        arithmetic.spill(arithmetic.subtract(arithmetic.add(arithmetic.add(
            arithmetic.multiply(right.x, left.w), arithmetic.multiply(right.w, left.x)),
            arithmetic.multiply(right.z, left.y)), arithmetic.multiply(right.y, left.z))),
        arithmetic.spill(arithmetic.add(arithmetic.add(arithmetic.subtract(
            arithmetic.multiply(right.y, left.w), arithmetic.multiply(right.z, left.x)),
            arithmetic.multiply(right.w, left.y)), arithmetic.multiply(right.x, left.z))),
        arithmetic.spill(arithmetic.add(arithmetic.subtract(arithmetic.add(
            arithmetic.multiply(right.z, left.w), arithmetic.multiply(right.y, left.x)),
            arithmetic.multiply(right.x, left.y)), arithmetic.multiply(right.w, left.z))),
        arithmetic.spill(arithmetic.subtract(arithmetic.subtract(arithmetic.subtract(
            arithmetic.multiply(left.w, right.w), arithmetic.multiply(left.x, right.x)),
            arithmetic.multiply(left.y, right.y)), arithmetic.multiply(left.z, right.z))),
    };
}

AmeEffectVector4 matrix_vector(
    const AmeEffectVector4& value,
    const CameraMatrix& matrix,
    const Arithmetic& arithmetic) {
    return {
        arithmetic.spill(arithmetic.add(arithmetic.add(
            arithmetic.multiply(value.y, matrix[4u]), arithmetic.multiply(value.x, matrix[0u])),
            arithmetic.multiply(value.z, matrix[8u]))),
        arithmetic.spill(arithmetic.add(arithmetic.add(
            arithmetic.multiply(value.x, matrix[1u]), arithmetic.multiply(value.y, matrix[5u])),
            arithmetic.multiply(value.z, matrix[9u]))),
        arithmetic.spill(arithmetic.add(arithmetic.add(
            arithmetic.multiply(value.x, matrix[2u]), arithmetic.multiply(value.y, matrix[6u])),
            arithmetic.multiply(value.z, matrix[10u]))),
        value.w,
    };
}

float random_unit(std::uint32_t& state, const Arithmetic& arithmetic) {
    return arithmetic.spill(arithmetic.multiply(static_cast<double>(next_crt_rand(state)), kRandomScale));
}

float random_centered(std::uint32_t& state, const Arithmetic& arithmetic) {
    return arithmetic.spill(arithmetic.subtract(
        arithmetic.multiply(static_cast<double>(next_crt_rand(state)), kRandomScale),
        0.5));
}

AmeEffectVector4 random_direction(std::uint32_t& state, const Arithmetic& arithmetic) {
    AmeEffectVector4 direction{
        random_centered(state, arithmetic),
        random_centered(state, arithmetic),
        random_centered(state, arithmetic),
        1.0f,
    };
    const double squared_x = arithmetic.multiply(direction.x, direction.x);
    const double squared_y = arithmetic.multiply(direction.y, direction.y);
    const double squared_z = arithmetic.multiply(direction.z, direction.z);
    const float length_squared = arithmetic.spill(arithmetic.add(
        arithmetic.add(squared_x, squared_y),
        squared_z));
    const float length = arithmetic.spill(arithmetic.rounded(std::sqrt(static_cast<double>(length_squared))));
    if (length == 0.0f) {
        return direction;
    }
    const float reciprocal = arithmetic.spill(arithmetic.divide(1.0, length));
    direction.x = arithmetic.spill(arithmetic.multiply(direction.x, reciprocal));
    direction.y = arithmetic.spill(arithmetic.multiply(direction.y, reciprocal));
    direction.z = arithmetic.spill(arithmetic.multiply(direction.z, reciprocal));
    return direction;
}

std::uint8_t color_component(std::uint32_t color, unsigned int shift) {
    return static_cast<std::uint8_t>((color >> shift) & 0xffu);
}

std::uint32_t pack_color(
    std::uint8_t red,
    std::uint8_t green,
    std::uint8_t blue,
    std::uint8_t alpha) {
    return static_cast<std::uint32_t>(red) |
        (static_cast<std::uint32_t>(green) << 8u) |
        (static_cast<std::uint32_t>(blue) << 16u) |
        (static_cast<std::uint32_t>(alpha) << 24u);
}

std::uint32_t apply_transparency(std::uint32_t color, std::int32_t transparency) {
    const std::uint8_t alpha = static_cast<std::uint8_t>(
        (static_cast<std::int32_t>(color_component(color, 24u)) * transparency) >> 8);
    return pack_color(
        color_component(color, 0u),
        color_component(color, 8u),
        color_component(color, 16u),
        alpha);
}

std::uint8_t interpolate_color_component(
    std::uint8_t start,
    std::uint8_t end,
    std::int32_t ratio) {
    const std::int32_t result = ((static_cast<std::int32_t>(start) << 8) +
        (static_cast<std::int32_t>(end) - static_cast<std::int32_t>(start)) * ratio) >> 8;
    return static_cast<std::uint8_t>(result);
}

std::uint32_t interpolate_color(
    std::uint32_t start,
    std::uint32_t end,
    float rate,
    std::int32_t transparency,
    const Arithmetic& arithmetic) {
    const std::int32_t ratio = static_cast<std::int32_t>(arithmetic.multiply(rate, 256.0));
    const std::uint8_t alpha = static_cast<std::uint8_t>(
        (static_cast<std::int32_t>(interpolate_color_component(
            color_component(start, 24u), color_component(end, 24u), ratio)) * transparency) >> 8);
    return pack_color(
        interpolate_color_component(color_component(start, 0u), color_component(end, 0u), ratio),
        interpolate_color_component(color_component(start, 8u), color_component(end, 8u), ratio),
        interpolate_color_component(color_component(start, 16u), color_component(end, 16u), ratio),
        alpha);
}

template<class TextureNode>
void validate_texture(const TextureNode& sprite, std::uint32_t flags) {
    if ((flags & ~kSupportedSpriteFlags) != 0u) {
        fail("AME sprite flags are unsupported");
    }
    if (sprite.texture_key_count < 0 ||
        static_cast<std::size_t>(sprite.texture_key_count) != sprite.texture_keys.size()) {
        fail("AME sprite texture keys are inconsistent");
    }
    if ((flags & kAnimateTexture) != 0u && sprite.texture_keys.empty()) {
        fail("AME sprite animation has no texture keys");
    }
    for (const AmeEffectTextureAnimationKey& key : sprite.texture_keys) {
        require_finite_bounded(key.time, "AME texture key time is nonfinite or out of range");
        require_finite_bounded(key.left, "AME texture key value is nonfinite or out of range");
        require_finite_bounded(key.top, "AME texture key value is nonfinite or out of range");
        require_finite_bounded(key.right, "AME texture key value is nonfinite or out of range");
        require_finite_bounded(key.bottom, "AME texture key value is nonfinite or out of range");
    }
}

void validate_sprite(const AmeEffectSpriteNode& sprite, std::uint32_t flags) {
    const float values[] = {
        sprite.translation.x,
        sprite.translation.y,
        sprite.translation.z,
        sprite.translation.w,
        sprite.rotation.x,
        sprite.rotation.y,
        sprite.rotation.z,
        sprite.rotation.w,
        sprite.z_bias,
        sprite.inheritance_rate,
        sprite.life,
        sprite.start_time,
        sprite.size,
        sprite.size_chaos,
        sprite.scale_x_start,
        sprite.scale_x_end,
        sprite.scale_y_start,
        sprite.scale_y_end,
        sprite.twist_angle,
        sprite.twist_angle_chaos,
        sprite.twist_angle_speed,
        sprite.crop_left,
        sprite.crop_top,
        sprite.crop_right,
        sprite.crop_bottom,
        sprite.scroll_u,
        sprite.scroll_v,
        sprite.texture_animation_time,
    };
    for (float value : values) {
        require_finite_bounded(value, "AME sprite value is nonfinite or out of range");
    }
    validate_texture(sprite, flags);
}

void validate_omni(const AmeEffectOmniNode& omni, std::uint32_t flags) {
    if (flags != 0u) {
        fail("AME omni flags are unsupported");
    }
    const float values[] = {
        omni.translation.x,
        omni.translation.y,
        omni.translation.z,
        omni.translation.w,
        omni.rotation.x,
        omni.rotation.y,
        omni.rotation.z,
        omni.rotation.w,
        omni.inheritance_rate,
        omni.life,
        omni.start_time,
        omni.offset,
        omni.offset_chaos,
        omni.speed,
        omni.speed_chaos,
        omni.max_count,
        omni.frequency,
    };
    for (float value : values) {
        require_finite_bounded(value, "AME omni value is nonfinite or out of range");
    }
}

void validate_node_payload(const AmeEffectNode& node) {
    if (node.type == kOmniNodeType) {
        if (!std::holds_alternative<AmeEffectOmniNode>(node.payload)) {
            fail("AME omni payload is invalid");
        }
        validate_omni(std::get<AmeEffectOmniNode>(node.payload), node.flags);
        return;
    }
    if (is_sprite_type(node.type)) {
        if (!std::holds_alternative<AmeEffectSpriteNode>(node.payload)) {
            fail("AME sprite payload is invalid");
        }
        validate_sprite(std::get<AmeEffectSpriteNode>(node.payload), node.flags);
        return;
    }
    if (node.type == kCircleNodeType) {
        const auto* circle = std::get_if<AmeEffectCircleNode>(&node.payload);
        if (circle == nullptr) {
            fail("AME circle payload is invalid");
        }
        validate_omni(circle->emitter, 0u);
        const auto& rotation = circle->emitter.rotation;
        if ((node.flags & ~3u) != 0u ||
            rotation.x != 0.0f || rotation.y != 0.0f || rotation.z != 0.0f || rotation.w != 1.0f) {
            fail("AME circle flags or rotation are unsupported");
        }
        for (float value : {circle->spread, circle->spread_variation, circle->radius, circle->radius_variation}) {
            require_finite_bounded(value, "AME circle value is nonfinite or out of range");
        }
        if ((node.flags & 3u) == 3u &&
            (circle->emitter.max_count < 1.0f || circle->emitter.max_count > 65535.0f)) {
            fail("AME circle angular division count is unsupported");
        }
        return;
    }
    if (node.type == kLineNodeType) {
        const auto* line = std::get_if<AmeEffectLineNode>(&node.payload);
        if (line == nullptr) {
            fail("AME line payload is invalid");
        }
        require_vector(line->translation, "AME line translation is invalid");
        for (float value : {line->rotation.x, line->rotation.y, line->rotation.z, line->rotation.w,
                line->z_bias, line->inheritance_rate, line->life, line->start_time,
                line->length_start, line->length_end, line->inside_width_start, line->inside_width_end,
                line->outside_width_start, line->outside_width_end, line->crop_left, line->crop_top,
                line->crop_right, line->crop_bottom, line->scroll_u, line->scroll_v, line->texture_animation_time}) {
            require_finite_bounded(value, "AME line value is nonfinite or out of range");
        }
        if ((node.flags & kRandomTwist) != 0u) {
            fail("AME line twist flags are unsupported");
        }
        validate_texture(*line, node.flags);
        return;
    }
    if (node.type == kRadialNodeType) {
        const auto* radial = std::get_if<AmeEffectRadialNode>(&node.payload);
        if (radial == nullptr) {
            fail("AME radial payload is invalid");
        }
        require_vector(radial->position, "AME radial position is invalid");
        require_finite_bounded(radial->magnitude, "AME radial magnitude is invalid");
        if ((node.flags & ~3u) != 0u || radial->attenuation != 0.0f) {
            fail("AME radial flags or attenuation are unsupported");
        }
        return;
    }
    fail("AME node type is unsupported");
}

void validate_tree(
    const AmeEffectData& effect,
    std::size_t index,
    std::optional<std::size_t> expected_parent,
    bool root_level,
    std::vector<bool>& visited) {
    if (index >= effect.nodes.size() || visited[index]) {
        fail("AME runtime graph is invalid");
    }
    const AmeEffectNode& node = effect.nodes[index];
    if (node.parent_index != expected_parent) {
        fail("AME runtime parent link is invalid");
    }
    if (root_level) {
        if (!is_emitter_type(node.type)) {
            fail("AME runtime root type is unsupported");
        }
    } else if (!is_particle_type(node.type) && node.type != kRadialNodeType) {
        fail("AME runtime child type is unsupported");
    }
    if (node.type == kRadialNodeType &&
        (node.child_index.has_value() || !expected_parent.has_value() ||
            !is_particle_type(effect.nodes[*expected_parent].type))) {
        fail("AME radial attachment is unsupported");
    }
    validate_node_payload(node);
    visited[index] = true;
    if (node.child_index.has_value()) {
        validate_tree(effect, *node.child_index, index, false, visited);
    }
    if (node.sibling_index.has_value()) {
        validate_tree(effect, *node.sibling_index, expected_parent, root_level, visited);
    }
}

void validate_effect(const AmeEffectData& effect) {
    for (const AmeEffectNode& node : effect.nodes) {
        validate_node_payload(node);
        if (node.child_index.has_value() && *node.child_index >= effect.nodes.size()) {
            fail("AME runtime child link is invalid");
        }
        if (node.sibling_index.has_value() && *node.sibling_index >= effect.nodes.size()) {
            fail("AME runtime sibling link is invalid");
        }
        if (node.parent_index.has_value() && *node.parent_index >= effect.nodes.size()) {
            fail("AME runtime parent link is invalid");
        }
    }
    if (!effect.header.root_node_index.has_value()) {
        if (!effect.nodes.empty()) {
            fail("AME runtime graph has no root");
        }
        return;
    }
    if (*effect.header.root_node_index >= effect.nodes.size()) {
        fail("AME runtime root link is invalid");
    }
    std::vector<bool> visited(effect.nodes.size(), false);
    validate_tree(effect, *effect.header.root_node_index, std::nullopt, true, visited);
    for (bool node_visited : visited) {
        if (!node_visited) {
            fail("AME runtime graph is disconnected");
        }
    }
}

void validate_input(const AmeRuntimeInput& input) {
    require_vector(input.position, "AME runtime position is nonfinite or out of range");
    require_vector(input.velocity, "AME runtime velocity is nonfinite or out of range");
    require_vector(input.parent_position, "AME runtime parent position is nonfinite or out of range");
    require_vector(input.parent_velocity, "AME runtime parent velocity is nonfinite or out of range");
    require_finite_bounded(input.size_rate, "AME runtime size rate is nonfinite or out of range");
    if (input.transparency < 0 || input.transparency > 256) {
        fail("AME runtime transparency is out of range");
    }
}

void validate_tick_inputs(float unit_frame, float unit_time) {
    require_finite_bounded(unit_frame, "AME runtime unit frame is nonfinite or out of range");
    require_finite_bounded(unit_time, "AME runtime unit time is nonfinite or out of range");
}

}

class AmeRuntime::Implementation {
public:
    Implementation(const AmeEffectData& effect, const AmeRuntimeInput& input, CameraPrecision precision)
        : effect_(effect), input_(input), arithmetic_{precision} {
        require_supported_precision(precision);
        validate_input(input_);
        validate_effect(effect_);
        if (!effect_.header.root_node_index.has_value()) {
            complete_ = true;
            rebuild_views();
            return;
        }
        std::optional<std::size_t> root = effect_.header.root_node_index;
        while (root.has_value()) {
            const std::size_t emitter = create_emitter(
                *root,
                input_.position,
                input_.velocity,
                input_.parent_position,
                input_.parent_velocity);
            runtimes_[emitter].state |= kRuntimeOwner;
            root = effect_.nodes[*root].sibling_index;
        }
        rebuild_views();
    }

    void advance(std::uint32_t& crt_rng_state, float unit_frame, float unit_time) {
        validate_tick_inputs(unit_frame, unit_time);
        if (complete_) {
            return;
        }

        const std::size_t entry_count = entries_.size();
        for (std::size_t index = 0u; index < entry_count; ++index) {
            const std::size_t runtime_index = entries_[index];
            Runtime& runtime = runtimes_[runtime_index];
            if (!runtime.alive) {
                continue;
            }
            if ((runtime.state & kRuntimeFinished) != 0u &&
                runtime.waiting.empty() && runtime.active.empty()) {
                if (runtime.spawn_runtime.has_value()) {
                    runtimes_[*runtime.spawn_runtime].state |= kRuntimeFinished;
                }
                runtime.state |= kRuntimeDelete;
                continue;
            }

            const AmeEffectNode& node = effect_.nodes[runtime.node_index];
            if (is_emitter_type(node.type)) {
                if (update_emitter(runtime_index, crt_rng_state, unit_frame, unit_time)) {
                    Runtime& emitter = runtimes_[runtime_index];
                    emitter.state |= kRuntimeDelete;
                    for (const std::size_t child : emitter.children) {
                        runtimes_[child].state |= kRuntimeFinished;
                    }
                }
            } else {
                if (node.type == kLineNodeType) {
                    update_line_runtime(runtime_index, crt_rng_state, unit_frame, unit_time);
                } else {
                    update_sprite_runtime(runtime_index, crt_rng_state, unit_frame, unit_time);
                }
                apply_radial_fields(runtime_index, unit_time);
            }
        }

        std::vector<std::size_t> live_entries;
        live_entries.reserve(entries_.size());
        for (const std::size_t runtime_index : entries_) {
            Runtime& runtime = runtimes_[runtime_index];
            if ((runtime.state & kRuntimeDelete) != 0u) {
                runtime.alive = false;
            } else {
                live_entries.push_back(runtime_index);
            }
        }
        entries_ = std::move(live_entries);
        if (entries_.empty()) {
            complete_ = true;
        }
        rebuild_views();
    }

    void set_transform(const AmeRuntimeTransform& transform, const CameraMatrixBackend& backend) {
        require_vector(transform.position, "AME transform position is invalid");
        const auto& rotation = transform.rotation;
        for (float value : {rotation.x, rotation.y, rotation.z, rotation.w}) {
            require_finite_bounded(value, "AME transform rotation is invalid");
        }
        for (const std::size_t index : entries_) {
            const Runtime& runtime = runtimes_[index];
            if ((runtime.state & kRuntimeOwner) != 0u &&
                (effect_.nodes[runtime.node_index].type != kCircleNodeType || !runtime.emitter_work.has_value())) {
                fail("AME transform owner type is unsupported");
            }
        }
        const CameraMatrix matrix = backend.rotation_quaternion(
            {{rotation.x, rotation.y, rotation.z, rotation.w}}, arithmetic_.precision);
        for (float value : matrix) {
            require_finite_bounded(value, "AME transform matrix is invalid");
        }
        input_.position = transform.position;
        for (const std::size_t index : entries_) {
            Runtime& runtime = runtimes_[index];
            if ((runtime.state & kRuntimeOwner) == 0u) {
                continue;
            }
            OmniWork& work = *runtime.emitter_work;
            work.position = add_vectors(omni_node(runtime.node_index).translation, transform.position, arithmetic_);
            work.rotation_matrix = matrix;
        }
    }

    const std::vector<AmeRuntimeSprite>& active_sprites() const noexcept {
        return active_sprites_;
    }

    const std::vector<AmeRuntimeLine>& active_lines() const noexcept {
        return active_lines_;
    }

    const std::vector<AmeRuntimeNodeState>& nodes() const noexcept {
        return nodes_;
    }

    AmeRuntimeLifecycle lifecycle() const noexcept {
        std::size_t waiting_count = 0u;
        std::size_t active_count = 0u;
        for (const std::size_t runtime_index : entries_) {
            const Runtime& runtime = runtimes_[runtime_index];
            waiting_count += runtime.waiting.size();
            active_count += runtime.active.size();
        }
        return {
            complete_ ? -1 : static_cast<std::int32_t>(entries_.size()),
            waiting_count,
            active_count,
            complete_,
        };
    }

private:
    struct SpriteWork {
        float time{};
        AmeEffectVector4 position{};
        AmeEffectVector4 velocity{};
        float base_size{};
        float size_x{};
        float size_y{};
        float twist{};
        float twist_speed{};
        std::uint32_t flags{};
        std::uint32_t color{};
        float texture_time{};
        std::size_t texture_key{};
        float texture_left{};
        float texture_top{};
        float texture_right{};
        float texture_bottom{};
        float line_length{};
        float inside_width{};
        float outside_width{};
        std::uint32_t outside_color{};
    };

    struct OmniWork {
        float time{};
        AmeEffectVector4 position{};
        AmeEffectVector4 velocity{};
        float offset{};
        float offset_chaos{};
        float spread{};
        float radius{};
        CameraMatrix rotation_matrix{{
            1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 1.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f, 0.0f,
            0.0f, 0.0f, 0.0f, 1.0f,
        }};
    };

    struct Runtime {
        std::size_t node_index{};
        std::uint32_t state{};
        float amount{};
        std::uint32_t emitted_count{};
        std::optional<OmniWork> emitter_work;
        std::optional<std::size_t> spawn_runtime;
        std::vector<std::size_t> children;
        std::vector<SpriteWork> waiting;
        std::vector<SpriteWork> active;
        bool alive{true};
    };

    struct CreatedWork {
        std::size_t runtime_index;
        bool waiting;
        std::size_t index;
    };

    const AmeEffectOmniNode& omni_node(std::size_t index) const {
        if (const auto* circle = std::get_if<AmeEffectCircleNode>(&effect_.nodes[index].payload)) {
            return circle->emitter;
        }
        return std::get<AmeEffectOmniNode>(effect_.nodes[index].payload);
    }

    const AmeEffectSpriteNode& sprite_node(std::size_t index) const {
        return std::get<AmeEffectSpriteNode>(effect_.nodes[index].payload);
    }

    SpriteWork& created_work(const CreatedWork& created) {
        Runtime& runtime = runtimes_[created.runtime_index];
        return created.waiting ? runtime.waiting[created.index] : runtime.active[created.index];
    }

    std::optional<std::size_t> first_particle_child(std::size_t node_index) const {
        std::optional<std::size_t> child = effect_.nodes[node_index].child_index;
        while (child.has_value()) {
            if (is_particle_type(effect_.nodes[*child].type)) {
                return child;
            }
            child = effect_.nodes[*child].sibling_index;
        }
        return std::nullopt;
    }

    std::size_t create_group(std::size_t node_index) {
        const std::size_t runtime_index = runtimes_.size();
        runtimes_.push_back({node_index});
        const std::optional<std::size_t> child = first_particle_child(node_index);
        if (child.has_value()) {
            const std::size_t spawn_runtime = create_group(*child);
            runtimes_[runtime_index].spawn_runtime = spawn_runtime;
        }
        entries_.push_back(runtime_index);
        return runtime_index;
    }

    std::size_t create_emitter(
        std::size_t node_index,
        const AmeEffectVector4& position,
        const AmeEffectVector4& velocity,
        const AmeEffectVector4& parent_position,
        const AmeEffectVector4& parent_velocity) {
        const AmeEffectOmniNode& node = omni_node(node_index);
        OmniWork work{};
        work.time = -node.start_time;
        work.position = add_vectors(parent_position, position, arithmetic_);
        add_vector_in_place(work.position, node.translation, arithmetic_);
        work.velocity = scale_vector(parent_velocity, node.inheritance_rate, arithmetic_);
        add_vector_in_place(work.velocity, velocity, arithmetic_);
        work.offset = arithmetic_.spill(arithmetic_.multiply(node.offset, input_.size_rate));
        work.offset_chaos = arithmetic_.spill(arithmetic_.multiply(node.offset_chaos, input_.size_rate));

        if (const auto* circle = std::get_if<AmeEffectCircleNode>(&effect_.nodes[node_index].payload)) {
            work.spread = circle->spread;
            work.radius = arithmetic_.spill(arithmetic_.multiply(circle->radius, input_.size_rate));
        }

        const std::size_t runtime_index = runtimes_.size();
        Runtime runtime{node_index};
        runtime.emitter_work = work;
        runtimes_.push_back(std::move(runtime));
        entries_.push_back(runtime_index);

        std::optional<std::size_t> child = effect_.nodes[node_index].child_index;
        while (child.has_value()) {
            const std::size_t child_runtime = create_group(*child);
            runtimes_[runtime_index].children.push_back(child_runtime);
            child = effect_.nodes[*child].sibling_index;
        }
        return runtime_index;
    }

    void set_texture_coordinates(SpriteWork& work, const AmeEffectTextureAnimationKey& key) const {
        work.texture_left = key.left;
        work.texture_top = key.top;
        work.texture_right = key.right;
        work.texture_bottom = key.bottom;
    }

    void flip_horizontal(SpriteWork& work) const {
        std::swap(work.texture_left, work.texture_right);
    }

    void flip_vertical(SpriteWork& work) const {
        std::swap(work.texture_top, work.texture_bottom);
    }

    template<class TextureNode>
    void initialize_texture(
        SpriteWork& work,
        const TextureNode& node,
        std::uint32_t node_flags,
        std::uint32_t& crt_rng_state) const {
        if ((node_flags & kAnimateTexture) != 0u) {
            work.texture_time = 0.0f;
            work.texture_key = 0u;
            if ((node_flags & kRandomTextureKey) != 0u) {
                const std::int32_t random_key = static_cast<std::int32_t>(
                    arithmetic_.multiply(100.0, random_unit(crt_rng_state, arithmetic_)));
                work.texture_key = static_cast<std::size_t>(random_key % node.texture_key_count);
            }
            set_texture_coordinates(work, node.texture_keys[work.texture_key]);
        } else if ((node_flags & kCropTexture) != 0u) {
            work.texture_left = node.crop_left;
            work.texture_top = node.crop_top;
            work.texture_right = node.crop_right;
            work.texture_bottom = node.crop_bottom;
        } else {
            work.texture_left = 0.0f;
            work.texture_top = 0.0f;
            work.texture_right = 1.0f;
            work.texture_bottom = 1.0f;
        }
    }

    void initialize_flips(
        SpriteWork& work,
        std::uint32_t node_flags,
        std::uint32_t& crt_rng_state) const {
        if ((node_flags & kForceHorizontalFlip) != 0u ||
            ((node_flags & kRandomHorizontalFlip) != 0u && random_unit(crt_rng_state, arithmetic_) > 0.5f)) {
            work.flags |= kHorizontalFlip;
            flip_horizontal(work);
        }
        if ((node_flags & kForceVerticalFlip) != 0u ||
            ((node_flags & kRandomVerticalFlip) != 0u && random_unit(crt_rng_state, arithmetic_) > 0.5f)) {
            work.flags |= kVerticalFlip;
            flip_vertical(work);
        }
    }

    CreatedWork create_line_particle(
        std::size_t runtime_index,
        const AmeEffectVector4& position,
        const AmeEffectVector4& velocity,
        const AmeEffectVector4& parent_position,
        const AmeEffectVector4& parent_velocity,
        std::uint32_t& crt_rng_state) {
        Runtime& runtime = runtimes_[runtime_index];
        const auto& effect_node = effect_.nodes[runtime.node_index];
        const auto& node = std::get<AmeEffectLineNode>(effect_node.payload);
        SpriteWork work{};
        work.time = -node.start_time;
        work.line_length = node.length_start;
        work.inside_width = node.inside_width_start;
        work.outside_width = node.outside_width_start;
        work.color = apply_transparency(node.inside_color_start, input_.transparency);
        work.outside_color = apply_transparency(node.outside_color_start, input_.transparency);
        work.position = add_vectors(parent_position, position, arithmetic_);
        add_vector_in_place(work.position, node.translation, arithmetic_);
        work.velocity = scale_vector(parent_velocity, node.inheritance_rate, arithmetic_);
        add_vector_in_place(work.velocity, velocity, arithmetic_);
        initialize_texture(work, node, effect_node.flags, crt_rng_state);
        initialize_flips(work, effect_node.flags, crt_rng_state);
        if (work.time < 0.0f) {
            runtime.waiting.push_back(std::move(work));
            return {runtime_index, true, runtime.waiting.size() - 1u};
        }
        runtime.active.push_back(std::move(work));
        return {runtime_index, false, runtime.active.size() - 1u};
    }

    CreatedWork create_particle(
        std::size_t runtime_index,
        const AmeEffectVector4& position,
        const AmeEffectVector4& velocity,
        const AmeEffectVector4& parent_position,
        const AmeEffectVector4& parent_velocity,
        std::uint32_t& crt_rng_state) {
        Runtime& runtime = runtimes_[runtime_index];
        const AmeEffectNode& effect_node = effect_.nodes[runtime.node_index];
        if (effect_node.type == kLineNodeType) {
            return create_line_particle(runtime_index, position, velocity, parent_position, parent_velocity, crt_rng_state);
        }
        const AmeEffectSpriteNode& node = sprite_node(runtime.node_index);
        SpriteWork work{};
        work.time = -node.start_time;
        work.color = apply_transparency(node.color_start, input_.transparency);
        work.position = add_vectors(parent_position, position, arithmetic_);
        add_vector_in_place(work.position, node.translation, arithmetic_);
        work.velocity = scale_vector(parent_velocity, node.inheritance_rate, arithmetic_);
        add_vector_in_place(work.velocity, velocity, arithmetic_);
        work.base_size = arithmetic_.spill(arithmetic_.add(
            node.size,
            arithmetic_.multiply(node.size_chaos, random_unit(crt_rng_state, arithmetic_))));
        work.size_x = arithmetic_.spill(arithmetic_.multiply(work.base_size, node.scale_x_start));
        work.size_y = arithmetic_.spill(arithmetic_.multiply(work.base_size, node.scale_y_start));
        if (effect_node.type == kSpriteNodeType) {
            work.twist = arithmetic_.spill(arithmetic_.add(
                node.twist_angle,
                arithmetic_.multiply(node.twist_angle_chaos, random_unit(crt_rng_state, arithmetic_))));
            if ((effect_node.flags & kRandomTwist) != 0u && random_unit(crt_rng_state, arithmetic_) > 0.5f) {
                work.flags |= kTwistReverse;
            }
            work.twist_speed = (work.flags & kTwistReverse) == 0u
                ? node.twist_angle_speed
                : -node.twist_angle_speed;
        }

        initialize_texture(work, node, effect_node.flags, crt_rng_state);
        initialize_flips(work, effect_node.flags, crt_rng_state);

        if (work.time < 0.0f) {
            runtime.waiting.push_back(std::move(work));
            return {runtime_index, true, runtime.waiting.size() - 1u};
        }
        runtime.active.push_back(std::move(work));
        return {runtime_index, false, runtime.active.size() - 1u};
    }

    void synchronize_child_flags(
        std::size_t parent_runtime_index,
        const SpriteWork& parent_work,
        const CreatedWork& child_created) {
        const Runtime& parent_runtime = runtimes_[parent_runtime_index];
        Runtime& child_runtime = runtimes_[child_created.runtime_index];
        const AmeEffectNode& parent_node = effect_.nodes[parent_runtime.node_index];
        const AmeEffectNode& child_node = effect_.nodes[child_runtime.node_index];
        if (parent_node.type != child_node.type) {
            return;
        }
        SpriteWork& child_work = created_work(child_created);
        if (child_node.type == kSpriteNodeType &&
            (parent_node.flags & child_node.flags & kRandomTwist) != 0u) {
            if ((parent_work.flags & kTwistReverse) != 0u) {
                child_work.flags |= kTwistReverse;
            } else {
                child_work.flags &= ~kTwistReverse;
            }
        }
        if ((parent_node.flags & child_node.flags & kRandomHorizontalFlip) != 0u &&
            ((parent_work.flags ^ child_work.flags) & kHorizontalFlip) != 0u) {
            child_work.flags ^= kHorizontalFlip;
            flip_horizontal(child_work);
        }
        if ((parent_node.flags & child_node.flags & kRandomVerticalFlip) != 0u &&
            ((parent_work.flags ^ child_work.flags) & kVerticalFlip) != 0u) {
            child_work.flags ^= kVerticalFlip;
            flip_vertical(child_work);
        }
    }

    void create_spawn_particle(
        std::size_t parent_runtime_index,
        const SpriteWork& parent_work,
        std::uint32_t& crt_rng_state) {
        Runtime& parent_runtime = runtimes_[parent_runtime_index];
        if (!parent_runtime.spawn_runtime.has_value()) {
            return;
        }
        const std::size_t child_runtime_index = *parent_runtime.spawn_runtime;
        Runtime& child_runtime = runtimes_[child_runtime_index];
        if ((parent_runtime.state & kRuntimeOwner) != 0u) {
            child_runtime.state |= kRuntimeOwner;
        }
        const AmeEffectVector4 zero{0.0f, 0.0f, 0.0f, 1.0f};
        const CreatedWork child = create_particle(
            child_runtime_index,
            zero,
            zero,
            parent_work.position,
            parent_work.velocity,
            crt_rng_state);
        synchronize_child_flags(parent_runtime_index, parent_work, child);
    }

    bool update_circle(
        std::size_t runtime_index,
        std::uint32_t& crt_rng_state,
        float unit_frame,
        float unit_time) {
        Runtime& runtime = runtimes_[runtime_index];
        const auto& effect_node = effect_.nodes[runtime.node_index];
        const auto& circle = std::get<AmeEffectCircleNode>(effect_node.payload);
        const auto& node = circle.emitter;
        OmniWork& work = *runtime.emitter_work;
        work.time = arithmetic_.spill(arithmetic_.add(work.time, unit_frame));
        if (work.time <= 0.0f) {
            return false;
        }
        if (node.life != -1.0f && work.time >= node.life) {
            return true;
        }
        add_vector_in_place(work.position, scale_vector(work.velocity, unit_time, arithmetic_), arithmetic_);
        work.spread = arithmetic_.spill(arithmetic_.add(
            work.spread, arithmetic_.multiply(circle.spread_variation, unit_time)));
        work.radius = circle.radius_variation == 0.0f
            ? arithmetic_.spill(arithmetic_.multiply(circle.radius, input_.size_rate))
            : arithmetic_.spill(arithmetic_.add(work.radius, arithmetic_.multiply(circle.radius_variation, unit_time)));
        work.offset = arithmetic_.spill(arithmetic_.multiply(node.offset, input_.size_rate));
        work.offset_chaos = arithmetic_.spill(arithmetic_.multiply(node.offset_chaos, input_.size_rate));

        for (const std::size_t child_runtime_index : runtime.children) {
            Runtime& child = runtimes_[child_runtime_index];
            child.amount = arithmetic_.spill(arithmetic_.add(
                child.amount, arithmetic_.multiply(node.frequency, unit_frame)));
            while (child.amount >= 1.0f) {
                child.amount = arithmetic_.spill(arithmetic_.subtract(child.amount, 1.0));
                ++child.emitted_count;
                if (node.max_count == -1.0f ||
                    static_cast<double>(child.waiting.size() + child.active.size()) >= node.max_count) {
                    continue;
                }
                auto angle = static_cast<std::int32_t>(
                    arithmetic_.multiply(random_unit(crt_rng_state, arithmetic_), 10000000.0));
                float radius = work.radius;
                if ((effect_node.flags & 1u) == 0u) {
                    radius = arithmetic_.spill(arithmetic_.multiply(radius, random_unit(crt_rng_state, arithmetic_)));
                } else if ((effect_node.flags & 2u) != 0u) {
                    const auto count = static_cast<std::uint32_t>(node.max_count);
                    angle = static_cast<std::int32_t>((65535u / count) * (child.emitted_count % count));
                }
                const auto rotation = nn_sin_cos(static_cast<std::uint16_t>(angle), arithmetic_.precision);
                AmeEffectVector4 position{
                    arithmetic_.spill(arithmetic_.multiply(rotation.sine, radius)), 0.0f,
                    arithmetic_.spill(arithmetic_.multiply(rotation.cosine, radius)), 1.0f,
                };
                const auto axis = normalize_vector({
                    arithmetic_.spill(arithmetic_.subtract(position.z, arithmetic_.multiply(0.0, position.y))),
                    arithmetic_.spill(arithmetic_.subtract(arithmetic_.multiply(0.0, position.x),
                        arithmetic_.multiply(0.0, position.z))),
                    arithmetic_.spill(arithmetic_.subtract(arithmetic_.multiply(0.0, position.y), position.x)),
                    1.0f,
                }, arithmetic_);
                const float radians = arithmetic_.spill(arithmetic_.multiply(work.spread, 0x1.1df46a0000000p-6));
                const float half_radians = arithmetic_.spill(arithmetic_.multiply(radians, 0.5));
                const double quaternion_angle = arithmetic_.multiply(half_radians, 10430.3779296875);
                if (!std::isfinite(quaternion_angle) ||
                    quaternion_angle < std::numeric_limits<std::int32_t>::min() ||
                    quaternion_angle > std::numeric_limits<std::int32_t>::max()) {
                    fail("AME circle spread exceeds the supported angle range");
                }
                const auto half_rotation = nn_sin_cos(
                    static_cast<std::uint16_t>(static_cast<std::int32_t>(quaternion_angle)), arithmetic_.precision);
                const AmeEffectQuaternion quaternion{
                    arithmetic_.spill(arithmetic_.multiply(axis.x, half_rotation.sine)),
                    arithmetic_.spill(arithmetic_.multiply(axis.y, half_rotation.sine)),
                    arithmetic_.spill(arithmetic_.multiply(axis.z, half_rotation.sine)),
                    half_rotation.cosine,
                };
                const auto product = multiply_quaternions(
                    quaternion, {0.0f, 1.0f, 0.0f, 1.0f}, arithmetic_);
                const auto rotated = multiply_quaternions(product,
                    {-quaternion.x, -quaternion.y, -quaternion.z, quaternion.w}, arithmetic_);
                const auto direction = matrix_vector(
                    {rotated.x, rotated.y, rotated.z, rotated.w}, work.rotation_matrix, arithmetic_);
                position = matrix_vector(position, work.rotation_matrix, arithmetic_);
                const float position_scale = arithmetic_.spill(arithmetic_.add(
                    work.offset, arithmetic_.multiply(work.offset_chaos, random_unit(crt_rng_state, arithmetic_))));
                add_vector_in_place(position, scale_vector(direction, position_scale, arithmetic_), arithmetic_);
                const float velocity_scale = arithmetic_.spill(arithmetic_.add(
                    node.speed, arithmetic_.multiply(node.speed_chaos, random_unit(crt_rng_state, arithmetic_))));
                create_particle(child_runtime_index, position, scale_vector(direction, velocity_scale, arithmetic_),
                    work.position, work.velocity, crt_rng_state);
            }
        }
        return false;
    }

    bool update_emitter(
        std::size_t runtime_index,
        std::uint32_t& crt_rng_state,
        float unit_frame,
        float unit_time) {
        Runtime& runtime = runtimes_[runtime_index];
        if (effect_.nodes[runtime.node_index].type == kCircleNodeType) {
            return update_circle(runtime_index, crt_rng_state, unit_frame, unit_time);
        }
        const AmeEffectOmniNode& node = omni_node(runtime.node_index);
        OmniWork& work = *runtime.emitter_work;
        work.time = arithmetic_.spill(arithmetic_.add(work.time, unit_frame));
        if (work.time <= 0.0f) {
            return false;
        }
        if (node.life != -1.0f && work.time >= node.life) {
            return true;
        }

        const AmeEffectVector4 movement = scale_vector(work.velocity, unit_time, arithmetic_);
        add_vector_in_place(work.position, movement, arithmetic_);
        work.offset = arithmetic_.spill(arithmetic_.multiply(node.offset, input_.size_rate));
        work.offset_chaos = arithmetic_.spill(arithmetic_.multiply(node.offset_chaos, input_.size_rate));

        for (const std::size_t child_runtime_index : runtime.children) {
            Runtime& child = runtimes_[child_runtime_index];
            child.amount = arithmetic_.spill(arithmetic_.add(
                child.amount,
                arithmetic_.multiply(node.frequency, unit_frame)));
            while (child.amount >= 1.0f) {
                child.amount = arithmetic_.spill(arithmetic_.subtract(child.amount, 1.0));
                ++child.emitted_count;
                if (node.max_count != -1.0f &&
                    static_cast<double>(child.waiting.size() + child.active.size()) <
                        static_cast<double>(node.max_count)) {
                    const AmeEffectVector4 direction = random_direction(crt_rng_state, arithmetic_);
                    const float position_scale = arithmetic_.spill(arithmetic_.add(
                        work.offset,
                        arithmetic_.multiply(work.offset_chaos, random_unit(crt_rng_state, arithmetic_))));
                    const AmeEffectVector4 position = scale_vector(direction, position_scale, arithmetic_);
                    const float velocity_scale = arithmetic_.spill(arithmetic_.add(
                        node.speed,
                        arithmetic_.multiply(node.speed_chaos, random_unit(crt_rng_state, arithmetic_))));
                    const AmeEffectVector4 velocity = scale_vector(direction, velocity_scale, arithmetic_);
                    create_particle(
                        child_runtime_index,
                        position,
                        velocity,
                        work.position,
                        work.velocity,
                        crt_rng_state);
                }
            }
        }
        return false;
    }

    void activate_waiting(Runtime& runtime, float unit_frame) {
        std::size_t index = 0u;
        while (index < runtime.waiting.size()) {
            SpriteWork& work = runtime.waiting[index];
            work.time = arithmetic_.spill(arithmetic_.add(work.time, unit_frame));
            if (work.time > 0.0f) {
                work.time = arithmetic_.spill(arithmetic_.subtract(work.time, unit_frame));
                runtime.active.push_back(std::move(work));
                runtime.waiting.erase(runtime.waiting.begin() + static_cast<std::ptrdiff_t>(index));
            } else {
                ++index;
            }
        }
    }

    template<class TextureNode>
    void update_texture(
        SpriteWork& work,
        const TextureNode& node,
        std::uint32_t node_flags,
        float unit_frame,
        float unit_time) {
        if ((node_flags & kAnimateTexture) != 0u) {
            if ((work.flags & kTextureFrozen) == 0u) {
                work.texture_time = arithmetic_.spill(arithmetic_.add(work.texture_time, unit_frame));
                if (work.texture_time >= node.texture_keys[work.texture_key].time) {
                    work.texture_time = 0.0f;
                    ++work.texture_key;
                    if (work.texture_key == node.texture_keys.size()) {
                        if ((node_flags & kLoopTexture) != 0u) {
                            work.texture_key = 0u;
                        } else {
                            work.texture_key = node.texture_keys.size() - 1u;
                            work.flags |= kTextureFrozen;
                        }
                    }
                }
            }
            set_texture_coordinates(work, node.texture_keys[work.texture_key]);
            if ((work.flags & kHorizontalFlip) != 0u) {
                flip_horizontal(work);
            }
            if ((work.flags & kVerticalFlip) != 0u) {
                flip_vertical(work);
            }
            return;
        }
        if ((node_flags & kScrollTexture) != 0u) {
            float horizontal = arithmetic_.spill(arithmetic_.multiply(node.scroll_u, unit_time));
            float vertical = arithmetic_.spill(arithmetic_.multiply(node.scroll_v, unit_time));
            if ((work.flags & kHorizontalFlip) != 0u) {
                horizontal = -horizontal;
            }
            if ((work.flags & kVerticalFlip) != 0u) {
                vertical = -vertical;
            }
            work.texture_left = arithmetic_.spill(arithmetic_.add(work.texture_left, horizontal));
            work.texture_top = arithmetic_.spill(arithmetic_.add(work.texture_top, vertical));
            work.texture_right = arithmetic_.spill(arithmetic_.add(work.texture_right, horizontal));
            work.texture_bottom = arithmetic_.spill(arithmetic_.add(work.texture_bottom, vertical));
        }
    }

    void update_sprite_runtime(
        std::size_t runtime_index,
        std::uint32_t& crt_rng_state,
        float unit_frame,
        float unit_time) {
        Runtime& runtime = runtimes_[runtime_index];
        const AmeEffectNode& effect_node = effect_.nodes[runtime.node_index];
        const AmeEffectSpriteNode& node = sprite_node(runtime.node_index);
        activate_waiting(runtime, unit_frame);

        float life = node.life;
        float reciprocal_life = 0.0f;
        if (life >= 0.0f && life != 0.0f) {
            reciprocal_life = arithmetic_.spill(arithmetic_.divide(1.0, life));
        } else if (life < 0.0f) {
            life = std::numeric_limits<float>::max();
        }
        const float scale_x_start = arithmetic_.spill(arithmetic_.multiply(node.scale_x_start, input_.size_rate));
        const float scale_y_start = arithmetic_.spill(arithmetic_.multiply(node.scale_y_start, input_.size_rate));
        const float scale_x_end = arithmetic_.spill(arithmetic_.multiply(node.scale_x_end, input_.size_rate));
        const float scale_y_end = arithmetic_.spill(arithmetic_.multiply(node.scale_y_end, input_.size_rate));

        std::size_t index = 0u;
        while (index < runtime.active.size()) {
            SpriteWork& work = runtime.active[index];
            work.time = arithmetic_.spill(arithmetic_.add(work.time, unit_frame));
            const float rate = arithmetic_.spill(arithmetic_.multiply(work.time, reciprocal_life));
            const AmeEffectVector4 movement = scale_vector(work.velocity, unit_time, arithmetic_);
            add_vector_in_place(work.position, movement, arithmetic_);
            if (work.time >= life) {
                const SpriteWork completed = work;
                create_spawn_particle(runtime_index, completed, crt_rng_state);
                runtime.active.erase(runtime.active.begin() + static_cast<std::ptrdiff_t>(index));
                continue;
            }

            const float complement = arithmetic_.spill(arithmetic_.subtract(1.0, rate));
            const float scale_x = arithmetic_.spill(arithmetic_.add(
                arithmetic_.multiply(scale_x_start, complement),
                arithmetic_.multiply(scale_x_end, rate)));
            const float scale_y = arithmetic_.spill(arithmetic_.add(
                arithmetic_.multiply(scale_y_start, complement),
                arithmetic_.multiply(scale_y_end, rate)));
            work.size_x = arithmetic_.spill(arithmetic_.multiply(work.base_size, scale_x));
            work.size_y = arithmetic_.spill(arithmetic_.multiply(work.base_size, scale_y));
            if (effect_node.type == kSpriteNodeType) {
                work.twist = arithmetic_.spill(arithmetic_.add(
                    work.twist,
                    arithmetic_.multiply(work.twist_speed, unit_time)));
            }
            work.color = interpolate_color(
                node.color_start,
                node.color_end,
                rate,
                input_.transparency,
                arithmetic_);
            update_texture(work, node, effect_node.flags, unit_frame, unit_time);
            ++index;
        }
    }

    void update_line_runtime(
        std::size_t runtime_index,
        std::uint32_t& crt_rng_state,
        float unit_frame,
        float unit_time) {
        Runtime& runtime = runtimes_[runtime_index];
        const auto& effect_node = effect_.nodes[runtime.node_index];
        const auto& node = std::get<AmeEffectLineNode>(effect_node.payload);
        activate_waiting(runtime, unit_frame);
        const float life = node.life < 0.0f ? 1.0e38f : node.life;
        const float reciprocal_life = node.life < 0.0f ? 0.0f
            : arithmetic_.spill(arithmetic_.divide(1.0, node.life));
        const float length_start = arithmetic_.spill(arithmetic_.multiply(node.length_start, input_.size_rate));
        const float length_end = arithmetic_.spill(arithmetic_.multiply(node.length_end, input_.size_rate));
        const float inside_start = arithmetic_.spill(arithmetic_.multiply(node.inside_width_start, input_.size_rate));
        const float inside_end = arithmetic_.spill(arithmetic_.multiply(node.inside_width_end, input_.size_rate));
        const float outside_start = arithmetic_.spill(arithmetic_.multiply(node.outside_width_start, input_.size_rate));
        const float outside_end = arithmetic_.spill(arithmetic_.multiply(node.outside_width_end, input_.size_rate));
        std::size_t index = 0u;
        while (index < runtime.active.size()) {
            SpriteWork& work = runtime.active[index];
            work.time = arithmetic_.spill(arithmetic_.add(work.time, unit_frame));
            const float rate = arithmetic_.spill(arithmetic_.multiply(work.time, reciprocal_life));
            add_vector_in_place(work.position, scale_vector(work.velocity, unit_time, arithmetic_), arithmetic_);
            if (work.time >= life) {
                const SpriteWork completed = work;
                create_spawn_particle(runtime_index, completed, crt_rng_state);
                runtime.active.erase(runtime.active.begin() + static_cast<std::ptrdiff_t>(index));
                continue;
            }
            const float complement = arithmetic_.spill(arithmetic_.subtract(1.0, rate));
            work.line_length = arithmetic_.spill(arithmetic_.add(
                arithmetic_.multiply(length_start, complement), arithmetic_.multiply(length_end, rate)));
            work.inside_width = arithmetic_.spill(arithmetic_.add(
                arithmetic_.multiply(inside_start, complement), arithmetic_.multiply(inside_end, rate)));
            work.outside_width = arithmetic_.spill(arithmetic_.add(
                arithmetic_.multiply(outside_start, complement), arithmetic_.multiply(outside_end, rate)));
            work.color = interpolate_color(node.inside_color_start, node.inside_color_end,
                rate, input_.transparency, arithmetic_);
            work.outside_color = interpolate_color(node.outside_color_start, node.outside_color_end,
                rate, input_.transparency, arithmetic_);
            update_texture(work, node, effect_node.flags, unit_frame, unit_time);
            ++index;
        }
    }

    void apply_radial_fields(std::size_t runtime_index, float unit_time) {
        Runtime& runtime = runtimes_[runtime_index];
        const auto& node = effect_.nodes[runtime.node_index];
        for (SpriteWork& work : runtime.active) {
            std::optional<std::size_t> child = node.child_index;
            while (child.has_value()) {
                const auto& field_node = effect_.nodes[*child];
                if (field_node.type == kRadialNodeType) {
                    const auto& field = std::get<AmeEffectRadialNode>(field_node.payload);
                    AmeEffectVector4 center = field.position;
                    if ((field_node.flags & 1u) != 0u) {
                        add_vector_in_place(center, input_.position, arithmetic_);
                    }
                    AmeEffectVector4 delta{
                        arithmetic_.spill(arithmetic_.subtract(work.position.x, center.x)),
                        arithmetic_.spill(arithmetic_.subtract(work.position.y, center.y)),
                        arithmetic_.spill(arithmetic_.subtract(work.position.z, center.z)),
                        work.position.w,
                    };
                    const float length = vector_length(delta, arithmetic_);
                    if (length > 0.0f) {
                        const float magnitude = arithmetic_.spill(arithmetic_.multiply(field.magnitude, unit_time));
                        delta = scale_vector(delta, arithmetic_.spill(arithmetic_.divide(magnitude, length)), arithmetic_);
                    }
                    add_vector_in_place(work.position, delta, arithmetic_);
                }
                child = field_node.sibling_index;
            }
        }
    }

    void rebuild_views() {
        active_sprites_.clear();
        active_lines_.clear();
        nodes_.clear();
        for (const std::size_t runtime_index : entries_) {
            const Runtime& runtime = runtimes_[runtime_index];
            nodes_.push_back({
                runtime.node_index,
                runtime.state,
                runtime.amount,
                runtime.emitted_count,
                runtime.waiting.size(),
                runtime.active.size(),
            });
            const AmeEffectNode& node = effect_.nodes[runtime.node_index];
            if (node.type == kLineNodeType) {
                const auto& line = std::get<AmeEffectLineNode>(node.payload);
                for (const SpriteWork& work : runtime.active) {
                    active_lines_.push_back({
                        runtime.node_index, node.flags, work.position, work.velocity, work.time,
                        work.line_length, work.inside_width, work.outside_width, work.color, work.outside_color,
                        work.flags, work.texture_time, work.texture_key, work.texture_left, work.texture_top,
                        work.texture_right, work.texture_bottom, line.texture_slot, line.texture_id,
                        line.blend, line.z_bias,
                    });
                }
                continue;
            }
            if (!is_sprite_type(node.type)) {
                continue;
            }
            const AmeEffectSpriteNode& sprite = sprite_node(runtime.node_index);
            for (const SpriteWork& work : runtime.active) {
                active_sprites_.push_back({
                    runtime.node_index,
                    node.type,
                    node.flags,
                    work.position,
                    work.velocity,
                    work.time,
                    work.size_x,
                    work.size_y,
                    work.base_size,
                    work.twist,
                    work.twist_speed,
                    work.flags,
                    work.color,
                    work.texture_time,
                    work.texture_key,
                    work.texture_left,
                    work.texture_top,
                    work.texture_right,
                    work.texture_bottom,
                    sprite.texture_slot,
                    sprite.texture_id,
                    sprite.blend,
                    sprite.z_bias,
                });
            }
        }
    }

    AmeEffectData effect_;
    AmeRuntimeInput input_;
    Arithmetic arithmetic_;
    std::vector<Runtime> runtimes_;
    std::vector<std::size_t> entries_;
    std::vector<AmeRuntimeSprite> active_sprites_;
    std::vector<AmeRuntimeLine> active_lines_;
    std::vector<AmeRuntimeNodeState> nodes_;
    bool complete_{};
};

AmeRuntime::AmeRuntime(
    const AmeEffectData& effect,
    const AmeRuntimeInput& input,
    CameraPrecision precision)
    : implementation_(std::make_unique<Implementation>(effect, input, precision)) {
}

AmeRuntime::AmeRuntime(AmeRuntime&&) noexcept = default;

AmeRuntime& AmeRuntime::operator=(AmeRuntime&&) noexcept = default;

AmeRuntime::~AmeRuntime() = default;

void AmeRuntime::advance(std::uint32_t& crt_rng_state, float unit_frame, float unit_time) {
    implementation_->advance(crt_rng_state, unit_frame, unit_time);
}

void AmeRuntime::set_transform(const AmeRuntimeTransform& transform, const CameraMatrixBackend& backend) {
    implementation_->set_transform(transform, backend);
}

const std::vector<AmeRuntimeSprite>& AmeRuntime::active_sprites() const noexcept {
    return implementation_->active_sprites();
}

const std::vector<AmeRuntimeLine>& AmeRuntime::active_lines() const noexcept {
    return implementation_->active_lines();
}

const std::vector<AmeRuntimeNodeState>& AmeRuntime::nodes() const noexcept {
    return implementation_->nodes();
}

AmeRuntimeLifecycle AmeRuntime::lifecycle() const noexcept {
    return implementation_->lifecycle();
}
