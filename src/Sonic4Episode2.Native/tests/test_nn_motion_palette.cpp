#include "nn_motion_palette.h"

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <limits>
#include <stdexcept>
#include <utility>
#include <vector>

namespace {

bool check(bool condition, const char* message) {
    if (!condition) {
        std::fprintf(stderr, "%s\n", message);
    }
    return condition;
}

std::uint32_t float_bits(float value) {
    std::uint32_t result = 0u;
    std::memcpy(&result, &value, sizeof(result));
    return result;
}

float float_from_bits(std::uint32_t bits) {
    float result = 0.0f;
    std::memcpy(&result, &bits, sizeof(result));
    return result;
}

CameraMatrix identity_matrix() {
    return {{
        1.0f, 0.0f, 0.0f, 0.0f,
        0.0f, 1.0f, 0.0f, 0.0f,
        0.0f, 0.0f, 1.0f, 0.0f,
        0.0f, 0.0f, 0.0f, 1.0f,
    }};
}

CameraMatrix translation_matrix(float x, float y, float z) {
    CameraMatrix result = identity_matrix();
    result[12u] = x;
    result[13u] = y;
    result[14u] = z;
    return result;
}

CameraMatrix multiply_matrices(const CameraMatrix& left, const CameraMatrix& right) {
    CameraMatrix result{};
    for (std::size_t row = 0u; row < 4u; ++row) {
        for (std::size_t column = 0u; column < 4u; ++column) {
            double sum = 0.0;
            for (std::size_t index = 0u; index < 4u; ++index) {
                sum += static_cast<double>(left[row * 4u + index]) *
                       static_cast<double>(right[index * 4u + column]);
            }
            result[row * 4u + column] = static_cast<float>(sum);
        }
    }
    return result;
}

CameraMatrix quaternion_matrix(const NnQuaternion& quaternion) {
    const float x = quaternion[0u];
    const float y = quaternion[1u];
    const float z = quaternion[2u];
    const float w = quaternion[3u];
    return {{
        1.0f - 2.0f * y * y - 2.0f * z * z,
        2.0f * x * y + 2.0f * z * w,
        2.0f * x * z - 2.0f * y * w,
        0.0f,
        2.0f * x * y - 2.0f * z * w,
        1.0f - 2.0f * x * x - 2.0f * z * z,
        2.0f * y * z + 2.0f * x * w,
        0.0f,
        2.0f * x * z + 2.0f * y * w,
        2.0f * y * z - 2.0f * x * w,
        1.0f - 2.0f * x * x - 2.0f * y * y,
        0.0f,
        0.0f, 0.0f, 0.0f, 1.0f,
    }};
}

bool same_matrix_bits(const CameraMatrix& left, const CameraMatrix& right) {
    for (std::size_t index = 0u; index < left.size(); ++index) {
        if (float_bits(left[index]) != float_bits(right[index])) {
            return false;
        }
    }
    return true;
}

std::array<std::uint32_t, 16u> matrix_bits(const CameraMatrix& matrix) {
    std::array<std::uint32_t, 16u> result{};
    for (std::size_t index = 0u; index < result.size(); ++index) {
        result[index] = float_bits(matrix[index]);
    }
    return result;
}

NnNodeData make_node(
    std::uint32_t flags,
    std::int16_t matrix_index,
    std::int16_t parent_index,
    std::int16_t child_index,
    std::int16_t sibling_index,
    const CameraMatrix& inverse_bind) {
    NnNodeData result{};
    result.flags = flags;
    result.matrix_index = matrix_index;
    result.parent_index = parent_index;
    result.child_index = child_index;
    result.sibling_index = sibling_index;
    result.translation_bits = {{
        float_bits(3.0f), float_bits(-5.0f), float_bits(7.0f),
    }};
    result.rotation_a16 = {{17, -19, 23}};
    result.scale_bits = {{
        float_bits(11.0f), float_bits(13.0f), float_bits(17.0f),
    }};
    result.inverse_bind_bits = matrix_bits(inverse_bind);
    return result;
}

NnModelData make_model(std::vector<NnNodeData> nodes, std::uint32_t matrix_palette_count) {
    NnModelData result{};
    result.node_count = static_cast<std::uint32_t>(nodes.size());
    result.matrix_palette_count = matrix_palette_count;
    result.nodes = std::move(nodes);
    return result;
}

NnMotionLocalPose make_pose(
    const std::array<float, 3u>& translation,
    const NnQuaternion& rotation,
    const std::array<float, 3u>& scale) {
    return {
        translation,
        rotation,
        scale,
        0x11111111u,
        0x22222222u,
        0x33333333u,
        std::numeric_limits<std::size_t>::max(),
    };
}

bool same_pose(const NnMotionLocalPose& left, const NnMotionLocalPose& right) {
    for (std::size_t index = 0u; index < 3u; ++index) {
        if (float_bits(left.translation[index]) != float_bits(right.translation[index]) ||
            float_bits(left.scale[index]) != float_bits(right.scale[index])) {
            return false;
        }
    }
    for (std::size_t index = 0u; index < 4u; ++index) {
        if (float_bits(left.rotation[index]) != float_bits(right.rotation[index])) {
            return false;
        }
    }
    return left.translation_flags == right.translation_flags &&
           left.rotation_flags == right.rotation_flags &&
           left.scale_flags == right.scale_flags &&
           left.next_channel == right.next_channel;
}

class MatrixBackend final : public CameraMatrixBackend {
public:
    mutable std::vector<NnQuaternion> rotations;
    mutable std::vector<CameraMatrix> left_inputs;
    mutable std::vector<CameraMatrix> right_inputs;
    mutable std::vector<CameraPrecision> precisions;
    bool return_nonfinite_rotation = false;
    bool return_nonaffine_multiply = false;

