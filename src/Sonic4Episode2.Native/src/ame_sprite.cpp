#include "ame_sprite.h"

#include "nn_trig.h"

#include <cmath>
#include <cstdint>
#include <limits>

namespace {

constexpr std::uint16_t kSimpleSpriteType = 0x0200u;
constexpr std::uint16_t kSpriteType = 0x0201u;
constexpr std::uint32_t kPCTFlag = 0x1000u;
constexpr float kInputLimit = 16777216.0f;
constexpr double kRadiansToA32 = 10430.3779296875;

using Vector = std::array<float, 3u>;

[[noreturn]] void fail(const char* message) {
    throw AmeSpriteError(message);
}

void require_precision(CameraPrecision precision) {
    if (precision != CameraPrecision::Single && precision != CameraPrecision::Double) {
        fail("AME sprite precision is unsupported");
    }
}

void require_finite(float value, const char* message) {
    if (!std::isfinite(value) || std::fabs(value) > kInputLimit) {
        fail(message);
    }
}

struct Arithmetic {
    CameraPrecision precision;

    double rounded(double value) const {
        if (!std::isfinite(value)) {
            fail("AME sprite arithmetic is nonfinite");
        }
        if (precision == CameraPrecision::Double) {
            return value;
        }
        int exponent = 0;
        const float mantissa = static_cast<float>(std::frexp(value, &exponent));
        const double result = std::ldexp(static_cast<double>(mantissa), exponent);
        if (!std::isfinite(result)) {
            fail("AME sprite arithmetic is nonfinite");
        }
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

    float spill(double value) const {
        const float result = static_cast<float>(value);
        if (!std::isfinite(result)) {
            fail("AME sprite arithmetic is nonfinite");
        }
        return result;
    }
};

Vector matrix_basis_column(const CameraMatrix& matrix, std::size_t column) {
    require_finite(matrix[column], "AME sprite world-view matrix is invalid");
    require_finite(matrix[4u + column], "AME sprite world-view matrix is invalid");
    require_finite(matrix[8u + column], "AME sprite world-view matrix is invalid");
    return {{matrix[column], matrix[4u + column], matrix[8u + column]}};
}

Vector scale_vector(const Vector& value, float scale, const Arithmetic& arithmetic) {
    return {{
        arithmetic.spill(arithmetic.multiply(value[0u], scale)),
        arithmetic.spill(arithmetic.multiply(value[1u], scale)),
        arithmetic.spill(arithmetic.multiply(value[2u], scale)),
    }};
}

Vector add_vectors(const Vector& left, const Vector& right, const Arithmetic& arithmetic) {
    return {{
        arithmetic.spill(arithmetic.add(left[0u], right[0u])),
        arithmetic.spill(arithmetic.add(left[1u], right[1u])),
        arithmetic.spill(arithmetic.add(left[2u], right[2u])),
    }};
}

Vector subtract_vectors(const Vector& left, const Vector& right, const Arithmetic& arithmetic) {
    return {{
        arithmetic.spill(arithmetic.subtract(left[0u], right[0u])),
        arithmetic.spill(arithmetic.subtract(left[1u], right[1u])),
        arithmetic.spill(arithmetic.subtract(left[2u], right[2u])),
    }};
}

Vector average_vectors(
    const Vector& left,
    const Vector& right,
    float left_scale,
    float right_scale,
    const Arithmetic& arithmetic) {
    return {{
        arithmetic.spill(arithmetic.add(
            arithmetic.multiply(left[0u], left_scale),
            arithmetic.multiply(right[0u], right_scale))),
        arithmetic.spill(arithmetic.add(
            arithmetic.multiply(left[1u], left_scale),
            arithmetic.multiply(right[1u], right_scale))),
        arithmetic.spill(arithmetic.add(
            arithmetic.multiply(left[2u], left_scale),
            arithmetic.multiply(right[2u], right_scale))),
    }};
}

void require(bool condition, const char* message) {
    if (!condition) {
        fail(message);
    }
}

std::uint16_t radians_to_angle(float radians, const Arithmetic& arithmetic) {
    const double converted = arithmetic.multiply(radians, kRadiansToA32);
    require(
        converted >= static_cast<double>(std::numeric_limits<std::int32_t>::min()) &&
            converted <= static_cast<double>(std::numeric_limits<std::int32_t>::max()),
        "AME sprite twist is out of range");
    return static_cast<std::uint16_t>(static_cast<std::int32_t>(converted));
}

std::uint32_t pre_backend_color(std::uint32_t color) noexcept {
    return ((color & 0x000000ffu) << 24u) |
           ((color & 0x0000ff00u) << 8u) |
           ((color & 0x00ff0000u) >> 8u) |
           ((color & 0xff000000u) >> 24u);
}

}

AmeSpriteQuad ame_sprite_quad(
    const AmeRuntimeSprite& sprite,
    const CameraMatrix& world_view,
    CameraPrecision precision) {
    require_precision(precision);
    require(sprite.type == kSimpleSpriteType || sprite.type == kSpriteType, "AME sprite type is unsupported");
    require((sprite.node_flags & kPCTFlag) != 0u, "AME sprite vertex format is unsupported");
    require_finite(sprite.position.x, "AME sprite position is invalid");
    require_finite(sprite.position.y, "AME sprite position is invalid");
    require_finite(sprite.position.z, "AME sprite position is invalid");
    require_finite(sprite.size_x, "AME sprite X size is invalid");
    require_finite(sprite.size_y, "AME sprite Y size is invalid");
    require_finite(sprite.z_bias, "AME sprite Z bias is invalid");
    require_finite(sprite.texture_left, "AME sprite texture coordinates are invalid");
    require_finite(sprite.texture_top, "AME sprite texture coordinates are invalid");
    require_finite(sprite.texture_right, "AME sprite texture coordinates are invalid");
    require_finite(sprite.texture_bottom, "AME sprite texture coordinates are invalid");
    if (sprite.type == kSpriteType) {
        require_finite(sprite.twist, "AME sprite twist is invalid");
    }

    const Arithmetic arithmetic{precision};
    const Vector right = matrix_basis_column(world_view, 0u);
    const Vector up = matrix_basis_column(world_view, 1u);
    const Vector forward = matrix_basis_column(world_view, 2u);
    const Vector center = add_vectors(
        {{sprite.position.x, sprite.position.y, sprite.position.z}},
        scale_vector(forward, sprite.z_bias, arithmetic),
        arithmetic);

    Vector horizontal = right;
    Vector vertical = up;
    if (sprite.type == kSpriteType) {
        const NnSinCos rotation = nn_sin_cos(radians_to_angle(sprite.twist, arithmetic), precision);
        horizontal = average_vectors(right, up, rotation.cosine, -rotation.sine, arithmetic);
        vertical = average_vectors(right, up, rotation.sine, rotation.cosine, arithmetic);
    }
    const Vector a = scale_vector(horizontal, sprite.size_x, arithmetic);
    const Vector b = scale_vector(vertical, sprite.size_y, arithmetic);

    const Vector v0 = add_vectors(subtract_vectors(center, a, arithmetic), b, arithmetic);
    const Vector v1 = add_vectors(add_vectors(center, a, arithmetic), b, arithmetic);
    const Vector v2 = subtract_vectors(subtract_vectors(center, a, arithmetic), b, arithmetic);
    const Vector v5 = subtract_vectors(add_vectors(center, a, arithmetic), b, arithmetic);
    const std::uint32_t color = pre_backend_color(sprite.color);
    AmeSpriteQuad result{};
    result.vertices = {{
        {v0, color, {{sprite.texture_left, sprite.texture_top}}},
        {v1, color, {{sprite.texture_right, sprite.texture_top}}},
        {v2, color, {{sprite.texture_left, sprite.texture_bottom}}},
        {v1, color, {{sprite.texture_right, sprite.texture_top}}},
        {v2, color, {{sprite.texture_left, sprite.texture_bottom}}},
        {v5, color, {{sprite.texture_right, sprite.texture_bottom}}},
    }};
    return result;
}

std::uint32_t ame_sprite_d3d_color(std::uint32_t color_rgba) noexcept {
    return (color_rgba >> 8u) | (color_rgba << 24u);
}
