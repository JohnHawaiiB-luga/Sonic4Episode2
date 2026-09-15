#include "normal_camera_view.h"

#include <cmath>
#include <stdexcept>

namespace {

constexpr float kInputLimit = 16777216.0f;
constexpr float kZoom = 1.0f;
constexpr float kLateFixedY = 49.58000183105469f;
constexpr float kEyeZOffset = 551.7836303710938f;
constexpr float kTargetYOffset = -4.0f;
constexpr float kTargetZMagnitude = 50.0f;

[[noreturn]] void fail(const char* message) {
    throw std::invalid_argument(message);
}

void require_precision(CameraPrecision precision) {
    if (precision != CameraPrecision::Single && precision != CameraPrecision::Double) {
        fail("normal camera view precision is unsupported");
    }
}

void require_finite(float value, const char* message) {
    if (!std::isfinite(value) || std::fabs(value) > kInputLimit) {
        fail(message);
    }
}

void require_position(const std::array<float, 3u>& position) {
    for (const float component : position) {
        require_finite(component, "normal camera view internal position is invalid");
    }
}

void require_viewport(std::int32_t width, std::int32_t height) {
    if (width <= 0 || height <= 0) {
        fail("normal camera viewport dimensions are invalid");
    }
}

void require_map_bounds(const NormalCameraMapBounds& map_bounds) {
    const float left = static_cast<float>(map_bounds.left);
    const float top = static_cast<float>(map_bounds.top);
    const float right = static_cast<float>(map_bounds.right);
    const float bottom = static_cast<float>(map_bounds.bottom);
    if (!std::isfinite(left) || !std::isfinite(top) || !std::isfinite(right) || !std::isfinite(bottom) ||
        map_bounds.left > map_bounds.right || map_bounds.top > map_bounds.bottom) {
        fail("normal camera map bounds are invalid");
    }
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

float midpoint(float lower, float upper, const Arithmetic& arithmetic) {
    return arithmetic.spill(arithmetic.multiply(arithmetic.add(lower, upper), 0.5));
}

void clamp_eye_to_map(
    std::array<float, 3u>& eye,
    std::int32_t viewport_width,
    std::int32_t viewport_height,
    float camera_scale,
    const NormalCameraMapBounds& map_bounds,
    const Arithmetic& arithmetic) {
    const float span_x = arithmetic.spill(arithmetic.multiply(
        static_cast<double>(camera_scale), static_cast<double>(viewport_width)));
    const float span_y = arithmetic.spill(arithmetic.multiply(
        static_cast<double>(camera_scale), static_cast<double>(viewport_height)));
    const double half_x = arithmetic.multiply(0.5, span_x);
    const double half_y = arithmetic.multiply(0.5, span_y);
    const float minimum_x = arithmetic.spill(arithmetic.add(static_cast<double>(map_bounds.left), half_x));
    const float maximum_x = arithmetic.spill(arithmetic.subtract(static_cast<double>(map_bounds.right), half_x));
    const float maximum_y = arithmetic.spill(-arithmetic.spill(
        arithmetic.add(static_cast<double>(map_bounds.top), half_y)));
    const float minimum_y = arithmetic.spill(-arithmetic.spill(
        arithmetic.subtract(static_cast<double>(map_bounds.bottom), half_y)));

    if (eye[0u] < minimum_x) {
        eye[0u] = minimum_x;
        if (eye[0u] > maximum_x) {
            eye[0u] = midpoint(minimum_x, maximum_x, arithmetic);
        }
    } else if (eye[0u] > maximum_x) {
        eye[0u] = maximum_x;
        if (eye[0u] < minimum_x) {
            eye[0u] = midpoint(minimum_x, maximum_x, arithmetic);
        }
    }
    if (eye[1u] > maximum_y) {
        eye[1u] = maximum_y;
    }
    if (eye[1u] < minimum_y) {
        eye[1u] = minimum_y;
    }
}

std::array<float, 3u> apply_zoom_one_split(
    const std::array<float, 3u>& eye,
    const std::array<float, 3u>& target,
    const Arithmetic& arithmetic) {
    const float ratio = arithmetic.spill(arithmetic.divide(
        eye[2u], arithmetic.subtract(target[2u], eye[2u])));

    const float perspective_x = arithmetic.spill(arithmetic.multiply(
        arithmetic.subtract(target[0u], eye[0u]), ratio));
    const float perspective_y = arithmetic.spill(arithmetic.multiply(
        arithmetic.subtract(target[1u], eye[1u]), ratio));
    const double perspective_z = arithmetic.multiply(
        arithmetic.subtract(target[2u], eye[2u]), ratio);
    const float stored_perspective_z = arithmetic.spill(perspective_z);

    const float base_x = arithmetic.spill(arithmetic.subtract(
        eye[0u], arithmetic.multiply(arithmetic.subtract(target[0u], eye[0u]), ratio)));
    const float base_y = arithmetic.spill(arithmetic.subtract(
        eye[1u], arithmetic.multiply(arithmetic.subtract(target[1u], eye[1u]), ratio)));
    const float base_z = arithmetic.spill(arithmetic.subtract(eye[2u], perspective_z));

    return {{
        arithmetic.spill(arithmetic.add(arithmetic.multiply(kZoom, perspective_x), base_x)),
        arithmetic.spill(arithmetic.add(arithmetic.multiply(kZoom, perspective_y), base_y)),
        arithmetic.spill(arithmetic.add(arithmetic.multiply(kZoom, stored_perspective_z), base_z)),
    }};
}

}

CameraViewInput make_normal_camera_view(
    const std::array<float, 3u>& internal_position,
    std::int32_t viewport_width,
    std::int32_t viewport_height,
    const NormalCameraMapBounds& map_bounds,
    CameraPrecision precision) {
    require_precision(precision);
    require_position(internal_position);
    require_viewport(viewport_width, viewport_height);
    require_map_bounds(map_bounds);

    const Arithmetic arithmetic{precision};
    const float aspect = arithmetic.spill(arithmetic.divide(
        static_cast<double>(viewport_width), static_cast<double>(viewport_height)));
    const CameraProjectionDefaults defaults = make_normal_main_camera_projection_defaults(aspect);
    std::array<float, 3u> eye{{
        arithmetic.spill(arithmetic.add(0.0, internal_position[0u])),
        arithmetic.spill(arithmetic.add(0.0, internal_position[1u])),
        arithmetic.spill(arithmetic.add(kEyeZOffset, internal_position[2u])),
    }};
    clamp_eye_to_map(
        eye, viewport_width, viewport_height, defaults.initial_scale, map_bounds, arithmetic);
    eye[0u] = arithmetic.spill(arithmetic.add(eye[0u], 0.0));
    eye[1u] = arithmetic.spill(arithmetic.add(eye[1u], kLateFixedY));

    std::array<float, 3u> target{{
        arithmetic.spill(arithmetic.add(eye[0u], 0.0)),
        arithmetic.spill(arithmetic.add(eye[1u], kTargetYOffset)),
        arithmetic.spill(arithmetic.add(arithmetic.subtract(0.0, kTargetZMagnitude), eye[2u])),
    }};
    eye = apply_zoom_one_split(eye, target, arithmetic);

    return {defaults.fov_angle,
            defaults.aspect,
            defaults.near_plane,
            defaults.far_plane,
            eye,
            target,
            std::nullopt,
            0};
}
