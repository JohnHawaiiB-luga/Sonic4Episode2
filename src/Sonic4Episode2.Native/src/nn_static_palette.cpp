#include "nn_static_palette.h"

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

constexpr std::uint32_t kSupportedFlags = 0x002000cfu;
constexpr std::uint32_t kUnitTranslation = 0x00000001u;
constexpr std::uint32_t kUnitRotation = 0x00000002u;
constexpr std::uint32_t kUnitScale = 0x00000004u;
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
        "static palette precision is unsupported");
}

float decode_float(std::uint32_t bits) {
    static_assert(sizeof(float) == sizeof(bits), "static palette requires 32-bit floats");
    float value = 0.0f;
    std::memcpy(&value, &bits, sizeof(value));
    return value;
}

bool is_affine(const CameraMatrix& matrix) {
    return matrix[3u] == 0.0f && matrix[7u] == 0.0f && matrix[11u] == 0.0f && matrix[15u] == 1.0f;
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

std::array<float, 3u> decode_active_vector(
    const std::array<std::uint32_t, 3u>& bits,
    const char* message) {
    std::array<float, 3u> values{};
    for (std::size_t index = 0u; index < values.size(); ++index) {
        const float value = decode_float(bits[index]);
        require(std::isfinite(value) && std::fabs(value) <= kActiveInputLimit, message);
        values[index] = value;
    }
    return values;
}

CameraMatrix decode_inverse_bind(const std::array<std::uint32_t, 16u>& bits) {
    CameraMatrix matrix{};
    for (std::size_t index = 0u; index < matrix.size(); ++index) {
        matrix[index] = decode_float(bits[index]);
    }
    require_affine_matrix(matrix, "static palette inverse bind is nonfinite or nonaffine");
    return matrix;
}

struct Arithmetic {
    CameraPrecision precision;

    double rounded(double value) const {
        if (precision == CameraPrecision::Double) {
            return value;
        }
        // x87 precision limits the significand without narrowing the exponent before a float store.
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
    require((node.flags & ~kSupportedFlags) == 0u, "static palette node flags are unsupported");
    require((node.flags & kUnitRotation) != 0u, "static palette nonunit rotation is unsupported");
    require(
        node.matrix_index >= -1 &&
            (node.matrix_index < 0 || static_cast<std::size_t>(node.matrix_index) < matrix_palette_count),
        "static palette node matrix index is out of range");
    require_link(node.parent_index, node_count, "static palette node parent index is out of range");
    require_link(node.child_index, node_count, "static palette node child index is out of range");
    require_link(node.sibling_index, node_count, "static palette node sibling index is out of range");
}

CameraMatrix apply_local(
    const CameraMatrix& parent_world,
    const NnNodeData& node,
    const Arithmetic& arithmetic) {
    CameraMatrix current = parent_world;
    if ((node.flags & kUnitTranslation) == 0u) {
        translate_in_place(
            current,
            decode_active_vector(node.translation_bits, "static palette translation is nonfinite or out of range"),
            arithmetic);
        require_affine_matrix(current, "static palette translation overflowed");
    }
    if ((node.flags & kUnitScale) == 0u) {
        scale_in_place(
            current,
            decode_active_vector(node.scale_bits, "static palette scale is nonfinite or out of range"),
            arithmetic);
        require_affine_matrix(current, "static palette scale overflowed");
    }
    return current;
}

struct PendingNode {
    std::size_t index;
    std::int16_t expected_parent;
    std::size_t depth;
    CameraMatrix parent_world;
};

std::vector<CameraMatrix> calculate_matrices(
    const NnModelData& model,
    const CameraMatrix& base,
    CameraPrecision precision,
    const CameraMatrixBackend& backend,
    std::vector<CameraMatrix>* node_world) {
    require_precision(precision);
    require_affine_matrix(base, "static palette base is nonfinite or nonaffine");

    const std::size_t node_count = model.nodes.size();
    const std::size_t matrix_palette_count = static_cast<std::size_t>(model.matrix_palette_count);
    require(model.node_count == node_count, "static palette node count does not match records");
    require(node_count != 0u, "static palette requires a root node");
    require(matrix_palette_count <= node_count, "static palette has more slots than nodes");
    for (const NnNodeData& node : model.nodes) {
        validate_node(node, node_count, matrix_palette_count);
    }
    require(model.nodes[0u].parent_index == -1, "static palette root node has a parent");

    const Arithmetic arithmetic{precision};
    if (node_world != nullptr) {
        node_world->resize(node_count);
    }
    std::vector<CameraMatrix> palette(matrix_palette_count);
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
            "static palette hierarchy depth is unsupported");
        require(!visited[pending_node.index], "static palette hierarchy has a cycle or duplicate visit");

        const NnNodeData& node = model.nodes[pending_node.index];
        require(node.parent_index == pending_node.expected_parent, "static palette node parent is inconsistent");
        visited[pending_node.index] = true;
        const CameraMatrix current = apply_local(pending_node.parent_world, node, arithmetic);
        if (node_world != nullptr) {
            (*node_world)[pending_node.index] = current;
        }

        if (node.matrix_index >= 0) {
            const std::size_t slot = static_cast<std::size_t>(node.matrix_index);
            require(!palette_written[slot], "static palette has a duplicate matrix slot");
            CameraMatrix value = current;
            if ((node.flags & kUnitInverseBind) == 0u) {
                const CameraMatrix inverse_bind = decode_inverse_bind(node.inverse_bind_bits);
                value = backend.multiply(inverse_bind, current, precision);
                require_affine_matrix(value, "static palette matrix result is nonfinite or nonaffine");
            }
            palette[slot] = value;
            palette_written[slot] = true;
        }

        if (node.sibling_index >= 0) {
            const std::size_t sibling = static_cast<std::size_t>(node.sibling_index);
            require(
                model.nodes[sibling].parent_index == pending_node.expected_parent,
                "static palette sibling parent is inconsistent");
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
                "static palette hierarchy depth is unsupported");
            require(
                pending_node.index <= static_cast<std::size_t>(std::numeric_limits<std::int16_t>::max()),
                "static palette child parent index is unsupported");
            const std::size_t child = static_cast<std::size_t>(node.child_index);
            const std::int16_t expected_parent = static_cast<std::int16_t>(pending_node.index);
            require(
                model.nodes[child].parent_index == expected_parent,
                "static palette child parent is inconsistent");
            pending.push_back({child, expected_parent, pending_node.depth + 1u, current});
        }
    }

    for (bool was_visited : visited) {
        require(was_visited, "static palette has an orphaned node");
    }
    for (bool was_written : palette_written) {
        require(was_written, "static palette has an unfilled matrix slot");
    }
    return palette;
}

}

NnStaticNodeMatrices nn_static_node_matrices(
    const NnModelData& model,
    const CameraMatrix& base,
    CameraPrecision precision,
    const CameraMatrixBackend& backend) {
    NnStaticNodeMatrices result;
    result.palette = calculate_matrices(model, base, precision, backend, &result.node_world);
    return result;
}

std::vector<CameraMatrix> nn_static_matrix_palette(
    const NnModelData& model,
    const CameraMatrix& base,
    CameraPrecision precision,
    const CameraMatrixBackend& backend) {
    return calculate_matrices(model, base, precision, backend, nullptr);
}
