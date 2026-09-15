#include "camera_matrix.h"

#include <array>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <limits>
#include <stdexcept>
#include <vector>

namespace {

bool check(bool condition, const char* message) {
    if (!condition) {
        std::fprintf(stderr, "%s\n", message);
    }
    return condition;
}

std::uint32_t bits(float value) {
    std::uint32_t result = 0u;
    std::memcpy(&result, &value, sizeof(result));
    return result;
}

CameraViewParameters view() {
    return {0u, 0xE0F, 16.0f / 9.0f, 1.0f, 60000.0f, {{0.0f, 0.0f, 10.0f}}, {{0.0f, 0.0f, 0.0f}}, 0};
}

class RecordingBackend final : public CameraMatrixBackend {
public:
    mutable std::vector<int> calls;
    mutable float angle = 0.0f;
    mutable CameraMatrix left{};
    mutable CameraMatrix right{};
    mutable CameraProjection projection{};
    mutable CameraPrecision selected = CameraPrecision::Single;
    CameraMatrix rotation_result{};
    CameraMatrix multiply_result{};
    CameraMatrix projection_result{};

    CameraMatrix rotation_z(float radians, CameraPrecision precision) const override {
        calls.push_back(1);
        angle = radians;
        selected = precision;
        return rotation_result;
    }

    CameraMatrix multiply(const CameraMatrix& a, const CameraMatrix& b, CameraPrecision precision) const override {
        calls.push_back(2);
        left = a;
        right = b;
        selected = precision;
        return multiply_result;
    }

    CameraMatrix perspective_fov_rh(const CameraProjection& value, CameraPrecision precision) const override {
        calls.push_back(3);
        projection = value;
        selected = precision;
        return projection_result;
    }
};

bool test_basis() {
    const auto input = view();
    const auto actual = make_camera_view_basis(input, CameraPrecision::Single);
    const std::array<std::uint32_t, 16u> expected = {{
        0x3f800000u, 0x80000000u, 0u, 0u,
        0u, 0x3f800000u, 0u, 0u,
        0x80000000u, 0u, 0x3f800000u, 0u,
        0x80000000u, 0x80000000u, 0xc1200000u, 0x3f800000u,
    }};
    for (std::size_t i = 0u; i < actual.size(); ++i) {
        if (!check(bits(actual[i]) == expected[i], "axis-aligned view basis or signed zero differs")) {
            return false;
        }
    }
    return check(input.eye == view().eye && input.target == view().target, "view input changed");
}

bool test_precision_and_degenerate_basis() {
    auto input = view();
    input.eye = {{50.0f, 0.0f, 50.0f}};
    const auto single = make_camera_view_basis(input, CameraPrecision::Single);
    const auto extended = make_camera_view_basis(input, CameraPrecision::Double);
    if (!check(bits(single[2u]) == 0x3f3504f4u && bits(extended[2u]) == 0x3f3504f3u,
               "normalization precision was lost")) {
        return false;
    }
    input.eye = input.target;
    const auto coincident = make_camera_view_basis(input, CameraPrecision::Single);
    input.eye = {{0.0f, 1.0f, 0.0f}};
    const auto vertical = make_camera_view_basis(input, CameraPrecision::Single);
    return check(coincident[0u] == 0.0f && coincident[5u] == 0.0f && coincident[10u] == 0.0f
                 && coincident[15u] == 1.0f, "coincident view invented a fallback basis")
        && check(vertical[0u] == 0.0f && vertical[5u] == 0.0f && vertical[6u] == 1.0f,
                 "vertical view invented an alternate up vector");
}

bool test_composition() {
    RecordingBackend backend;
    backend.rotation_result[4u] = 7.0f;
    backend.multiply_result[8u] = 9.0f;
    auto input = view();
    const auto result = make_camera_view_matrix(input, CameraPrecision::Single, backend);
    if (!check(result == backend.multiply_result && backend.calls == std::vector<int>{1, 2}
               && bits(backend.angle) == 0u && backend.right == backend.rotation_result
               && backend.left == make_camera_view_basis(input, CameraPrecision::Single),
               "zero roll skipped or reordered backend operations")) {
        return false;
    }
    input.roll_angle = 16384;
    make_camera_view_matrix(input, CameraPrecision::Double, backend);
    if (!check(bits(backend.angle) == 0xbfc90fdbu && backend.selected == CameraPrecision::Double,
               "roll sign or angle units differ")) {
        return false;
    }
    input.roll_angle = std::numeric_limits<std::int32_t>::min();
    make_camera_view_matrix(input, CameraPrecision::Single, backend);
    return check(bits(backend.angle) == 0xc8490fdbu, "minimum signed roll did not wrap");
}

bool test_projection_and_invalid_precision() {
    RecordingBackend backend;
    backend.projection_result[0u] = 3.0f;
    const CameraProjection projection{0.3f, 1.7f, 1.0f, 60000.0f};
    const auto result = make_camera_projection_matrix(projection, CameraPrecision::Double, backend);
    if (!check(result == backend.projection_result && backend.calls == std::vector<int>{3}
               && bits(backend.projection.fov_radians) == bits(projection.fov_radians)
               && bits(backend.projection.aspect) == bits(projection.aspect)
               && bits(backend.projection.near_plane) == bits(projection.near_plane)
               && bits(backend.projection.far_plane) == bits(projection.far_plane),
               "projection arguments changed")) {
        return false;
    }
    try {
        make_camera_view_basis(view(), static_cast<CameraPrecision>(99));
    } catch (const std::invalid_argument&) {
        return true;
    }
    return check(false, "unsupported precision was accepted");
}

}

int main() {
    return test_basis() && test_precision_and_degenerate_basis() && test_composition()
        && test_projection_and_invalid_precision() ? 0 : 1;
}
