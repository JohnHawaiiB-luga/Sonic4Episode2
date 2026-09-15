#include "nn_node_clip.h"

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <stdexcept>

namespace {

constexpr float kInputLimit = 16777216.0f;
constexpr std::uint32_t kOutsideMask = 0x10u;
constexpr std::uint32_t kNearIntersectionMask = 0x104u;
constexpr std::uint32_t kFarIntersectionMask = 0x208u;
constexpr std::uint32_t kRightIntersectionMask = 0x1000u;
constexpr std::uint32_t kLeftIntersectionMask = 0x2000u;
constexpr std::uint32_t kTopIntersectionMask = 0x4000u;
constexpr std::uint32_t kBottomIntersectionMask = 0x8000u;
constexpr std::uint32_t kReturnedIntersectionMask = 0x3eu;

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
        "node clip precision is unsupported");
}

void require_finite_bounded(float value, const char* message) {
    require(std::isfinite(value) && std::fabs(value) <= kInputLimit, message);
}

float decode_float(std::uint32_t bits) {
    static_assert(sizeof(float) == sizeof(bits), "node clip requires 32-bit floats");
    float value = 0.0f;
    std::memcpy(&value, &bits, sizeof(value));
    return value;
}

bool is_affine(const CameraMatrix& matrix) {
    return matrix[3u] == 0.0f && matrix[7u] == 0.0f && matrix[11u] == 0.0f && matrix[15u] == 1.0f;
}

void require_affine_matrix(const CameraMatrix& matrix) {
    for (float value : matrix) {
        require(std::isfinite(value), "node clip matrix is nonfinite");
    }
    require(is_affine(matrix), "node clip matrix is nonaffine");
}

void require_center(const std::array<float, 3u>& center) {
    for (float value : center) {
        require_finite_bounded(value, "node clip center is nonfinite or out of range");
    }
}

void require_half_extents(const std::array<float, 3u>& half_extents) {
    for (float value : half_extents) {
        require_finite_bounded(value, "node clip half extent is nonfinite or out of range");
        require(value >= 0.0f, "node clip half extent is negative");
    }
}

void require_radius(float radius) {
    require_finite_bounded(radius, "node clip sphere radius is nonfinite or out of range");
    require(radius >= 0.0f, "node clip sphere radius is negative");
}

void require_plane(const std::array<float, 2u>& plane) {
    require_finite_bounded(plane[0u], "node clip plane coefficient is nonfinite or out of range");
    require_finite_bounded(plane[1u], "node clip plane coefficient is nonfinite or out of range");
}

void require_context(const NnPerspectiveClipContext& context) {
    require_finite_bounded(context.near_plane, "node clip near plane is nonfinite or out of range");
    require_finite_bounded(context.far_plane, "node clip far plane is nonfinite or out of range");
    require(context.near_plane > 0.0f, "node clip near plane is not positive");
    require(context.far_plane > context.near_plane, "node clip far plane is not after near plane");
    require_plane(context.top);
    require_plane(context.bottom);
    require_plane(context.right);
    require_plane(context.left);
}

struct Arithmetic {
    CameraPrecision precision;

