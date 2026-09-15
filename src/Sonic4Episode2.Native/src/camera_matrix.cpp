#include "camera_matrix.h"

#include <cmath>
#include <cstdint>
#include <stdexcept>

namespace {

using Vector = std::array<float, 3u>;

void require_precision(CameraPrecision precision) {
    if (precision != CameraPrecision::Single && precision != CameraPrecision::Double) {
        throw std::invalid_argument("camera precision is unsupported");
    }
}

struct Arithmetic {
    CameraPrecision precision;

    double rounded(double value) const {
        if (precision == CameraPrecision::Double) {
            return value;
        }
        // x87 precision control rounds the significand without narrowing the exponent.
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

    Vector normalize(const Vector& input) const {
        const double xy = add(multiply(input[0u], input[0u]), multiply(input[1u], input[1u]));
        const float squared_length = static_cast<float>(add(xy, multiply(input[2u], input[2u])));
        if (squared_length == 0.0f) {
            return {{0.0f, 0.0f, 0.0f}};
        }
        const double length = rounded(std::sqrt(static_cast<double>(squared_length)));
        const float reciprocal = static_cast<float>(rounded(1.0 / length));
        return {{
            static_cast<float>(multiply(input[0u], reciprocal)),
            static_cast<float>(multiply(input[1u], reciprocal)),
            static_cast<float>(multiply(input[2u], reciprocal)),
        }};
    }

    float cross_component(const Vector& left, const Vector& right, std::size_t a, std::size_t b) const {
        return static_cast<float>(subtract(multiply(left[a], right[b]), multiply(left[b], right[a])));
    }

    float translation(const Vector& eye, const Vector& axis, bool y_first) const {
        const double x = multiply(eye[0u], axis[0u]);
        const double y = multiply(eye[1u], axis[1u]);
        const double xy = y_first ? add(y, x) : add(x, y);
        return static_cast<float>(-add(xy, multiply(eye[2u], axis[2u])));
    }
};

float roll_radians(std::int32_t roll, CameraPrecision precision) {
    const std::uint32_t wrapped = 0u - static_cast<std::uint32_t>(roll);
    const std::int64_t angle = wrapped <= 0x7fffffffu
        ? static_cast<std::int64_t>(wrapped)
        : static_cast<std::int64_t>(wrapped) - 0x100000000ll;
    const std::int64_t product = angle * 13176795ll;
    if (precision == CameraPrecision::Double) {
        return static_cast<float>(std::ldexp(static_cast<double>(product), -37));
    }

    // Round the exact integer product directly to avoid double rounding large roll angles.
    const bool negative = product < 0;
    std::uint64_t magnitude = static_cast<std::uint64_t>(negative ? -product : product);
    std::uint64_t significant = magnitude;
    unsigned shift = 0u;
    while (significant > 0xffffffu) {
        significant >>= 1u;
        ++shift;
    }
    if (shift != 0u) {
        const std::uint64_t remainder = magnitude & ((std::uint64_t{1u} << shift) - 1u);
        const std::uint64_t half = std::uint64_t{1u} << (shift - 1u);
        if (remainder > half || (remainder == half && (significant & 1u) != 0u)) {
            ++significant;
        }
        magnitude = significant << shift;
    }
    const double rounded = static_cast<double>(magnitude);
    return static_cast<float>(std::ldexp(negative ? -rounded : rounded, -37));
}

}

CameraMatrix CameraMatrixBackend::rotation_quaternion(
    const std::array<float, 4u>&, CameraPrecision precision) const {
    require_precision(precision);
    throw std::logic_error("matrix backend does not support quaternion rotation");
}

CameraMatrix make_camera_view_basis(const CameraViewParameters& view, CameraPrecision precision) {
    require_precision(precision);
    const Arithmetic arithmetic{precision};
    Vector direction{};
    for (std::size_t index = 0u; index < direction.size(); ++index) {
        direction[index] = static_cast<float>(arithmetic.subtract(view.eye[index], view.target[index]));
    }
    direction = arithmetic.normalize(direction);
    const Vector right = arithmetic.normalize({{direction[2u], 0.0f, -direction[0u]}});
    const Vector up = {{
        arithmetic.cross_component(direction, right, 1u, 2u),
        arithmetic.cross_component(direction, right, 2u, 0u),
        arithmetic.cross_component(direction, right, 0u, 1u),
    }};

    return {{
        right[0u], up[0u], direction[0u], 0.0f,
        right[1u], up[1u], direction[1u], 0.0f,
        right[2u], up[2u], direction[2u], 0.0f,
        arithmetic.translation(view.eye, right, true),
        arithmetic.translation(view.eye, up, true),
        arithmetic.translation(view.eye, direction, false), 1.0f,
    }};
}

CameraMatrix make_camera_view_matrix(
    const CameraViewParameters& view, CameraPrecision precision, const CameraMatrixBackend& backend) {
    const CameraMatrix basis = make_camera_view_basis(view, precision);
    const CameraMatrix rotation = backend.rotation_z(roll_radians(view.roll_angle, precision), precision);
    return backend.multiply(basis, rotation, precision);
}

CameraMatrix make_camera_projection_matrix(
    const CameraProjection& projection, CameraPrecision precision, const CameraMatrixBackend& backend) {
    require_precision(precision);
    return backend.perspective_fov_rh(projection, precision);
}
