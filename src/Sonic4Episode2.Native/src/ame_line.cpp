#include "ame_line.h"

#include <cmath>

namespace {

using Vector = std::array<float, 3u>;
constexpr float kInputLimit = 16777216.0f;

void require(bool value, const char* message) {
    if (!value) {
        throw AmeLineError(message);
    }
}

void require_finite(float value) {
    require(std::isfinite(value) && std::fabs(value) <= kInputLimit, "AME line input is invalid");
}

struct Arithmetic {
    CameraPrecision precision;

    double rounded(double value) const {
        require(std::isfinite(value), "AME line arithmetic is nonfinite");
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
        const float result = static_cast<float>(value);
        require(std::isfinite(result), "AME line arithmetic is nonfinite");
        return result;
    }
};

Vector scale(const Vector& value, float factor, const Arithmetic& arithmetic) {
    return {{
        arithmetic.spill(arithmetic.multiply(value[0u], factor)),
        arithmetic.spill(arithmetic.multiply(value[1u], factor)),
        arithmetic.spill(arithmetic.multiply(value[2u], factor)),
    }};
}

Vector add(const Vector& left, const Vector& right, const Arithmetic& arithmetic) {
    return {{
        arithmetic.spill(arithmetic.add(left[0u], right[0u])),
        arithmetic.spill(arithmetic.add(left[1u], right[1u])),
        arithmetic.spill(arithmetic.add(left[2u], right[2u])),
    }};
}

Vector subtract(const Vector& left, const Vector& right, const Arithmetic& arithmetic) {
    return {{
        arithmetic.spill(arithmetic.subtract(left[0u], right[0u])),
        arithmetic.spill(arithmetic.subtract(left[1u], right[1u])),
        arithmetic.spill(arithmetic.subtract(left[2u], right[2u])),
    }};
}

float square_length(const Vector& value, const Arithmetic& arithmetic) {
    return arithmetic.spill(arithmetic.add(
        arithmetic.add(arithmetic.multiply(value[1u], value[1u]), arithmetic.multiply(value[0u], value[0u])),
        arithmetic.multiply(value[2u], value[2u])));
}

Vector direction(const Vector& value, const Arithmetic& arithmetic) {
    const float length = arithmetic.spill(arithmetic.rounded(std::sqrt(square_length(value, arithmetic))));
    if (length == 0.0f) {
        return value;
    }
    return scale(value, arithmetic.spill(arithmetic.divide(1.0, length)), arithmetic);
}

Vector cross_direction(const Vector& left, const Vector& right, const Arithmetic& arithmetic) {
    const Vector cross{{
        arithmetic.spill(arithmetic.subtract(arithmetic.multiply(left[1u], right[2u]), arithmetic.multiply(left[2u], right[1u]))),
        arithmetic.spill(arithmetic.subtract(arithmetic.multiply(right[0u], left[2u]), arithmetic.multiply(right[2u], left[0u]))),
        arithmetic.spill(arithmetic.subtract(arithmetic.multiply(left[0u], right[1u]), arithmetic.multiply(left[1u], right[0u]))),
    }};
    const float square = square_length(cross, arithmetic);
    if (square == 0.0f) {
        return {{0.0f, 0.0f, 0.0f}};
    }
    const double length = arithmetic.rounded(std::sqrt(static_cast<double>(square)));
    return scale(cross, arithmetic.spill(arithmetic.divide(1.0, length)), arithmetic);
}

std::uint32_t vertex_color(std::uint32_t color) noexcept {
    return ((color & 0x000000ffu) << 24u) |
           ((color & 0x0000ff00u) << 8u) |
           ((color & 0x00ff0000u) >> 8u) |
           ((color & 0xff000000u) >> 24u);
}

}

AmeLineQuad ame_line_quad(
    const AmeRuntimeLine& line,
    const CameraMatrix& world_view,
    const std::array<float, 3u>& camera_position,
    CameraPrecision precision) {
    require(precision == CameraPrecision::Single || precision == CameraPrecision::Double, "AME line precision is unsupported");
    require((line.node_flags & 0x1000u) != 0u, "AME line vertex format is unsupported");
    for (float value : {line.position.x, line.position.y, line.position.z,
             line.velocity.x, line.velocity.y, line.velocity.z, line.length,
             line.inside_width, line.outside_width, line.z_bias,
             line.texture_left, line.texture_top, line.texture_right, line.texture_bottom,
             world_view[2u], world_view[6u], world_view[10u], camera_position[2u]}) {
        require_finite(value);
    }
    const Arithmetic arithmetic{precision};
    const Vector eye{{world_view[2u], world_view[6u], world_view[10u]}};
    const Vector velocity = direction({{line.velocity.x, line.velocity.y, line.velocity.z}}, arithmetic);
    const Vector inside = add({{line.position.x, line.position.y, line.position.z}}, scale(eye, line.z_bias, arithmetic), arithmetic);
    const Vector outside = add(scale(velocity, line.length, arithmetic), inside, arithmetic);
    const Vector across = cross_direction(velocity, eye, arithmetic);
    const Vector outer_width = scale(across, line.outside_width, arithmetic);
    const Vector inner_width = scale(across, line.inside_width, arithmetic);
    const std::uint32_t outer_color = vertex_color(line.outside_color);
    const std::uint32_t inner_color = vertex_color(line.inside_color);
    const AmeSpriteVertex v0{subtract(outside, outer_width, arithmetic), outer_color, {{line.texture_left, line.texture_top}}};
    const AmeSpriteVertex v1{add(outside, outer_width, arithmetic), outer_color, {{line.texture_right, line.texture_top}}};
    const AmeSpriteVertex v2{subtract(inside, inner_width, arithmetic), inner_color, {{line.texture_left, line.texture_bottom}}};
    const AmeSpriteVertex v5{add(inside, inner_width, arithmetic), inner_color, {{line.texture_right, line.texture_bottom}}};
    return {{{v0, v1, v2, v1, v2, v5}}, arithmetic.spill(std::fabs(arithmetic.subtract(outside[2u], camera_position[2u])))};
}
