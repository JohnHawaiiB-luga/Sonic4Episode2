#include "nn_motion_palette.h"

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <stdexcept>
#include <utility>
#include <vector>

namespace {

constexpr std::uint32_t kSupportedFlags = 0x006000cfu;
constexpr std::uint32_t kUnitInverseBind = 0x00000008u;
constexpr float kActiveInputLimit = 16777216.0f;
constexpr std::size_t kMaximumSupportedNodeDepth = 32u;

[[noreturn]] void fail(const char* message) {
    throw std::invalid_argument(message);
}

void require(bool condition, const char* message) {
    if (!condition) {
        fail(message);
    }
}

void require_precision(CameraPrecision precision) {
    require(
        precision == CameraPrecision::Single || precision == CameraPrecision::Double,
        "motion palette precision is unsupported");
}

float decode_float(std::uint32_t bits) {
    static_assert(sizeof(float) == sizeof(bits), "motion palette requires 32-bit floats");
    float value = 0.0f;
    std::memcpy(&value, &bits, sizeof(value));
    return value;
}

bool is_affine(const CameraMatrix& matrix) {
    return matrix[3u] == 0.0f && matrix[7u] == 0.0f &&
           matrix[11u] == 0.0f && matrix[15u] == 1.0f;
}

void require_finite_matrix(const CameraMatrix& matrix, const char* message) {
    for (float value : matrix) {
        require(std::isfinite(value), message);
    }
}

void require_affine_matrix(const CameraMatrix& matrix, const char* message) {
    require_finite_matrix(matrix, message);
    require(is_affine(matrix), message);
}

template <std::size_t N>
void require_active_vector(const std::array<float, N>& values, const char* message) {
    for (float value : values) {
        require(std::isfinite(value) && std::fabs(value) <= kActiveInputLimit, message);
    }
}

CameraMatrix decode_inverse_bind(const std::array<std::uint32_t, 16u>& bits) {
    CameraMatrix matrix{};
    for (std::size_t index = 0u; index < matrix.size(); ++index) {
        matrix[index] = decode_float(bits[index]);
    }
    require_affine_matrix(matrix, "motion palette inverse bind is nonfinite or nonaffine");
    return matrix;
}

struct Arithmetic {
    CameraPrecision precision;

    double rounded(double value) const {
        if (precision == CameraPrecision::Double) {
            return value;
        }
        int exponent = 0;
        const float mantissa = static_cast<float>(std::frexp(value, &exponent));
        return std::ldexp(static_cast<double>(mantissa), exponent);
    }

    double add(double left, double right) const {
        return rounded(left + right);
    }

    double multiply(double left, double right) const {
        return rounded(left * right);
    }
};

float translated_component(
    const CameraMatrix& matrix,
    std::size_t column,
    float x,
    float y,
    float z,
    const Arithmetic& arithmetic) {
    const double x_product = arithmetic.multiply(x, matrix[column]);
    const double y_product = arithmetic.multiply(y, matrix[4u + column]);
    const double z_product = arithmetic.multiply(z, matrix[8u + column]);
    const double xy_sum = column == 0u
        ? arithmetic.add(y_product, x_product)
        : arithmetic.add(x_product, y_product);
    const double xyz_sum = arithmetic.add(xy_sum, z_product);
    return static_cast<float>(arithmetic.add(xyz_sum, matrix[12u + column]));
}

void translate_in_place(
    CameraMatrix& matrix,
    const std::array<float, 3u>& translation,
    const Arithmetic& arithmetic) {
    const float translated_x = translated_component(
        matrix, 0u, translation[0u], translation[1u], translation[2u], arithmetic);
    const float translated_y = translated_component(
        matrix, 1u, translation[0u], translation[1u], translation[2u], arithmetic);
    const float translated_z = translated_component(
        matrix, 2u, translation[0u], translation[1u], translation[2u], arithmetic);
    matrix[12u] = translated_x;
    matrix[13u] = translated_y;
    matrix[14u] = translated_z;
    matrix[15u] = 1.0f;
}

void scale_in_place(
    CameraMatrix& matrix,
    const std::array<float, 3u>& scale,
    const Arithmetic& arithmetic) {
    for (std::size_t column = 0u; column < 3u; ++column) {
        matrix[column] = static_cast<float>(arithmetic.multiply(matrix[column], scale[0u]));
        matrix[4u + column] = static_cast<float>(arithmetic.multiply(matrix[4u + column], scale[1u]));
        matrix[8u + column] = static_cast<float>(arithmetic.multiply(matrix[8u + column], scale[2u]));
    }
    matrix[3u] = 0.0f;
    matrix[7u] = 0.0f;
    matrix[11u] = 0.0f;
    matrix[15u] = 1.0f;
}

void require_link(std::int16_t link, std::size_t node_count, const char* message) {
    require(
        link >= -1 && (link < 0 || static_cast<std::size_t>(link) < node_count),
        message);
}

void validate_node(
    const NnNodeData& node,
    std::size_t node_count,
    std::size_t matrix_palette_count) {
    require((node.flags & ~kSupportedFlags) == 0u, "motion palette node flags are unsupported");
    require(
        node.matrix_index >= -1 &&
            (node.matrix_index < 0 || static_cast<std::size_t>(node.matrix_index) < matrix_palette_count),
        "motion palette node matrix index is out of range");
    require_link(node.parent_index, node_count, "motion palette node parent index is out of range");
    require_link(node.child_index, node_count, "motion palette node child index is out of range");
    require_link(node.sibling_index, node_count, "motion palette node sibling index is out of range");
}

void validate_pose(const NnMotionLocalPose& pose) {
    require_active_vector(pose.translation, "motion palette translation is nonfinite or out of range");
    require_active_vector(pose.rotation, "motion palette quaternion is nonfinite or out of range");
    require_active_vector(pose.scale, "motion palette scale is nonfinite or out of range");
}

struct PendingNode {
    std::size_t index;
    std::int16_t expected_parent;
    std::size_t depth;
    CameraMatrix parent_world;
};

}

