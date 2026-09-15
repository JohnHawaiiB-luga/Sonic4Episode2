#include "nn_motion_pose.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <exception>
#include <initializer_list>
#include <limits>
#include <stdexcept>
#include <utility>
#include <vector>

namespace {

constexpr std::uint32_t kUnitTranslation = 0x00000001u;
constexpr std::uint32_t kUnitRotation = 0x00000002u;
constexpr std::uint32_t kUnitScale = 0x00000004u;
constexpr std::uint32_t kInterval = 0x00010000u;
constexpr std::uint32_t kClamp = 0x00020000u;
constexpr std::uint32_t kLinear = 0x00000002u;
constexpr std::uint32_t kConstant = 0x00000004u;

bool check(bool condition, const char* message) {
    if (condition) {
        return true;
    }
    std::fprintf(stderr, "%s\n", message);
    return false;
}

std::uint32_t float_bits(float value) {
    std::uint32_t bits = 0u;
    std::memcpy(&bits, &value, sizeof(bits));
    return bits;
}

bool has_float_vector_bits(
    const std::array<float, 3u>& value,
    const std::array<std::uint32_t, 3u>& expected) {
    for (std::size_t index = 0u; index < value.size(); ++index) {
        if (float_bits(value[index]) != expected[index]) {
            return false;
        }
    }
    return true;
}

bool same_quaternion(const NnQuaternion& left, const NnQuaternion& right) {
    for (std::size_t index = 0u; index < left.size(); ++index) {
        if (float_bits(left[index]) != float_bits(right[index])) {
            return false;
        }
    }
    return true;
}

bool has_quaternion_bits(
    const NnQuaternion& value,
    const std::array<std::uint32_t, 4u>& expected) {
    for (std::size_t index = 0u; index < value.size(); ++index) {
        if (float_bits(value[index]) != expected[index]) {
            return false;
        }
    }
    return true;
}

bool same_channel(const NnMotionChannel& left, const NnMotionChannel& right) {
    return left.flags == right.flags &&
           left.interpolation_flags == right.interpolation_flags &&
           left.target == right.target &&
           left.start_bits == right.start_bits &&
           left.end_bits == right.end_bits &&
           left.start_key_bits == right.start_key_bits &&
           left.end_key_bits == right.end_key_bits &&
           left.key_count == right.key_count &&
           left.key_stride == right.key_stride &&
           left.key_data_offset == right.key_data_offset;
}

bool same_motion(const NnMotionData& left, const NnMotionData& right) {
    if (left.flags != right.flags ||
        left.start_bits != right.start_bits ||
        left.end_bits != right.end_bits ||
        left.frame_rate_bits != right.frame_rate_bits ||
        left.reserved_bits != right.reserved_bits ||
        left.channel_count != right.channel_count ||
        left.key_data != right.key_data ||
        left.channels.size() != right.channels.size()) {
        return false;
    }
    for (std::size_t index = 0u; index < left.channels.size(); ++index) {
        if (!same_channel(left.channels[index], right.channels[index])) {
            return false;
        }
    }
    return true;
}

bool same_node(const NnNodeData& left, const NnNodeData& right) {
    return left.flags == right.flags &&
           left.matrix_index == right.matrix_index &&
           left.parent_index == right.parent_index &&
           left.child_index == right.child_index &&
           left.sibling_index == right.sibling_index &&
           left.translation_bits == right.translation_bits &&
           left.rotation_a16 == right.rotation_a16 &&
           left.scale_bits == right.scale_bits &&
           left.inverse_bind_bits == right.inverse_bind_bits &&
           left.opaque_bits == right.opaque_bits;
}

template <typename Callable>
bool expects_invalid_argument(Callable&& callable, const char* message) {
    try {
        std::forward<Callable>(callable)();
    } catch (const std::invalid_argument&) {
        return true;
    } catch (const std::exception&) {
        return check(false, message);
    }
    return check(false, message);
}

template <typename Callable>
bool expects_exception(Callable&& callable, const char* message) {
    try {
        std::forward<Callable>(callable)();
    } catch (const std::exception&) {
        return true;
    }
    return check(false, message);
}

void append_le16(std::vector<std::uint8_t>& data, std::uint16_t value) {
    data.push_back(static_cast<std::uint8_t>(value & 0xffu));
    data.push_back(static_cast<std::uint8_t>((value >> 8u) & 0xffu));
}

void append_le32(std::vector<std::uint8_t>& data, std::uint32_t value) {
    data.push_back(static_cast<std::uint8_t>(value & 0xffu));
    data.push_back(static_cast<std::uint8_t>((value >> 8u) & 0xffu));
    data.push_back(static_cast<std::uint8_t>((value >> 16u) & 0xffu));
    data.push_back(static_cast<std::uint8_t>((value >> 24u) & 0xffu));
}

std::size_t append_a16_keys(
    std::vector<std::uint8_t>& data,
    std::initializer_list<NnMotionA16Key> keys) {
    const std::size_t offset = data.size();
    for (const NnMotionA16Key& key : keys) {
        append_le16(data, static_cast<std::uint16_t>(key.frame));
        append_le16(data, static_cast<std::uint16_t>(key.value));
    }
    return offset;
}

std::size_t append_float_keys(
    std::vector<std::uint8_t>& data,
    std::initializer_list<std::pair<float, float>> keys) {
    const std::size_t offset = data.size();
    for (const std::pair<float, float>& key : keys) {
        append_le32(data, float_bits(key.first));
        append_le32(data, float_bits(key.second));
    }
    return offset;
}

NnMotionChannel make_channel(
    std::uint32_t flags,
    std::uint32_t interpolation_flags,
    std::int32_t target,
    float start,
    float end,
    float start_key,
    float end_key,
    std::uint32_t key_count,
    std::uint32_t key_stride,
    std::size_t key_data_offset) {
    return {
        flags,
        interpolation_flags,
        target,
        float_bits(start),
        float_bits(end),
        float_bits(start_key),
        float_bits(end_key),
        key_count,
        key_stride,
        key_data_offset,
    };
}

NnMotionData make_motion(
    std::vector<NnMotionChannel> channels,
    std::vector<std::uint8_t> key_data) {
    NnMotionData motion{};
    motion.channel_count = static_cast<std::uint32_t>(channels.size());
    motion.channels = std::move(channels);
    motion.key_data = std::move(key_data);
    return motion;
}

NnNodeData make_node(
    std::uint32_t flags,
    const std::array<std::uint32_t, 3u>& translation_bits,
    const std::array<std::int32_t, 3u>& rotation_a16,
    const std::array<std::uint32_t, 3u>& scale_bits) {
    NnNodeData node{};
    node.flags = flags;
    node.translation_bits = translation_bits;
    node.rotation_a16 = rotation_a16;
    node.scale_bits = scale_bits;
    return node;
}

bool test_bind_seeds_preserve_raw_vectors_and_rotation_mode() {
    NnMotionData empty = make_motion({}, {});
    const NnMotionData empty_before = empty;
    NnNodeData unit = make_node(
        kUnitTranslation | kUnitRotation | kUnitScale,
        {{0x80000000u, 0x00000001u, 0x40400000u}},
        {{16384, -8192, 4096}},
        {{0x3f000000u, 0x80000000u, 0x40000000u}});
    const NnNodeData unit_before = unit;

    for (CameraPrecision precision : {CameraPrecision::Single, CameraPrecision::Double}) {
        const NnMotionLocalPose pose = nn_motion_local_pose(unit, 0, empty, 0u, 0.0f, precision);
        if (!check(
                has_float_vector_bits(pose.translation, unit.translation_bits) &&
                    has_float_vector_bits(pose.scale, unit.scale_bits),
                "unit translation or scale flags changed bind vector bits") ||
            !check(
                has_quaternion_bits(pose.rotation, {{0x00000000u, 0x00000000u, 0x00000000u, 0x3f800000u}}),
                "unit rotation did not seed the identity quaternion") ||
            !check(
                pose.translation_flags == 0u &&
                    pose.rotation_flags == 0u &&
                    pose.scale_flags == 0u &&
                    pose.next_channel == 0u,
                "bind-only unit pose changed its flags or cursor")) {
            return false;
        }
    }

    NnNodeData nonunit = make_node(
        0u,
        {{0x3f800000u, 0x40000000u, 0x40400000u}},
        {{16384, -8192, 4096}},
        {{0x3f000000u, 0x3f800000u, 0x40000000u}});
    const NnNodeData nonunit_before = nonunit;
    for (CameraPrecision precision : {CameraPrecision::Single, CameraPrecision::Double}) {
        const NnMotionLocalPose pose = nn_motion_local_pose(nonunit, 0, empty, 0u, 0.0f, precision);
        if (!check(
                same_quaternion(pose.rotation, nn_quaternion_xyz(nonunit.rotation_a16, precision)),
                "nonunit rotation did not seed the XYZ bind quaternion") ||
            !check(
                pose.translation_flags == 0u &&
                    pose.rotation_flags == 0u &&
                    pose.scale_flags == 0u &&
                    pose.next_channel == 0u,
                "bind-only nonunit pose changed its flags or cursor")) {
            return false;
        }
    }

    return check(
        same_node(unit, unit_before) &&
            same_node(nonunit, nonunit_before) &&
            same_motion(empty, empty_before),
        "bind pose evaluation modified source data");
}

bool test_active_channels_follow_file_order_and_overwrite_components() {
    std::vector<std::uint8_t> key_data;
    const std::size_t translation_x_old = append_float_keys(key_data, {{0.0f, -1.0f}});
    const std::size_t translation_x_new = append_float_keys(key_data, {{0.0f, 9.0f}});
    const std::size_t translation_y = append_float_keys(key_data, {{0.0f, 2.0f}, {10.0f, 6.0f}});
    const std::size_t scale_z = append_float_keys(key_data, {{0.0f, 3.5f}});
    const std::size_t rotation_x_old = append_a16_keys(key_data, {{0, 100}});
    const std::size_t rotation_x_new = append_a16_keys(key_data, {{0, 1000}, {10, 3000}});
    const std::size_t rotation_y = append_a16_keys(key_data, {{0, -500}});

    std::vector<NnMotionChannel> channels;
    channels.push_back(make_channel(
        0x101u, kClamp | kConstant, 7, 0.0f, 10.0f, 0.0f, 10.0f, 1u, 8u, translation_x_old));
    channels.push_back(make_channel(
        0x101u, kClamp | kConstant, 7, 0.0f, 10.0f, 0.0f, 10.0f, 1u, 8u, translation_x_new));
    channels.push_back(make_channel(
        0x201u, kClamp | kLinear, 7, 0.0f, 10.0f, 0.0f, 10.0f, 2u, 8u, translation_y));
    channels.push_back(make_channel(
        0x20001u, kClamp | kConstant, 7, 0.0f, 10.0f, 0.0f, 10.0f, 1u, 8u, scale_z));
    channels.push_back(make_channel(
        0x812u, kClamp | kConstant, 7, 0.0f, 10.0f, 0.0f, 10.0f, 1u, 4u, rotation_x_old));
    channels.push_back(make_channel(
        0x812u, kClamp | kLinear, 7, 0.0f, 10.0f, 0.0f, 10.0f, 2u, 4u, rotation_x_new));
    channels.push_back(make_channel(
        0x1012u, kClamp | kConstant, 7, 0.0f, 10.0f, 0.0f, 10.0f, 1u, 4u, rotation_y));
    NnMotionData motion = make_motion(std::move(channels), std::move(key_data));
    const NnMotionData motion_before = motion;
    NnNodeData node = make_node(
        kUnitTranslation | kUnitRotation | kUnitScale,
        {{0x3f800000u, 0x40000000u, 0x80000000u}},
        {{0, 0, 0}},
        {{0x3f000000u, 0x40000000u, 0x40400000u}});
    const NnNodeData node_before = node;
    const std::array<std::uint32_t, 3u> expected_translation = {{
        float_bits(9.0f), float_bits(4.0f), 0x80000000u,
    }};
    const std::array<std::uint32_t, 3u> expected_scale = {{
        0x3f000000u, 0x40000000u, float_bits(3.5f),
    }};
    const std::array<std::int32_t, 3u> expected_euler = {{2000, -500, 0}};

    for (CameraPrecision precision : {CameraPrecision::Single, CameraPrecision::Double}) {
        const NnMotionLocalPose pose = nn_motion_local_pose(node, 7, motion, 0u, 5.0f, precision);
        if (!check(
                has_float_vector_bits(pose.translation, expected_translation) &&
                    has_float_vector_bits(pose.scale, expected_scale),
                "active scalar channels did not overwrite the expected components") ||
            !check(
                same_quaternion(pose.rotation, nn_quaternion_xyz(expected_euler, precision)),
                "active A16 channels did not overwrite the expected Euler components") ||
            !check(
                pose.translation_flags == 1u &&
                    pose.rotation_flags == 1u &&
                    pose.scale_flags == 1u &&
                    pose.next_channel == 0u,
                "active channels changed flags or exhaustion cursor incorrectly")) {
            return false;
        }
    }

    return check(
        same_node(node, node_before) && same_motion(motion, motion_before),
        "active pose evaluation modified source data");
}

bool test_target_order_controls_early_stop_and_exhaustion_cursor() {
    std::vector<std::uint8_t> key_data;
    const std::size_t target_three_keys = append_float_keys(key_data, {{0.0f, 11.0f}});
    std::vector<NnMotionChannel> early_channels;
    early_channels.push_back(make_channel(0u, 0u, 1, 0.0f, 1.0f, 0.0f, 1.0f, 0u, 0u, 0u));
    early_channels.push_back(make_channel(
        0x101u, kClamp | kConstant, 3, 0.0f, 1.0f, 0.0f, 1.0f, 1u, 8u, target_three_keys));
    early_channels.push_back(make_channel(0u, 0u, 4, 0.0f, 1.0f, 0.0f, 1.0f, 0u, 0u, 0u));
    NnMotionData early_motion = make_motion(std::move(early_channels), key_data);
    const NnMotionData early_before = early_motion;
    NnNodeData node = make_node(
        kUnitRotation,
        {{0x3f800000u, 0x40000000u, 0x40400000u}},
        {{0, 0, 0}},
        {{0x3f800000u, 0x3f800000u, 0x3f800000u}});
    const NnNodeData node_before = node;

    const NnMotionLocalPose early = nn_motion_local_pose(
        node, 3, early_motion, 0u, 0.0f, CameraPrecision::Single);
    if (!check(
            float_bits(early.translation[0u]) == float_bits(11.0f) &&
                early.translation_flags == 1u && early.next_channel == 2u,
            "target ordering did not skip lower targets and stop at the first greater target")) {
        return false;
    }

    std::vector<NnMotionChannel> exhausted_channels;
    exhausted_channels.push_back(make_channel(0u, 0u, 1, 0.0f, 1.0f, 0.0f, 1.0f, 0u, 0u, 0u));
    exhausted_channels.push_back(make_channel(
        0x101u, kClamp | kConstant, 3, 0.0f, 1.0f, 0.0f, 1.0f, 1u, 8u, target_three_keys));
    NnMotionData exhausted_motion = make_motion(std::move(exhausted_channels), std::move(key_data));
    const NnMotionData exhausted_before = exhausted_motion;
    const NnMotionLocalPose exhausted = nn_motion_local_pose(
        node, 3, exhausted_motion, 0u, 0.0f, CameraPrecision::Double);
    const NnMotionLocalPose offset_exhausted = nn_motion_local_pose(
        node, 3, exhausted_motion, 1u, 0.0f, CameraPrecision::Double);
    const NnMotionLocalPose end_cursor = nn_motion_local_pose(
        node,
        3,
        exhausted_motion,
        exhausted_motion.channels.size(),
        0.0f,
        CameraPrecision::Double);

    return check(
               exhausted.next_channel == 0u && offset_exhausted.next_channel == 1u,
               "channel-list exhaustion did not preserve the original cursor") &&
           check(
               end_cursor.next_channel == exhausted_motion.channels.size() &&
                   end_cursor.translation_flags == 0u,
               "end cursor did not return the bind pose and original cursor") &&
           check(
               same_node(node, node_before) &&
                   same_motion(early_motion, early_before) &&
                   same_motion(exhausted_motion, exhausted_before),
               "cursor evaluation modified source data");
}

bool test_frame_gates_and_mapped_inactivity_leave_key_storage_unread() {
    NnNodeData node = make_node(
        kUnitRotation,
        {{0x3f800000u, 0x40000000u, 0x40400000u}},
        {{0, 0, 0}},
        {{0x3f800000u, 0x3f800000u, 0x3f800000u}});
    const NnNodeData node_before = node;

    NnMotionData outer_inactive = make_motion(
        {make_channel(
            0xdeadbeefu, 0xffffffffu, 5, 3.0f, 5.0f, 0.0f, 1.0f, 0u, 0u, 0u)},
        {});
    const NnMotionData outer_inactive_before = outer_inactive;
    const NnMotionLocalPose outer_pose = nn_motion_local_pose(
        node, 5, outer_inactive, 0u, 2.0f, CameraPrecision::Single);

    NnMotionData mapped_inactive = make_motion(
        {make_channel(
            0x101u, kInterval | kConstant, 5, 0.0f, 10.0f, 0.0f, 10.0f, 0u, 0u, 0u)},
        {});
    const NnMotionData mapped_inactive_before = mapped_inactive;
    const NnMotionLocalPose mapped_pose = nn_motion_local_pose(
        node, 5, mapped_inactive, 0u, 10.0f, CameraPrecision::Double);

    NnMotionData zero_channel = make_motion(
        {make_channel(0u, 0xffffffffu, 5, 0.0f, 1.0f, 0.0f, 1.0f, 0u, 0u, 0u)},
        {});
    const NnMotionData zero_channel_before = zero_channel;
    const NnMotionLocalPose zero_pose = nn_motion_local_pose(
        node, 5, zero_channel, 0u, 0.0f, CameraPrecision::Single);

    return check(
               outer_pose.translation_flags == 0u &&
                   outer_pose.rotation_flags == 0u &&
                   outer_pose.scale_flags == 0u &&
                   outer_pose.next_channel == 0u,
               "outer-inactive channel changed the bind pose") &&
           check(
               mapped_pose.translation_flags == 0u &&
                   mapped_pose.rotation_flags == 0u &&
                   mapped_pose.scale_flags == 0u &&
                   mapped_pose.next_channel == 0u,
               "mapped-inactive channel changed the bind pose") &&
           check(
               zero_pose.translation_flags == 0u &&
                   zero_pose.rotation_flags == 0u &&
                   zero_pose.scale_flags == 0u &&
                   zero_pose.next_channel == 0u,
               "zero-flag channel changed the bind pose") &&
           check(
               same_node(node, node_before) &&
                   same_motion(outer_inactive, outer_inactive_before) &&
                   same_motion(mapped_inactive, mapped_inactive_before) &&
                   same_motion(zero_channel, zero_channel_before),
               "inactive pose evaluation modified source data");
}

bool test_mapped_inactive_unsupported_channels_do_not_dispatch() {
    const NnNodeData node = make_node(
        kUnitRotation,
        {{0x3f800000u, 0x40000000u, 0x40400000u}},
        {{0, 0, 0}},
        {{0x3f800000u, 0x3f800000u, 0x3f800000u}});
    std::array<NnMotionData, 2u> motions = {{
        make_motion(
            {make_channel(0xdeadbeefu, kInterval | kConstant, 5,
                          0.0f, 10.0f, 0.0f, 10.0f, 0u, 0u, 0u)}, {}),
        make_motion(
            {make_channel(0x101u, kInterval | 0x8u, 5,
                          0.0f, 10.0f, 0.0f, 10.0f, 0u, 0u, 0u)}, {}),
    }};
    const auto before = motions;
    for (CameraPrecision precision : {CameraPrecision::Single, CameraPrecision::Double}) {
        for (const auto& motion : motions) {
            try {
                const auto pose = nn_motion_local_pose(node, 5, motion, 0u, 10.0f, precision);
                if (!check(
                        has_float_vector_bits(pose.translation, node.translation_bits) &&
                            has_float_vector_bits(pose.scale, node.scale_bits) &&
                            has_quaternion_bits(pose.rotation, {{0u, 0u, 0u, 0x3f800000u}}) &&
                            pose.translation_flags == 0u && pose.rotation_flags == 0u &&
                            pose.scale_flags == 0u && pose.next_channel == 0u,
                        "mapped-inactive unsupported channel changed the bind pose")) {
                    return false;
                }
            } catch (const std::exception&) {
                return check(false, "mapped-inactive unsupported channel dispatched before frame gating");
            }
        }
    }
    return check(same_motion(motions[0u], before[0u]) && same_motion(motions[1u], before[1u]),
                 "mapped-inactive unsupported channel modified source data");
}

bool test_rejects_invalid_or_active_malformed_inputs() {
    NnNodeData node = make_node(
        kUnitRotation,
        {{0x3f800000u, 0x40000000u, 0x40400000u}},
        {{0, 0, 0}},
        {{0x3f800000u, 0x3f800000u, 0x3f800000u}});
    const NnNodeData node_before = node;
    NnMotionData empty = make_motion({}, {});
    const NnMotionData empty_before = empty;

    NnMotionData inconsistent = empty;
    inconsistent.channel_count = 1u;
    NnNodeData unsupported_node = node;
    unsupported_node.flags = 0x00800000u;
    NnNodeData nonfinite_bind = node;
    nonfinite_bind.flags |= kUnitTranslation | kUnitScale;
    nonfinite_bind.translation_bits[1u] = 0x7fc00001u;

    NnMotionData unordered = make_motion(
        {
            make_channel(0u, 0u, 2, 0.0f, 1.0f, 0.0f, 1.0f, 0u, 0u, 0u),
            make_channel(0u, 0u, 1, 0.0f, 1.0f, 0.0f, 1.0f, 0u, 0u, 0u),
        },
        {});
    const NnMotionData unordered_before = unordered;
    NnMotionData unsupported_type = make_motion(
        {make_channel(0x55u, kClamp | kConstant, 0, 0.0f, 1.0f, 0.0f, 1.0f, 0u, 0u, 0u)},
        {});
    const NnMotionData unsupported_type_before = unsupported_type;
    NnMotionData unsupported_interpolation = make_motion(
        {make_channel(0x101u, kClamp | 0x8u, 0, 0.0f, 1.0f, 0.0f, 1.0f, 0u, 0u, 0u)},
        {});
    const NnMotionData unsupported_interpolation_before = unsupported_interpolation;
    NnMotionData malformed_keys = make_motion(
        {make_channel(0x101u, kClamp | kConstant, 0, 0.0f, 1.0f, 0.0f, 1.0f, 0u, 0u, 0u)},
        {});
    const NnMotionData malformed_keys_before = malformed_keys;

    return expects_invalid_argument(
               [&]() {
                   static_cast<void>(nn_motion_local_pose(
                       node, 0, empty, 0u, 0.0f, static_cast<CameraPrecision>(99)));
               },
               "local pose accepted an unsupported precision") &&
           expects_invalid_argument(
               [&]() {
                   static_cast<void>(nn_motion_local_pose(
                       node,
                       0,
                       empty,
                       0u,
                       std::numeric_limits<float>::quiet_NaN(),
                       CameraPrecision::Single));
               },
               "local pose accepted a nonfinite frame") &&
           expects_invalid_argument(
               [&]() {
                   static_cast<void>(nn_motion_local_pose(node, -1, empty, 0u, 0.0f, CameraPrecision::Single));
               },
               "local pose accepted a negative node ID") &&
           expects_invalid_argument(
               [&]() {
                   static_cast<void>(nn_motion_local_pose(
                       node, 0, inconsistent, 0u, 0.0f, CameraPrecision::Single));
               },
               "local pose accepted inconsistent channel storage") &&
           expects_invalid_argument(
               [&]() {
                   static_cast<void>(nn_motion_local_pose(node, 0, empty, 1u, 0.0f, CameraPrecision::Single));
               },
               "local pose accepted a cursor beyond channel storage") &&
           expects_invalid_argument(
               [&]() {
                   static_cast<void>(nn_motion_local_pose(
                       unsupported_node, 0, empty, 0u, 0.0f, CameraPrecision::Single));
               },
               "local pose accepted unsupported node flags") &&
           expects_invalid_argument(
               [&]() {
                   static_cast<void>(nn_motion_local_pose(
                       nonfinite_bind, 0, empty, 0u, 0.0f, CameraPrecision::Single));
               },
               "local pose accepted nonfinite bind vectors") &&
           expects_invalid_argument(
               [&]() {
                   static_cast<void>(nn_motion_local_pose(node, 0, unordered, 0u, 0.0f, CameraPrecision::Single));
               },
               "local pose accepted unordered signed target IDs") &&
           expects_invalid_argument(
               [&]() {
                   static_cast<void>(nn_motion_local_pose(
                       node, 0, unsupported_type, 0u, 0.0f, CameraPrecision::Single));
               },
               "local pose accepted an unsupported active channel type") &&
           expects_invalid_argument(
               [&]() {
                   static_cast<void>(nn_motion_local_pose(
                       node, 0, unsupported_interpolation, 0u, 0.0f, CameraPrecision::Single));
               },
               "local pose accepted unsupported active interpolation") &&
           expects_exception(
               [&]() {
                   static_cast<void>(nn_motion_local_pose(
                       node, 0, malformed_keys, 0u, 0.0f, CameraPrecision::Single));
               },
               "local pose accepted malformed active key storage") &&
           check(
               same_node(node, node_before) &&
                   same_motion(empty, empty_before) &&
                   same_motion(unordered, unordered_before) &&
                   same_motion(unsupported_type, unsupported_type_before) &&
                   same_motion(unsupported_interpolation, unsupported_interpolation_before) &&
                   same_motion(malformed_keys, malformed_keys_before),
               "rejected local pose input modified source data");
}

}

int main() {
    return test_bind_seeds_preserve_raw_vectors_and_rotation_mode() &&
                   test_active_channels_follow_file_order_and_overwrite_components() &&
                   test_target_order_controls_early_stop_and_exhaustion_cursor() &&
                   test_frame_gates_and_mapped_inactivity_leave_key_storage_unread() &&
                   test_mapped_inactive_unsupported_channels_do_not_dispatch() &&
                   test_rejects_invalid_or_active_malformed_inputs()
        ? 0
        : 1;
}