    double rounded(double value) const {
        require(std::isfinite(value), "node clip arithmetic is nonfinite");
        if (precision == CameraPrecision::Double) {
            return value;
        }
        int exponent = 0;
        const float mantissa = static_cast<float>(std::frexp(value, &exponent));
        const double result = std::ldexp(static_cast<double>(mantissa), exponent);
        require(std::isfinite(result), "node clip arithmetic is nonfinite");
        return result;
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
};

float store_finite(double value, const char* message) {
    require(std::isfinite(value), message);
    const float stored = static_cast<float>(value);
    require(std::isfinite(stored), message);
    return stored;
}

float transformed_component(
    const CameraMatrix& matrix,
    std::size_t axis,
    const std::array<float, 3u>& center,
    const Arithmetic& arithmetic) {
    const double x_product = arithmetic.multiply(center[0u], matrix[axis]);
    const double y_product = arithmetic.multiply(center[1u], matrix[4u + axis]);
    const double z_product = arithmetic.multiply(center[2u], matrix[8u + axis]);
    const double xy_sum = axis == 0u
        ? arithmetic.add(y_product, x_product)
        : arithmetic.add(x_product, y_product);
    const double xyz_sum = arithmetic.add(xy_sum, z_product);
    return store_finite(
        arithmetic.add(xyz_sum, matrix[12u + axis]),
        "node clip transformed center is nonfinite");
}

std::array<float, 3u> transform_center(
    const CameraMatrix& matrix,
    const std::array<float, 3u>& center,
    const Arithmetic& arithmetic) {
    return {{
        transformed_component(matrix, 0u, center, arithmetic),
        transformed_component(matrix, 1u, center, arithmetic),
        transformed_component(matrix, 2u, center, arithmetic),
    }};
}

using ProjectedExtents = std::array<std::array<float, 3u>, 3u>;

ProjectedExtents project_half_extents(
    const CameraMatrix& matrix,
    const std::array<float, 3u>& half_extents,
    const Arithmetic& arithmetic) {
    ProjectedExtents result{};
    for (std::size_t local_axis = 0u; local_axis < half_extents.size(); ++local_axis) {
        for (std::size_t world_axis = 0u; world_axis < result.size(); ++world_axis) {
            result[world_axis][local_axis] = store_finite(
                arithmetic.multiply(half_extents[local_axis], matrix[local_axis * 4u + world_axis]),
                "node clip projected extent is nonfinite");
        }
    }
    return result;
}

float absolute_sum(
    const std::array<float, 3u>& values,
    const Arithmetic& arithmetic,
    const char* message) {
    const double first = arithmetic.add(
        std::fabs(static_cast<double>(values[0u])),
        std::fabs(static_cast<double>(values[1u])));
    return store_finite(
        arithmetic.add(first, std::fabs(static_cast<double>(values[2u]))),
        message);
}

float side_radius(
    const ProjectedExtents& projected,
    std::size_t axis,
    const std::array<float, 2u>& plane,
    const Arithmetic& arithmetic) {
    std::array<float, 3u> contributions{};
    for (std::size_t local_axis = 0u; local_axis < contributions.size(); ++local_axis) {
        const double coordinate_term = arithmetic.multiply(projected[axis][local_axis], plane[0u]);
        const double z_term = arithmetic.multiply(projected[2u][local_axis], plane[1u]);
        contributions[local_axis] = store_finite(
            std::fabs(arithmetic.add(coordinate_term, z_term)),
            "node clip side contribution is nonfinite");
    }
    return absolute_sum(contributions, arithmetic, "node clip side radius is nonfinite");
}

float side_distance(
    const std::array<float, 3u>& transformed_center,
    std::size_t axis,
    const std::array<float, 2u>& plane,
    const Arithmetic& arithmetic) {
    return store_finite(
        arithmetic.add(
            arithmetic.multiply(transformed_center[axis], plane[0u]),
            arithmetic.multiply(transformed_center[2u], plane[1u])),
        "node clip side distance is nonfinite");
}

bool classify_side(
    const ProjectedExtents& projected,
    const std::array<float, 3u>& transformed_center,
    std::size_t axis,
    const std::array<float, 2u>& plane,
    std::uint32_t side_mask,
    const Arithmetic& arithmetic,
    std::uint32_t& result) {
    const float radius = side_radius(projected, axis, plane, arithmetic);
    const float distance = side_distance(transformed_center, axis, plane, arithmetic);
    if (static_cast<double>(distance) > static_cast<double>(radius)) {
        return true;
    }
    if (static_cast<double>(distance) >
        arithmetic.subtract(0.0, static_cast<double>(radius))) {
        result |= side_mask;
    }
    return false;
}

bool classify_sphere_side(
    const std::array<float, 3u>& transformed_center,
    float radius,
    std::size_t axis,
    const std::array<float, 2u>& plane,
    std::uint32_t side_mask,
    const Arithmetic& arithmetic,
    std::uint32_t& result) {
    const float distance = side_distance(transformed_center, axis, plane, arithmetic);
    if (static_cast<double>(distance) > static_cast<double>(radius)) {
        return true;
    }
    if (static_cast<double>(distance) >
        arithmetic.subtract(0.0, static_cast<double>(radius))) {
        result |= side_mask;
    }
    return false;
}

std::array<float, 3u> decode_center(const NnNodeData& node) {
    return {{
        decode_float(node.opaque_bits[0u]),
        decode_float(node.opaque_bits[1u]),
        decode_float(node.opaque_bits[2u]),
    }};
}

std::array<float, 3u> decode_half_extents(const NnNodeData& node) {
    return {{
        decode_float(node.opaque_bits[5u]),
        decode_float(node.opaque_bits[6u]),
        decode_float(node.opaque_bits[7u]),
    }};
}

}