    CameraMatrix rotation_z(float, CameraPrecision) const override {
        return identity_matrix();
    }

    CameraMatrix rotation_quaternion(
        const std::array<float, 4u>& rotation, CameraPrecision precision) const override {
        rotations.push_back(rotation);
        precisions.push_back(precision);
        CameraMatrix result = quaternion_matrix(rotation);
        if (return_nonfinite_rotation) {
            result[0u] = float_from_bits(0x7fc00001u);
        }
        return result;
    }

    CameraMatrix multiply(
        const CameraMatrix& left, const CameraMatrix& right, CameraPrecision precision) const override {
        left_inputs.push_back(left);
        right_inputs.push_back(right);
        precisions.push_back(precision);
        CameraMatrix result = multiply_matrices(left, right);
        if (return_nonaffine_multiply) {
            result[3u] = 1.0f;
        }
        return result;
    }

    CameraMatrix perspective_fov_rh(const CameraProjection&, CameraPrecision) const override {
        return identity_matrix();
    }
};

template <typename Callable>
bool expects_invalid_argument(Callable&& callable, const char* message) {
    try {
        std::forward<Callable>(callable)();
    } catch (const std::invalid_argument&) {
        return true;
    } catch (...) {
        std::fprintf(stderr, "%s (wrong exception type)\n", message);
        return false;
    }
    std::fprintf(stderr, "%s\n", message);
    return false;
}

bool test_animation_ignores_bind_unit_bits_and_stale_inverse_bind() {
    const CameraMatrix base = {{
        2.0f, 0.0f, 0.0f, 0.0f,
        0.0f, 3.0f, 0.0f, 0.0f,
        0.0f, 0.0f, 4.0f, 0.0f,
        10.0f, 20.0f, 30.0f, 1.0f,
    }};
    NnNodeData node = make_node(0x0000000fu, 0, -1, -1, -1, identity_matrix());
    node.translation_bits.fill(0x7fc00001u);
    node.rotation_a16 = {{std::numeric_limits<std::int32_t>::min(), -1, 1}};
    node.scale_bits.fill(0x7fc00002u);
    node.inverse_bind_bits.fill(0x7fc00003u);
    NnModelData model = make_model({node}, 1u);
    std::vector<NnMotionLocalPose> poses = {
        make_pose({{1.0f, 2.0f, 3.0f}}, {{0.0f, 0.0f, 1.0f, 0.0f}}, {{2.0f, 3.0f, 4.0f}}),
    };
    const NnNodeData saved_node = model.nodes[0u];
    const std::vector<NnMotionLocalPose> saved_poses = poses;
    const CameraMatrix expected = {{
        -4.0f, 0.0f, 0.0f, 0.0f,
        0.0f, -9.0f, 0.0f, 0.0f,
        0.0f, 0.0f, 16.0f, 0.0f,
        12.0f, 26.0f, 42.0f, 1.0f,
    }};

    for (CameraPrecision precision : {CameraPrecision::Single, CameraPrecision::Double}) {
        MatrixBackend backend;
        const NnMotionNodeMatrices result = nn_motion_node_matrices(
            model, poses, base, precision, backend);
        if (!check(
                result.node_world.size() == 1u && result.palette.size() == 1u &&
                    same_matrix_bits(result.node_world[0u], expected) &&
                    same_matrix_bits(result.palette[0u], expected),
                "animated T/R/S did not replace bind-unit fields") ||
            !check(
                backend.rotations == std::vector<NnQuaternion>({poses[0u].rotation}) &&
                    backend.left_inputs.size() == 1u && backend.right_inputs.size() == 1u,
                "unit inverse-bind path did not apply exactly one quaternion matrix multiply")) {
            return false;
        }
    }

    return check(
        model.nodes[0u].flags == saved_node.flags &&
            model.nodes[0u].translation_bits == saved_node.translation_bits &&
            model.nodes[0u].rotation_a16 == saved_node.rotation_a16 &&
            model.nodes[0u].scale_bits == saved_node.scale_bits &&
            model.nodes[0u].inverse_bind_bits == saved_node.inverse_bind_bits &&
            poses.size() == saved_poses.size() && same_pose(poses[0u], saved_poses[0u]),
        "motion palette mutated model or pose input");
}

bool test_parent_child_sibling_worlds_and_palette_slots() {
    const NnModelData model = make_model({
        make_node(0x0000000fu, 2, -1, 1, -1, identity_matrix()),
        make_node(0x0000000fu, 0, 0, 2, 3, identity_matrix()),
        make_node(0x0000000fu, 1, 1, -1, -1, identity_matrix()),
        make_node(0x0000000fu, 3, 0, -1, -1, identity_matrix()),
    }, 4u);
    const std::vector<NnMotionLocalPose> poses = {
        make_pose({{10.0f, 0.0f, 0.0f}}, {{0.0f, 0.0f, 0.0f, 1.0f}}, {{1.0f, 1.0f, 1.0f}}),
        make_pose({{0.0f, 5.0f, 0.0f}}, {{0.0f, 0.0f, 0.0f, 1.0f}}, {{1.0f, 1.0f, 1.0f}}),
        make_pose({{0.0f, 0.0f, 7.0f}}, {{0.0f, 0.0f, 0.0f, 1.0f}}, {{1.0f, 1.0f, 1.0f}}),
        make_pose({{0.0f, -5.0f, 0.0f}}, {{0.0f, 0.0f, 0.0f, 1.0f}}, {{1.0f, 1.0f, 1.0f}}),
    };
    MatrixBackend backend;
    const NnMotionNodeMatrices result = nn_motion_node_matrices(
        model, poses, identity_matrix(), CameraPrecision::Double, backend);

    return check(result.node_world.size() == 4u && result.palette.size() == 4u,
                 "animated hierarchy output sizes are incorrect") &&
           check(same_matrix_bits(result.node_world[0u], translation_matrix(10.0f, 0.0f, 0.0f)),
                 "animated root world mismatch") &&
           check(same_matrix_bits(result.node_world[1u], translation_matrix(10.0f, 5.0f, 0.0f)),
                 "animated child world mismatch") &&
           check(same_matrix_bits(result.node_world[2u], translation_matrix(10.0f, 5.0f, 7.0f)),
                 "animated grandchild world mismatch") &&
           check(same_matrix_bits(result.node_world[3u], translation_matrix(10.0f, -5.0f, 0.0f)),
                 "animated sibling inherited the wrong parent world") &&
           check(same_matrix_bits(result.palette[0u], result.node_world[1u]) &&
                     same_matrix_bits(result.palette[1u], result.node_world[2u]) &&
                     same_matrix_bits(result.palette[2u], result.node_world[0u]) &&
                     same_matrix_bits(result.palette[3u], result.node_world[3u]),
                 "animated palette did not preserve matrix-slot ownership");
}

bool test_inverse_bind_multiplies_world_after_capture() {
    const CameraMatrix inverse_bind = translation_matrix(-7.0f, -8.0f, -9.0f);
    const NnModelData model = make_model({
        make_node(0x00000007u, 0, -1, -1, -1, inverse_bind),
    }, 1u);
    const std::vector<NnMotionLocalPose> poses = {
        make_pose({{7.0f, 8.0f, 9.0f}}, {{0.0f, 0.0f, 0.0f, 1.0f}}, {{1.0f, 1.0f, 1.0f}}),
    };
    MatrixBackend backend;
    const NnMotionNodeMatrices result = nn_motion_node_matrices(
        model, poses, identity_matrix(), CameraPrecision::Single, backend);

    return check(
               same_matrix_bits(result.node_world[0u], translation_matrix(7.0f, 8.0f, 9.0f)) &&
                   same_matrix_bits(result.palette[0u], identity_matrix()),
               "inverse-bind palette result did not preserve the captured node world") &&
           check(
               backend.left_inputs.size() == 2u && backend.right_inputs.size() == 2u &&
                   same_matrix_bits(backend.left_inputs[1u], inverse_bind) &&
                   same_matrix_bits(backend.right_inputs[1u], result.node_world[0u]),
               "inverse-bind multiplication did not use inverse-bind times world");
}

bool test_slotless_node_keeps_inverse_bind_unread() {
    NnNodeData root = make_node(0x00000007u, -1, -1, 1, -1, identity_matrix());
    root.inverse_bind_bits.fill(0x7fc00001u);
    const NnModelData model = make_model({
        root,
        make_node(0x0000000fu, 0, 0, -1, -1, identity_matrix()),
    }, 1u);
    const std::vector<NnMotionLocalPose> poses = {
        make_pose({{1.0f, 0.0f, 0.0f}}, {{0.0f, 0.0f, 0.0f, 1.0f}}, {{1.0f, 1.0f, 1.0f}}),
        make_pose({{0.0f, 2.0f, 0.0f}}, {{0.0f, 0.0f, 0.0f, 1.0f}}, {{1.0f, 1.0f, 1.0f}}),
    };
    MatrixBackend backend;
    const NnMotionNodeMatrices result = nn_motion_node_matrices(
        model, poses, identity_matrix(), CameraPrecision::Single, backend);

    return check(
               result.palette.size() == 1u &&
                   same_matrix_bits(result.node_world[0u], translation_matrix(1.0f, 0.0f, 0.0f)) &&
                   same_matrix_bits(result.node_world[1u], translation_matrix(1.0f, 2.0f, 0.0f)) &&
                   same_matrix_bits(result.palette[0u], result.node_world[1u]),
               "slotless animated node did not carry world into its child") &&
           check(
               backend.left_inputs.size() == 2u && backend.right_inputs.size() == 2u,
               "slotless node read its inactive inverse bind");
}

NnModelData valid_model() {
    return make_model({
        make_node(0x0000000fu, 0, -1, -1, -1, identity_matrix()),
    }, 1u);
}

std::vector<NnMotionLocalPose> valid_poses() {
    return {
        make_pose({{0.0f, 0.0f, 0.0f}}, {{0.0f, 0.0f, 0.0f, 1.0f}}, {{1.0f, 1.0f, 1.0f}}),
    };
}

NnModelData make_chain(std::size_t depth) {
    std::vector<NnNodeData> nodes;
    nodes.reserve(depth);
    for (std::size_t index = 0u; index < depth; ++index) {
        const std::int16_t node_index = static_cast<std::int16_t>(index);
        const std::int16_t parent_index = index == 0u
            ? static_cast<std::int16_t>(-1)
            : static_cast<std::int16_t>(index - 1u);
        const std::int16_t child_index = index + 1u == depth
            ? static_cast<std::int16_t>(-1)
            : static_cast<std::int16_t>(index + 1u);
        nodes.push_back(make_node(
            0x0000000fu,
            node_index,
            parent_index,
            child_index,
            -1,
            identity_matrix()));
    }
    return make_model(std::move(nodes), static_cast<std::uint32_t>(depth));
}

bool test_rejects_invalid_inputs_and_topology() {
    MatrixBackend backend;
    if (!expects_invalid_argument(
            [&]() {
                nn_motion_node_matrices(
                    valid_model(), valid_poses(), identity_matrix(), static_cast<CameraPrecision>(99), backend);
            },
            "motion palette accepted unsupported precision")) {
        return false;
    }

    if (!expects_invalid_argument(
            [&]() {
                nn_motion_node_matrices(
                    valid_model(), {}, identity_matrix(), CameraPrecision::Single, backend);
            },
            "motion palette accepted a pose-count mismatch")) {
        return false;
    }

    NnModelData count_mismatch = valid_model();
    count_mismatch.node_count = 2u;
    if (!expects_invalid_argument(
            [&]() {
                nn_motion_node_matrices(
                    count_mismatch, valid_poses(), identity_matrix(), CameraPrecision::Single, backend);
            },
            "motion palette accepted a node-count mismatch")) {
        return false;
    }

    NnModelData excessive_slots = valid_model();
    excessive_slots.matrix_palette_count = 2u;
    if (!expects_invalid_argument(
            [&]() {
                nn_motion_node_matrices(
                    excessive_slots, valid_poses(), identity_matrix(), CameraPrecision::Single, backend);
            },
            "motion palette accepted more palette slots than nodes")) {
        return false;
    }

    NnModelData unfilled_slot = valid_model();
    unfilled_slot.nodes[0u].matrix_index = -1;
    if (!expects_invalid_argument(
            [&]() {
                nn_motion_node_matrices(
                    unfilled_slot, valid_poses(), identity_matrix(), CameraPrecision::Single, backend);
            },
            "motion palette accepted an unfilled matrix slot")) {
        return false;
    }

    NnModelData unsupported_flags = valid_model();
    unsupported_flags.nodes[0u].flags = 0x00000010u;
    if (!expects_invalid_argument(
            [&]() {
                nn_motion_node_matrices(
                    unsupported_flags, valid_poses(), identity_matrix(), CameraPrecision::Single, backend);
            },
            "motion palette accepted unsupported node flags")) {
        return false;
    }

    std::vector<NnMotionLocalPose> nonfinite_translation = valid_poses();
    nonfinite_translation[0u].translation[0u] = float_from_bits(0x7fc00001u);
    if (!expects_invalid_argument(
            [&]() {
                nn_motion_node_matrices(
                    valid_model(), nonfinite_translation, identity_matrix(), CameraPrecision::Single, backend);
            },
            "motion palette accepted nonfinite translation")) {
        return false;
    }

    std::vector<NnMotionLocalPose> nonfinite_rotation = valid_poses();
    nonfinite_rotation[0u].rotation[2u] = float_from_bits(0x7fc00001u);
    if (!expects_invalid_argument(
            [&]() {
                nn_motion_node_matrices(
                    valid_model(), nonfinite_rotation, identity_matrix(), CameraPrecision::Single, backend);
            },
            "motion palette accepted nonfinite quaternion input")) {
        return false;
    }

    std::vector<NnMotionLocalPose> oversized_scale = valid_poses();
    oversized_scale[0u].scale[1u] = 33554432.0f;
    if (!expects_invalid_argument(
            [&]() {
                nn_motion_node_matrices(
                    valid_model(), oversized_scale, identity_matrix(), CameraPrecision::Single, backend);
            },
            "motion palette accepted out-of-range scale")) {
        return false;
    }

    std::vector<NnMotionLocalPose> oversized_quaternion = valid_poses();
    oversized_quaternion[0u].rotation[3u] = 33554432.0f;
    if (!expects_invalid_argument(
            [&]() {
                nn_motion_node_matrices(
                    valid_model(), oversized_quaternion, identity_matrix(), CameraPrecision::Single, backend);
            },
            "motion palette accepted out-of-range quaternion input")) {
        return false;
    }

    NnModelData active_nonfinite_inverse = valid_model();
    active_nonfinite_inverse.nodes[0u].flags = 0x00000007u;
    active_nonfinite_inverse.nodes[0u].inverse_bind_bits[0u] = 0x7fc00001u;
    if (!expects_invalid_argument(
            [&]() {
                nn_motion_node_matrices(
                    active_nonfinite_inverse, valid_poses(), identity_matrix(), CameraPrecision::Single, backend);
            },
            "motion palette accepted nonfinite active inverse bind")) {
        return false;
    }

    CameraMatrix nonaffine_base = identity_matrix();
    nonaffine_base[3u] = 1.0f;
    if (!expects_invalid_argument(
            [&]() {
                nn_motion_node_matrices(
                    valid_model(), valid_poses(), nonaffine_base, CameraPrecision::Single, backend);
            },
            "motion palette accepted a nonaffine base")) {
        return false;
    }

    NnModelData duplicate_slot = make_model({
        make_node(0x0000000fu, 0, -1, 1, -1, identity_matrix()),
        make_node(0x0000000fu, 0, 0, -1, -1, identity_matrix()),
    }, 1u);
    const std::vector<NnMotionLocalPose> two_poses = {
        valid_poses()[0u], valid_poses()[0u],
    };
    if (!expects_invalid_argument(
            [&]() {
                nn_motion_node_matrices(
                    duplicate_slot, two_poses, identity_matrix(), CameraPrecision::Single, backend);
            },
            "motion palette accepted a duplicate matrix slot")) {
        return false;
    }

    NnModelData inconsistent_child = make_model({
        make_node(0x0000000fu, 0, -1, 1, -1, identity_matrix()),
        make_node(0x0000000fu, 1, -1, -1, -1, identity_matrix()),
    }, 2u);
    if (!expects_invalid_argument(
            [&]() {
                nn_motion_node_matrices(
                    inconsistent_child, two_poses, identity_matrix(), CameraPrecision::Single, backend);
            },
            "motion palette accepted an inconsistent child parent")) {
        return false;
    }

    NnModelData orphan = make_model({
        make_node(0x0000000fu, 0, -1, -1, -1, identity_matrix()),
        make_node(0x0000000fu, 1, -1, -1, -1, identity_matrix()),
    }, 2u);
    if (!expects_invalid_argument(
            [&]() {
                nn_motion_node_matrices(
                    orphan, two_poses, identity_matrix(), CameraPrecision::Single, backend);
            },
            "motion palette accepted an orphaned node")) {
        return false;
    }

    const NnMotionLocalPose unit_pose = valid_poses()[0u];
    const std::vector<NnMotionLocalPose> deep_poses(33u, unit_pose);
    if (!expects_invalid_argument(
            [&]() {
                nn_motion_node_matrices(
                    make_chain(33u), deep_poses, identity_matrix(), CameraPrecision::Single, backend);
            },
            "motion palette accepted a hierarchy deeper than 32 nodes")) {
        return false;
    }

    MatrixBackend nonfinite_rotation_backend;
    nonfinite_rotation_backend.return_nonfinite_rotation = true;
    if (!expects_invalid_argument(
            [&]() {
                nn_motion_node_matrices(
                    valid_model(), valid_poses(), identity_matrix(), CameraPrecision::Single, nonfinite_rotation_backend);
            },
            "motion palette accepted a nonfinite rotation backend result")) {
        return false;
    }

    MatrixBackend nonaffine_multiply_backend;
    nonaffine_multiply_backend.return_nonaffine_multiply = true;
    return expects_invalid_argument(
        [&]() {
            nn_motion_node_matrices(
                valid_model(), valid_poses(), identity_matrix(), CameraPrecision::Single, nonaffine_multiply_backend);
        },
        "motion palette accepted a nonaffine multiply backend result");
}

}

int main() {
    return test_animation_ignores_bind_unit_bits_and_stale_inverse_bind() &&
                   test_parent_child_sibling_worlds_and_palette_slots() &&
                   test_inverse_bind_multiplies_world_after_capture() &&
                   test_slotless_node_keeps_inverse_bind_unread() &&
                   test_rejects_invalid_inputs_and_topology()
               ? 0
               : 1;
}
