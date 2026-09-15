#include "nn_motion_pose.h"

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <stdexcept>
#include <vector>

namespace {

constexpr std::uint32_t kSupportedNodeFlags = 0x006000cfu;
constexpr std::uint32_t kUnitRotation = 0x00000002u;
constexpr std::uint32_t kInterpolationMask = 0x00000e77u;
constexpr std::uint32_t kLinearInterpolation = 0x00000002u;
constexpr std::uint32_t kConstantInterpolation = 0x00000004u;

void require(bool condition, const char* message) {
    if (!condition) {
        throw std::invalid_argument(message);
    }
}

void require_precision(CameraPrecision precision) {
    require(
        precision == CameraPrecision::Single || precision == CameraPrecision::Double,
        "motion pose precision is unsupported");
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

float linked_component(float primary, float secondary, float weight, const Arithmetic& arithmetic) {
    const double difference = arithmetic.subtract(
        static_cast<double>(secondary), static_cast<double>(primary));
    const double weighted_difference = arithmetic.multiply(
        difference, static_cast<double>(weight));
    return arithmetic.spill(arithmetic.add(weighted_difference, static_cast<double>(primary)));
}

float decode_float(std::uint32_t bits) {
    float value = 0.0f;
    std::memcpy(&value, &bits, sizeof(value));
    return value;
}

std::array<float, 3u> decode_bind_vector(
    const std::array<std::uint32_t, 3u>& bits,
    const char* message) {
    std::array<float, 3u> value{};
    for (std::size_t index = 0u; index < value.size(); ++index) {
        value[index] = decode_float(bits[index]);
        require(std::isfinite(value[index]), message);
    }
    return value;
}

void require_motion_storage(const NnMotionData& motion) {
    require(
        motion.channels.size() <= std::numeric_limits<std::uint32_t>::max() &&
            motion.channel_count == static_cast<std::uint32_t>(motion.channels.size()),
        "motion pose channel storage is inconsistent");
}

void require_ordered_targets(const NnMotionData& motion) {
    for (std::size_t index = 1u; index < motion.channels.size(); ++index) {
        require(
            motion.channels[index - 1u].target <= motion.channels[index].target,
            "motion pose target IDs are not ordered");
    }
}

bool frame_is_in_channel_range(const NnMotionChannel& channel, float frame) {
    const float start = decode_float(channel.start_bits);
    const float end = decode_float(channel.end_bits);
    return start <= frame && frame <= end;
}

enum class ChannelKind {
    Rotation,
    Translation,
    Scale,
};

struct ChannelAxis {
    ChannelKind kind;
    std::size_t index;
};

ChannelAxis active_channel_axis(std::uint32_t flags) {
    switch (flags) {
    case 0x812u:
        return {ChannelKind::Rotation, 0u};
    case 0x1012u:
        return {ChannelKind::Rotation, 1u};
    case 0x2012u:
        return {ChannelKind::Rotation, 2u};
    case 0x101u:
        return {ChannelKind::Translation, 0u};
    case 0x201u:
        return {ChannelKind::Translation, 1u};
    case 0x401u:
        return {ChannelKind::Translation, 2u};
    case 0x8001u:
        return {ChannelKind::Scale, 0u};
    case 0x10001u:
        return {ChannelKind::Scale, 1u};
    case 0x20001u:
        return {ChannelKind::Scale, 2u};
    default:
        throw std::invalid_argument("motion pose active channel type is unsupported");
    }
}

enum class Interpolation {
    Linear,
    Constant,
};

Interpolation active_interpolation(std::uint32_t flags) {
    const std::uint32_t interpolation = flags & kInterpolationMask;
    if (interpolation == kLinearInterpolation) {
        return Interpolation::Linear;
    }
    if (interpolation == kConstantInterpolation) {
        return Interpolation::Constant;
    }
    throw std::invalid_argument("motion pose active interpolation is unsupported");
}

}

NnMotionLocalPose nn_motion_local_pose(
    const NnNodeData& node,
    std::int32_t node_id,
    const NnMotionData& motion,
    std::size_t first_channel,
    float frame,
    CameraPrecision precision) {
    require_precision(precision);
    require(std::isfinite(frame), "motion pose frame must be finite");
    require(node_id >= 0, "motion pose node ID is negative");
    require((node.flags & ~kSupportedNodeFlags) == 0u, "motion pose node flags are unsupported");
    require_motion_storage(motion);
    require(first_channel <= motion.channels.size(), "motion pose channel cursor is out of range");
    require_ordered_targets(motion);

    const std::array<float, 3u> translation = decode_bind_vector(
        node.translation_bits,
        "motion pose bind translation is nonfinite");
    const std::array<float, 3u> scale = decode_bind_vector(
        node.scale_bits,
        "motion pose bind scale is nonfinite");
    std::array<std::int32_t, 3u> euler = {{0, 0, 0}};
    NnQuaternion rotation = {{0.0f, 0.0f, 0.0f, 1.0f}};
    if ((node.flags & kUnitRotation) == 0u) {
        euler = node.rotation_a16;
        rotation = nn_quaternion_xyz(euler, precision);
    }

    NnMotionLocalPose result{
        translation,
        rotation,
        scale,
        0u,
        0u,
        0u,
        first_channel,
    };
    for (std::size_t channel_index = first_channel;
         channel_index < motion.channels.size();
         ++channel_index) {
        const NnMotionChannel& channel = motion.channels[channel_index];
        if (channel.target < node_id) {
            continue;
        }
        if (channel.target > node_id) {
            result.next_channel = channel_index;
            break;
        }
        if (channel.flags == 0u || !frame_is_in_channel_range(channel, frame)) {
            continue;
        }

        const NnMotionFrameResult mapped = nn_map_motion_frame(
            channel.interpolation_flags,
            decode_float(channel.start_key_bits),
            decode_float(channel.end_key_bits),
            frame,
            precision);
        if (!mapped.active) {
            continue;
        }

        const ChannelAxis axis = active_channel_axis(channel.flags);
        const Interpolation interpolation = active_interpolation(channel.interpolation_flags);
        if (axis.kind == ChannelKind::Rotation) {
            const std::vector<NnMotionA16Key> keys = nn_motion_a16_keys(motion, channel_index);
            const auto value = interpolation == Interpolation::Linear
                ? nn_evaluate_linear_a16(
                    channel.interpolation_flags,
                    decode_float(channel.start_key_bits),
                    decode_float(channel.end_key_bits),
                    frame,
                    keys.data(),
                    keys.size(),
                    precision)
                : nn_evaluate_constant_a16(
                    channel.interpolation_flags,
                    decode_float(channel.start_key_bits),
                    decode_float(channel.end_key_bits),
                    frame,
                    keys.data(),
                    keys.size(),
                    precision);
            if (!value.has_value()) {
                continue;
            }
            euler[axis.index] = static_cast<std::int32_t>(*value);
            result.rotation_flags = 1u;
            continue;
        }

        const std::vector<NnMotionFloatKey> keys = nn_motion_float_keys(motion, channel_index);
        const auto value = nn_evaluate_scalar_float(
            channel.interpolation_flags,
            decode_float(channel.start_key_bits),
            decode_float(channel.end_key_bits),
            frame,
            keys.data(),
            keys.size(),
            precision);
        if (!value.has_value()) {
            continue;
        }
        if (axis.kind == ChannelKind::Translation) {
            result.translation[axis.index] = *value;
            result.translation_flags = 1u;
        } else {
            result.scale[axis.index] = *value;
            result.scale_flags = 1u;
        }
    }

    if (result.rotation_flags == 1u) {
        result.rotation = nn_quaternion_xyz(euler, precision);
    }
    return result;
}

NnMotionLocalPose nn_link_motion_pose(
    const NnMotionLocalPose& primary,
    const NnMotionLocalPose& secondary,
    float weight,
    CameraPrecision precision) {
    require_precision(precision);
    if (weight >= 1.0f) {
        return secondary;
    }
    if (!(weight > 0.0f)) {
        return primary;
    }

    const Arithmetic arithmetic{precision};
    NnMotionLocalPose result = primary;
    result.translation[0u] = linked_component(
        primary.translation[0u], secondary.translation[0u], weight, arithmetic);
    result.translation[1u] = linked_component(
        primary.translation[1u], secondary.translation[1u], weight, arithmetic);
    result.translation[2u] = linked_component(
        primary.translation[2u], secondary.translation[2u], weight, arithmetic);
    result.scale[0u] = linked_component(
        primary.scale[0u], secondary.scale[0u], weight, arithmetic);
    result.scale[1u] = linked_component(
        primary.scale[1u], secondary.scale[1u], weight, arithmetic);
    result.scale[2u] = linked_component(
        primary.scale[2u], secondary.scale[2u], weight, arithmetic);
    result.rotation = nn_quaternion_slerp(primary.rotation, secondary.rotation, weight, precision);
    return result;
}