std::uint32_t nn_clip_box(
    const std::array<float, 3u>& center,
    const std::array<float, 3u>& half_extents,
    const CameraMatrix& node_world,
    const NnPerspectiveClipContext& context,
    CameraPrecision precision) {
    require_precision(precision);
    require_center(center);
    require_half_extents(half_extents);
    require_affine_matrix(node_world);
    require_context(context);

    const Arithmetic arithmetic{precision};
    const std::array<float, 3u> transformed_center = transform_center(node_world, center, arithmetic);
    const ProjectedExtents projected = project_half_extents(node_world, half_extents, arithmetic);
    const float z_radius = absolute_sum(projected[2u], arithmetic, "node clip depth radius is nonfinite");

    if (static_cast<double>(transformed_center[2u]) >
            arithmetic.add(-static_cast<double>(context.near_plane), z_radius) ||
        static_cast<double>(transformed_center[2u]) <
            arithmetic.subtract(-static_cast<double>(context.far_plane), z_radius)) {
        return kOutsideMask;
    }

    std::uint32_t result = 0u;
    if (static_cast<double>(transformed_center[2u]) >
        arithmetic.subtract(-static_cast<double>(context.near_plane), z_radius)) {
        result |= kNearIntersectionMask;
    }
    if (static_cast<double>(transformed_center[2u]) <
        arithmetic.add(-static_cast<double>(context.far_plane), z_radius)) {
        result |= kFarIntersectionMask;
    }

    if (classify_side(
            projected,
            transformed_center,
            0u,
            context.right,
            kRightIntersectionMask,
            arithmetic,
            result) ||
        classify_side(
            projected,
            transformed_center,
            0u,
            context.left,
            kLeftIntersectionMask,
            arithmetic,
            result) ||
        classify_side(
            projected,
            transformed_center,
            1u,
            context.top,
            kTopIntersectionMask,
            arithmetic,
            result) ||
        classify_side(
            projected,
            transformed_center,
            1u,
            context.bottom,
            kBottomIntersectionMask,
            arithmetic,
            result)) {
        return kOutsideMask;
    }

    return result == 0u ? 2u : result & kReturnedIntersectionMask;
}

std::uint32_t nn_clip_sphere(
    const std::array<float, 3u>& center,
    float radius,
    const CameraMatrix& node_world,
    const NnPerspectiveClipContext& context,
    CameraPrecision precision) {
    require_precision(precision);
    require_center(center);
    require_radius(radius);
    require_affine_matrix(node_world);
    require_context(context);

    const Arithmetic arithmetic{precision};
    const std::array<float, 3u> transformed_center = transform_center(node_world, center, arithmetic);
    if (static_cast<double>(transformed_center[2u]) >
            arithmetic.add(-static_cast<double>(context.near_plane), static_cast<double>(radius)) ||
        static_cast<double>(transformed_center[2u]) <
            arithmetic.subtract(-static_cast<double>(context.far_plane), static_cast<double>(radius))) {
        return kOutsideMask;
    }

    std::uint32_t result = 0u;
    if (static_cast<double>(transformed_center[2u]) >
        arithmetic.subtract(-static_cast<double>(context.near_plane), static_cast<double>(radius))) {
        result |= kNearIntersectionMask;
    }
    if (static_cast<double>(transformed_center[2u]) <
        arithmetic.add(-static_cast<double>(context.far_plane), static_cast<double>(radius))) {
        result |= kFarIntersectionMask;
    }

    if (classify_sphere_side(
            transformed_center,
            radius,
            0u,
            context.right,
            kRightIntersectionMask,
            arithmetic,
            result) ||
        classify_sphere_side(
            transformed_center,
            radius,
            0u,
            context.left,
            kLeftIntersectionMask,
            arithmetic,
            result) ||
        classify_sphere_side(
            transformed_center,
            radius,
            1u,
            context.top,
            kTopIntersectionMask,
            arithmetic,
            result) ||
        classify_sphere_side(
            transformed_center,
            radius,
            1u,
            context.bottom,
            kBottomIntersectionMask,
            arithmetic,
            result)) {
        return kOutsideMask;
    }

    return result == 0u ? 2u : result & kReturnedIntersectionMask;
}

std::uint32_t nn_clip_box_node_status(
    const NnNodeData& node,
    const CameraMatrix& node_world,
    const NnPerspectiveClipContext& context,
    std::uint32_t initial_status,
    std::uint32_t flags,
    CameraPrecision precision) {
    require_precision(precision);
    std::uint32_t status = initial_status;
    if ((status & 0x401u) != 0u) {
        return status;
    }

    if ((node.flags & 0x10u) != 0u) {
        status |= 1u;
    }
    if ((node.flags & 0x20u) != 0u) {
        require(node.child_index == -1, "node clip leaf status does not support descendants");
        return status | 1u;
    }

    if (flags == 0u) {
        return status;
    }
    require((flags & ~0x13u) == 0u, "node clip status flags are unsupported");
    if (node.matrix_index == -1) {
        return status;
    }
    require(node.child_index == -1, "node clip leaf status does not support descendants");
    require(
        (node.flags & 0x600000u) == 0x200000u,
        "node clip status does not support the selected bound route");

    const std::array<float, 3u> center = decode_center(node);
    const std::array<float, 3u> half_extents = decode_half_extents(node);
    status |= nn_clip_box(center, half_extents, node_world, context, precision);
    if ((status & kOutsideMask) != 0u) {
        status |= (flags & 2u) != 0u ? 0x400u : 1u;
    }
    return status;
}
