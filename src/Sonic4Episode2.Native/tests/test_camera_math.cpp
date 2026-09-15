#include "camera_math.h"

#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <limits>
#include <stdexcept>
#include <utility>

namespace {

bool check(bool condition, const char* message) {
    if (condition) {
        return true;
    }
    std::fprintf(stderr, "%s\n", message);
    return false;
}

float float_from_bits(std::uint32_t bits) {
    float value = 0.0f;
    std::memcpy(&value, &bits, sizeof(value));
    return value;
}

std::uint32_t float_bits(float value) {
    std::uint32_t bits = 0u;
    std::memcpy(&bits, &value, sizeof(bits));
    return bits;
}

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

struct ScaleSnapshot {
    std::uint32_t current;
    std::uint32_t target;
    std::uint32_t speed;
    std::uint32_t camera_scale;
};

ScaleSnapshot snapshot_scale(const CameraScaleState& state, float camera_scale) {
    return {
        float_bits(state.current),
        float_bits(state.target),
        float_bits(state.speed),
        float_bits(camera_scale),
    };
}

bool same_scale(const ScaleSnapshot& expected, const CameraScaleState& state, float camera_scale) {
    const ScaleSnapshot actual = snapshot_scale(state, camera_scale);
    return expected.current == actual.current && expected.target == actual.target &&
           expected.speed == actual.speed && expected.camera_scale == actual.camera_scale;
}

bool test_normal_defaults_and_projection_arguments() {
    const float aspect = float_from_bits(0x3FE38E39u);
    const CameraProjectionDefaults defaults = make_normal_main_camera_projection_defaults(aspect);
    bool passed = true;

    passed = check(defaults.fov_angle == 0xE0F, "normal default FOV angle mismatch") && passed;
    passed = check(float_bits(defaults.aspect) == 0x3FE38E39u, "normal default aspect was not copied") && passed;
    passed = check(float_bits(defaults.near_plane) == 0x3F800000u, "normal default near plane mismatch") && passed;
    passed = check(float_bits(defaults.far_plane) == 0x476A6000u, "normal default far plane mismatch") && passed;
    passed = check(float_bits(defaults.initial_scale) == 0x3E9975AEu, "normal default scale mismatch") && passed;

    for (const CameraPrecision precision : std::array<CameraPrecision, 2u>{{
             CameraPrecision::Single,
             CameraPrecision::Double,
         }}) {
        const CameraProjection normal = make_camera_projection(
            defaults.fov_angle,
            defaults.aspect,
            defaults.near_plane,
            defaults.far_plane,
            precision);
        const CameraProjection wide = make_camera_projection(
            0x1FFF,
            defaults.aspect,
            defaults.near_plane,
            defaults.far_plane,
            precision);

        passed = check(float_bits(normal.fov_radians) == 0x3EB0AA5Eu,
                       "normal angle-to-radian conversion mismatch") &&
                 passed;
        passed = check(float_bits(wide.fov_radians) == 0x3F490993u,
                       "wide angle-to-radian conversion mismatch") &&
                 passed;
        passed = check(float_bits(normal.aspect) == float_bits(defaults.aspect) &&
                           float_bits(normal.near_plane) == float_bits(defaults.near_plane) &&
                           float_bits(normal.far_plane) == float_bits(defaults.far_plane),
                       "normal projection parameters changed") &&
                 passed;
        passed = check(float_bits(wide.aspect) == float_bits(defaults.aspect) &&
                           float_bits(wide.near_plane) == float_bits(defaults.near_plane) &&
                           float_bits(wide.far_plane) == float_bits(defaults.far_plane),
                       "wide projection parameters changed") &&
                 passed;
    }
    return passed;
}

bool test_scale_deadband_keeps_both_outputs_unchanged() {
    const float sentinel = -123.0f;
    const std::array<CameraScaleState, 3u> cases = {{
        {1.0f, 1.0f, 0.05f},
        {1.0f, 1.0f - 0x1p-23f, 0.5f},
        {1.0f, 1.0f + 0x1p-23f, 0.5f},
    }};

    bool passed = true;
    for (const CameraPrecision precision : std::array<CameraPrecision, 2u>{{
             CameraPrecision::Single,
             CameraPrecision::Double,
         }}) {
        for (const CameraScaleState& expected_state : cases) {
            CameraScaleState state = expected_state;
            float camera_scale = sentinel;
            const ScaleSnapshot before = snapshot_scale(state, camera_scale);

            update_camera_scale(state, camera_scale, precision);

            passed = check(same_scale(before, state, camera_scale),
                           "inclusive scale deadband changed state or camera scale") &&
                     passed;
        }
    }
    return passed;
}

bool test_precision_selects_deadband_boundary() {
    struct BoundaryCase {
        CameraScaleState initial;
        std::uint32_t double_camera_scale;
        const char* message;
    };

    const std::array<BoundaryCase, 2u> cases = {{
        {{float_from_bits(0x3D800010u), float_from_bits(0x3D7FFFFFu), 0.0f},
         0x3C9975C1u,
         "upper precision boundary mismatch"},
        {{float_from_bits(0x40000000u), float_from_bits(0x40000001u), 0.0f},
         0x3F1975AEu,
         "lower precision boundary mismatch"},
    }};

    bool passed = true;
    for (const BoundaryCase& expected : cases) {
        CameraScaleState single_state = expected.initial;
        float single_camera_scale = -123.0f;
        const ScaleSnapshot single_before = snapshot_scale(single_state, single_camera_scale);

        update_camera_scale(single_state, single_camera_scale, CameraPrecision::Single);

        passed = check(same_scale(single_before, single_state, single_camera_scale),
                       "single precision did not preserve the deadband sentinel") &&
                 passed;

        CameraScaleState double_state = expected.initial;
        float double_camera_scale = -123.0f;
        const ScaleSnapshot double_before = snapshot_scale(double_state, double_camera_scale);

        update_camera_scale(double_state, double_camera_scale, CameraPrecision::Double);

        passed = check(float_bits(double_state.current) == double_before.current &&
                           float_bits(double_state.target) == double_before.target &&
                           float_bits(double_state.speed) == double_before.speed,
                       "zero-speed double precision boundary changed scale state") &&
                 passed;
        passed = check(float_bits(double_camera_scale) == expected.double_camera_scale, expected.message) && passed;
    }
    return passed;
}

bool test_scale_step_overshoot_and_zero_speed() {
    struct ScaleCase {
        CameraScaleState initial;
        std::uint32_t expected_current;
        std::uint32_t expected_camera_scale;
        const char* message;
    };

    const std::array<ScaleCase, 5u> cases = {{
        {{1.0f, 2.0f, 0.125f}, 0x3F900000u, 0x3EACA464u, "increasing scale step mismatch"},
        {{2.0f, 1.0f, 0.125f}, 0x3FF00000u, 0x3F0FDE53u, "decreasing scale step mismatch"},
        {{1.0f, 1.05f, 0.1f}, 0x3F866666u, 0x3EA121F6u, "increasing scale overshoot mismatch"},
        {{2.0f, 1.95f, 0.1f}, 0x3FF9999Au, 0x3F159F8Au, "decreasing scale overshoot mismatch"},
        {{1.0f, 2.0f, 0.0f}, 0x3F800000u, 0x3E9975AEu, "zero-speed scale update mismatch"},
    }};

    bool passed = true;
    for (const CameraPrecision precision : std::array<CameraPrecision, 2u>{{
             CameraPrecision::Single,
             CameraPrecision::Double,
         }}) {
        for (const ScaleCase& expected : cases) {
            CameraScaleState state = expected.initial;
            float camera_scale = -123.0f;

            update_camera_scale(state, camera_scale, precision);

            passed = check(float_bits(state.current) == expected.expected_current, expected.message) && passed;
            passed = check(float_bits(camera_scale) == expected.expected_camera_scale,
                           "camera scale factor result mismatch") &&
                     passed;
        }
    }
    return passed;
}

bool test_rejects_bad_domains_without_scale_mutation() {
    const CameraScaleState valid = {1.0f, 2.0f, 0.125f};
    const std::array<CameraScaleState, 4u> invalid_states = {{
        {std::numeric_limits<float>::quiet_NaN(), 2.0f, 0.125f},
        {1.0f, std::numeric_limits<float>::infinity(), 0.125f},
        {1.0f, 2.0f, -0.125f},
        {1.0f, 2.0f, std::nextafter(16777216.0f, std::numeric_limits<float>::infinity())},
    }};
    bool passed = true;

    for (const CameraScaleState& invalid : invalid_states) {
        CameraScaleState state = invalid;
        float camera_scale = -123.0f;
        const ScaleSnapshot before = snapshot_scale(state, camera_scale);
        passed = expects_invalid_argument(
                     [&]() { update_camera_scale(state, camera_scale, CameraPrecision::Double); },
                     "invalid scale state was accepted") &&
                 passed;
        passed = check(same_scale(before, state, camera_scale), "invalid scale state mutated an output") && passed;
    }

    {
        CameraScaleState state = valid;
        float camera_scale = std::numeric_limits<float>::quiet_NaN();
        const ScaleSnapshot before = snapshot_scale(state, camera_scale);
        passed = expects_invalid_argument(
                     [&]() { update_camera_scale(state, camera_scale, CameraPrecision::Double); },
                     "nonfinite camera scale was accepted") &&
                 passed;
        passed = check(same_scale(before, state, camera_scale), "nonfinite camera scale mutated an output") && passed;
    }
    {
        CameraScaleState state = valid;
        float camera_scale = -123.0f;
        const ScaleSnapshot before = snapshot_scale(state, camera_scale);
        passed = expects_invalid_argument(
                     [&]() {
                         update_camera_scale(
                             state,
                             camera_scale,
                             static_cast<CameraPrecision>(2));
                     },
                     "unsupported camera precision was accepted") &&
                 passed;
        passed = check(same_scale(before, state, camera_scale),
                       "unsupported camera precision mutated an output") &&
                 passed;
    }

    const float limit = 16777216.0f;
    const float outside_limit = std::nextafter(limit, std::numeric_limits<float>::infinity());
    passed = expects_invalid_argument(
                 []() { make_normal_main_camera_projection_defaults(0.0f); },
                 "zero default aspect was accepted") &&
             passed;
    passed = expects_invalid_argument(
                 [outside_limit]() { make_normal_main_camera_projection_defaults(outside_limit); },
                 "out-of-range default aspect was accepted") &&
             passed;
    for (const std::int32_t angle : std::array<std::int32_t, 3u>{{-1, 0, 32768}}) {
        passed = expects_invalid_argument(
                     [angle]() {
                         make_camera_projection(
                             angle,
                             1.0f,
                             1.0f,
                             60000.0f,
                             CameraPrecision::Double);
                     },
                     "out-of-domain projection angle was accepted") &&
                 passed;
    }
    passed = expects_invalid_argument(
                 []() {
                     make_camera_projection(
                         0xE0F,
                         std::numeric_limits<float>::infinity(),
                         1.0f,
                         60000.0f,
                         CameraPrecision::Double);
                 },
                 "nonfinite projection aspect was accepted") &&
             passed;
    passed = expects_invalid_argument(
                 []() {
                     make_camera_projection(
                         0xE0F,
                         1.0f,
                         60000.0f,
                         60000.0f,
                         CameraPrecision::Double);
                 },
                 "non-increasing projection planes were accepted") &&
             passed;
    passed = expects_invalid_argument(
                 []() {
                     make_camera_projection(
                         0xE0F,
                         1.0f,
                         1.0f,
                         60000.0f,
                         static_cast<CameraPrecision>(2));
                 },
                 "unsupported projection precision was accepted") &&
             passed;
    return passed;
}

}

int main() {
    return test_normal_defaults_and_projection_arguments() &&
                   test_scale_deadband_keeps_both_outputs_unchanged() &&
                   test_precision_selects_deadband_boundary() &&
                   test_scale_step_overshoot_and_zero_speed() &&
                   test_rejects_bad_domains_without_scale_mutation()
               ? 0
               : 1;
}
