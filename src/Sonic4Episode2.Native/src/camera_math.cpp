#include "camera_math.h"

#include <cmath>
#include <stdexcept>

namespace {

constexpr float kInputLimit = 16777216.0f;
constexpr std::int32_t kNormalFovAngle = 0xE0F;
constexpr double kAngleToRadians = 0x1.921fb60000000p-14;
constexpr double kScaleMultiplier = 0x1.32eb5c0000000p-2;
constexpr double kScaleDeadband = 0x1p-23;

[[noreturn]] void fail(const char* message) {
    throw std::invalid_argument(message);
}

void require_supported_precision(CameraPrecision precision) {
    if (precision != CameraPrecision::Single && precision != CameraPrecision::Double) {
        fail("camera precision is unsupported");
    }
}

void require_finite_bounded(float value, const char* message) {
    if (!std::isfinite(value) || std::fabs(value) > kInputLimit) {
        fail(message);
    }
}

void require_positive_finite_bounded(float value, const char* message) {
    if (!std::isfinite(value) || value <= 0.0f || value > kInputLimit) {
        fail(message);
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
};

void require_scale_inputs(const CameraScaleState& state, float camera_scale) {
    require_finite_bounded(state.current, "camera scale current is nonfinite or out of range");
    require_finite_bounded(state.target, "camera scale target is nonfinite or out of range");
    require_finite_bounded(state.speed, "camera scale speed is nonfinite or out of range");
    require_finite_bounded(camera_scale, "camera scale output is nonfinite or out of range");
    if (state.speed < 0.0f) {
        fail("camera scale speed is negative");
    }
}

}

CameraProjectionDefaults make_normal_main_camera_projection_defaults(float aspect) {
    require_positive_finite_bounded(aspect, "camera projection aspect is nonfinite or out of range");
    return {
        kNormalFovAngle,
        aspect,
        1.0f,
        60000.0f,
        static_cast<float>(kScaleMultiplier),
    };
}

CameraProjection make_camera_projection(
    std::int32_t angle,
    float aspect,
    float near_plane,
    float far_plane,
    CameraPrecision precision) {
    require_supported_precision(precision);
    if (angle <= 0 || angle >= 32768) {
        fail("camera projection angle is out of range");
    }
    require_positive_finite_bounded(aspect, "camera projection aspect is nonfinite or out of range");
    require_positive_finite_bounded(near_plane, "camera projection near plane is nonfinite or out of range");
    require_positive_finite_bounded(far_plane, "camera projection far plane is nonfinite or out of range");
    if (far_plane <= near_plane) {
        fail("camera projection far plane must exceed near plane");
    }

    const Arithmetic arithmetic{precision};
    return {
        static_cast<float>(arithmetic.multiply(static_cast<double>(angle), kAngleToRadians)),
        aspect,
        near_plane,
        far_plane,
    };
}

void update_camera_scale(CameraScaleState& state, float& camera_scale, CameraPrecision precision) {
    require_supported_precision(precision);
    require_scale_inputs(state, camera_scale);

    const Arithmetic arithmetic{precision};
    const double lower = arithmetic.subtract(static_cast<double>(state.target), kScaleDeadband);
    const double upper = arithmetic.add(static_cast<double>(state.target), kScaleDeadband);
    const double current = static_cast<double>(state.current);
    if (current >= lower && current <= upper) {
        return;
    }

    float next = 0.0f;
    if (current < lower) {
        next = static_cast<float>(arithmetic.add(current, static_cast<double>(state.speed)));
        if (next > state.target) {
            next = state.target;
        }
    } else {
        next = static_cast<float>(arithmetic.subtract(current, static_cast<double>(state.speed)));
        if (next < state.target) {
            next = state.target;
        }
    }

    state.current = next;
    camera_scale = static_cast<float>(arithmetic.multiply(static_cast<double>(state.current), kScaleMultiplier));
}