NnMotionNodeMatrices nn_motion_node_matrices(
    const NnModelData& model,
    const std::vector<NnMotionLocalPose>& poses,
    const CameraMatrix& base,
    CameraPrecision precision,
    const CameraMatrixBackend& backend) {
    require_precision(precision);
    require_affine_matrix(base, "motion palette base is nonfinite or nonaffine");

    const std::size_t node_count = model.nodes.size();
    const std::size_t matrix_palette_count = static_cast<std::size_t>(model.matrix_palette_count);
    require(model.node_count == node_count, "motion palette node count does not match records");
    require(node_count != 0u, "motion palette requires a root node");
    require(poses.size() == node_count, "motion palette pose count does not match nodes");
    require(matrix_palette_count <= node_count, "motion palette has more slots than nodes");
    for (std::size_t index = 0u; index < node_count; ++index) {
        validate_node(model.nodes[index], node_count, matrix_palette_count);
        validate_pose(poses[index]);
    }
    require(model.nodes[0u].parent_index == -1, "motion palette root node has a parent");

    const Arithmetic arithmetic{precision};
    NnMotionNodeMatrices result;
    result.node_world.resize(node_count);
    result.palette.resize(matrix_palette_count);
    std::vector<bool> palette_written(matrix_palette_count, false);
    std::vector<bool> visited(node_count, false);
    std::vector<PendingNode> pending;
    pending.reserve(node_count);
    pending.push_back({0u, -1, 1u, base});

    while (!pending.empty()) {
        PendingNode pending_node = std::move(pending.back());
        pending.pop_back();
        require(
            pending_node.depth <= kMaximumSupportedNodeDepth,
            "motion palette hierarchy depth is unsupported");
        require(!visited[pending_node.index], "motion palette hierarchy has a cycle or duplicate visit");

        const NnNodeData& node = model.nodes[pending_node.index];
        const NnMotionLocalPose& pose = poses[pending_node.index];
        require(node.parent_index == pending_node.expected_parent, "motion palette node parent is inconsistent");
        visited[pending_node.index] = true;

        CameraMatrix current = pending_node.parent_world;
        translate_in_place(current, pose.translation, arithmetic);
        require_affine_matrix(current, "motion palette translation overflowed");

        const CameraMatrix rotation = backend.rotation_quaternion(pose.rotation, precision);
        require_affine_matrix(rotation, "motion palette rotation matrix is nonfinite or nonaffine");
        current = backend.multiply(rotation, current, precision);
        require_affine_matrix(current, "motion palette rotation result is nonfinite or nonaffine");

        scale_in_place(current, pose.scale, arithmetic);
        require_affine_matrix(current, "motion palette scale overflowed");
        result.node_world[pending_node.index] = current;

        if (node.matrix_index >= 0) {
            const std::size_t slot = static_cast<std::size_t>(node.matrix_index);
            require(!palette_written[slot], "motion palette has a duplicate matrix slot");
            CameraMatrix value = current;
            if ((node.flags & kUnitInverseBind) == 0u) {
                const CameraMatrix inverse_bind = decode_inverse_bind(node.inverse_bind_bits);
                value = backend.multiply(inverse_bind, current, precision);
                require_affine_matrix(value, "motion palette matrix result is nonfinite or nonaffine");
            }
            result.palette[slot] = value;
            palette_written[slot] = true;
        }

        if (node.sibling_index >= 0) {
            const std::size_t sibling = static_cast<std::size_t>(node.sibling_index);
            require(
                model.nodes[sibling].parent_index == pending_node.expected_parent,
                "motion palette sibling parent is inconsistent");
            pending.push_back({
                sibling,
                pending_node.expected_parent,
                pending_node.depth,
                pending_node.parent_world,
            });
        }
        if (node.child_index >= 0) {
            require(
                pending_node.depth < kMaximumSupportedNodeDepth,
                "motion palette hierarchy depth is unsupported");
            require(
                pending_node.index <= static_cast<std::size_t>(std::numeric_limits<std::int16_t>::max()),
                "motion palette child parent index is unsupported");
            const std::size_t child = static_cast<std::size_t>(node.child_index);
            const std::int16_t expected_parent = static_cast<std::int16_t>(pending_node.index);
            require(
                model.nodes[child].parent_index == expected_parent,
                "motion palette child parent is inconsistent");
            pending.push_back({child, expected_parent, pending_node.depth + 1u, current});
        }
    }

    for (bool was_visited : visited) {
        require(was_visited, "motion palette has an orphaned node");
    }
    for (bool was_written : palette_written) {
        require(was_written, "motion palette has an unfilled matrix slot");
    }
    return result;
}
