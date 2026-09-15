#include "stage_transform.h"

#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <limits>
#include <utility>

namespace {

bool check(bool condition, const char* message) {
    if (condition) {
        return true;
    }
    std::fprintf(stderr, "%s\n", message);
    return false;
}

bool check_close(float actual, float expected, const char* message) {
    return check(std::fabs(actual - expected) <= 0.00001f, message);
}

std::uint32_t float_bits(float value) {
    std::uint32_t bits = 0u;
    std::memcpy(&bits, &value, sizeof(bits));
    return bits;
}

template <typename Callable>
bool expects_stage_data_error(Callable&& callable, const char* message) {
    try {
        std::forward<Callable>(callable)();
    } catch (const StageDataError&) {
        return true;
    } catch (...) {
        std::fprintf(stderr, "%s (wrong exception type)\n", message);
        return false;
    }

    std::fprintf(stderr, "%s\n", message);
    return false;
}

bool test_identity_scale_pivot_cancellation() {
    const StageMapPlacement placement = {0u, 0u, 1u, 0u, false, false};
    const std::array<float, 16u> zero_pivot = make_pc_stage_transform(
        placement,
        {0.0f, 0.0f, 0.0f},
        {0.0f, 0.0f, 0.0f},
        false, StageTransformPrecision::Double);
    StageMapPlacement quarter_turn = placement;
    quarter_turn.rotation = 0x4000u;
    const std::array<float, 16u> quarter_turn_matrix = make_pc_stage_transform(
        quarter_turn,
        {0.0f, 0.0f, 0.0f},
        {0.0f, 0.0f, 0.0f},
        false, StageTransformPrecision::Double);
    const std::array<float, 16u> matrix = make_pc_stage_transform(
        placement,
        {0.0f, -20.0f, 0.0f},
        {0.0f, 0.0f, 0.0f},
        false, StageTransformPrecision::Double);

    return check(float_bits(zero_pivot[12u]) == 0xB5000000u,
                 "zero-pivot X rounding mismatch") &&
           check(float_bits(zero_pivot[13u]) == 0xC2800000u,
                 "zero-pivot Y rounding mismatch") &&
           check(float_bits(zero_pivot[14u]) == 0x00000000u,
                 "zero-pivot Z rounding mismatch") &&
           check(float_bits(zero_pivot[15u]) == 0x3F800000u,
                 "zero-pivot homogeneous element mismatch") &&
           check(float_bits(quarter_turn_matrix[0u]) == 0x00000000u,
                 "quarter-turn X signed zero mismatch") &&
           check(float_bits(quarter_turn_matrix[1u]) == 0x404CCCCDu &&
                       float_bits(quarter_turn_matrix[4u]) == 0xC04CCCCDu,
                 "quarter-turn XY basis mismatch") &&
           check(float_bits(quarter_turn_matrix[5u]) == 0x80000000u &&
                       float_bits(quarter_turn_matrix[6u]) == 0x80000000u,
                 "quarter-turn signed zero basis mismatch") &&
           check(float_bits(quarter_turn_matrix[12u]) == 0x42800000u &&
                       float_bits(quarter_turn_matrix[13u]) == 0xC2800000u &&
                       float_bits(quarter_turn_matrix[14u]) == 0x00000000u &&
                       float_bits(quarter_turn_matrix[15u]) == 0x3F800000u,
                 "quarter-turn translation mismatch") &&
           check(matrix[0u] == 3.2f && matrix[5u] == 3.2f && matrix[10u] == 3.2f,
                 "identity scale diagonal mismatch") &&
           check(matrix[15u] == 1.0f, "identity scale homogeneous element mismatch") &&
           check(float_bits(matrix[12u]) == 0xB5000000u,
                 "pivot cancellation X rounding mismatch") &&
           check(float_bits(matrix[13u]) == 0x35000000u,
                 "pivot cancellation Y rounding mismatch") &&
           check(matrix[14u] == 0.0f, "pivot cancellation Z mismatch");
}

bool test_offsets_depth_and_model_id_independence() {
    const StageMapPlacement placement = {2u, 3u, 7u, 0u, false, false};
    const std::array<float, 3u> pivot = {0.0f, 0.0f, 0.0f};
    const std::array<float, 3u> offsets = {1.25f, -2.5f, 7.75f};
    const std::array<float, 16u> upper = make_pc_stage_transform(
        placement, pivot, offsets, false, StageTransformPrecision::Double);
    const std::array<float, 16u> lower = make_pc_stage_transform(
        placement, pivot, offsets, true, StageTransformPrecision::Double);
    StageMapPlacement other_model = placement;
    other_model.model_id = 0x0FFFu;

    return check_close(upper[12u], 129.25f, "X offset transform mismatch") &&
           check_close(upper[13u], -253.5f, "Y offset transform mismatch") &&
           check_close(upper[14u], 7.75f, "Z offset transform mismatch") &&
           check_close(lower[12u], upper[12u], "lower-depth X changed") &&
           check_close(lower[13u], upper[13u], "lower-depth Y changed") &&
           check_close(lower[14u], upper[14u] - 32.0f, "lower-depth Z adjustment mismatch") &&
           check(make_pc_stage_transform(other_model, pivot, offsets, false, StageTransformPrecision::Double) == upper,
                 "model ID changed the transform");
}

bool test_all_quarter_turn_and_flip_combinations() {
    struct RotationCase {
        std::uint16_t rotation;
        bool flip_x;
        bool flip_y;
        float xx;
        float xy;
        float yx;
        float yy;
        float translation_x;
        float translation_y;
    };

    const std::array<RotationCase, 16u> cases = {{
        {0x0000u, false, false, 3.2f, 0.0f, 0.0f, 3.2f, 0.0f, -64.0f},
        {0x0000u, true, false, -3.2f, 0.0f, 0.0f, 3.2f, 64.0f, -64.0f},
        {0x0000u, false, true, 3.2f, 0.0f, 0.0f, -3.2f, 0.0f, 0.0f},
        {0x0000u, true, true, -3.2f, 0.0f, 0.0f, -3.2f, 64.0f, 0.0f},
        {0x4000u, false, false, 0.0f, 3.2f, -3.2f, 0.0f, 64.0f, -64.0f},
        {0x4000u, true, false, 0.0f, -3.2f, -3.2f, 0.0f, 64.0f, 0.0f},
        {0x4000u, false, true, 0.0f, 3.2f, 3.2f, 0.0f, 0.0f, -64.0f},
        {0x4000u, true, true, 0.0f, -3.2f, 3.2f, 0.0f, 0.0f, 0.0f},
        {0x8000u, false, false, -3.2f, 0.0f, 0.0f, -3.2f, 64.0f, 0.0f},
        {0x8000u, true, false, 3.2f, 0.0f, 0.0f, -3.2f, 0.0f, 0.0f},
        {0x8000u, false, true, -3.2f, 0.0f, 0.0f, 3.2f, 64.0f, -64.0f},
        {0x8000u, true, true, 3.2f, 0.0f, 0.0f, 3.2f, 0.0f, -64.0f},
        {0xC000u, false, false, 0.0f, -3.2f, 3.2f, 0.0f, 0.0f, 0.0f},
        {0xC000u, true, false, 0.0f, 3.2f, 3.2f, 0.0f, 0.0f, -64.0f},
        {0xC000u, false, true, 0.0f, -3.2f, -3.2f, 0.0f, 64.0f, 0.0f},
        {0xC000u, true, true, 0.0f, 3.2f, -3.2f, 0.0f, 64.0f, -64.0f},
    }};

    for (const RotationCase& expected : cases) {
        const StageMapPlacement placement = {
            0u,
            0u,
            1u,
            expected.rotation,
            expected.flip_x,
            expected.flip_y,
        };
        const std::array<float, 16u> matrix = make_pc_stage_transform(
            placement,
            {0.0f, 0.0f, 0.0f},
            {0.0f, 0.0f, 0.0f},
            false, StageTransformPrecision::Double);
        if (!check_close(matrix[0u], expected.xx, "quarter-turn X basis mismatch") ||
            !check_close(matrix[1u], expected.xy, "quarter-turn X basis mismatch") ||
            !check_close(matrix[4u], expected.yx, "quarter-turn Y basis mismatch") ||
            !check_close(matrix[5u], expected.yy, "quarter-turn Y basis mismatch") ||
            !check_close(matrix[10u], 3.2f, "quarter-turn Z basis mismatch") ||
            !check_close(matrix[12u], expected.translation_x, "quarter-turn X translation mismatch") ||
            !check_close(matrix[13u], expected.translation_y, "quarter-turn Y translation mismatch") ||
            !check_close(matrix[14u], 0.0f, "quarter-turn Z translation mismatch") ||
            !check(matrix[3u] == 0.0f && matrix[7u] == 0.0f && matrix[11u] == 0.0f &&
                       matrix[15u] == 1.0f,
                   "quarter-turn homogeneous row mismatch")) {
            return false;
        }
    }

    return true;
}

bool test_rejects_invalid_input_and_accepts_boundaries() {
    constexpr float kInputLimit = 16777216.0f;
    const StageMapPlacement valid = {1u, 1u, 1u, 0u, false, false};
    const std::array<float, 3u> pivot = {0.0f, 0.0f, 0.0f};
    const std::array<float, 3u> offsets = {0.0f, 0.0f, 0.0f};
    const std::array<std::uint16_t, 4u> invalid_rotations = {{
        0x0001u,
        0x2000u,
        0x6000u,
        0xFFFFu,
    }};

    for (const std::uint16_t rotation : invalid_rotations) {
        StageMapPlacement invalid = valid;
        invalid.rotation = rotation;
        if (!expects_stage_data_error(
                [&]() { make_pc_stage_transform(invalid, pivot, offsets, false, StageTransformPrecision::Double); },
                "unsupported rotation was accepted")) {
            return false;
        }
    }

    const std::array<float, 3u> nonfinite_values = {{
        std::numeric_limits<float>::quiet_NaN(),
        std::numeric_limits<float>::infinity(),
        -std::numeric_limits<float>::infinity(),
    }};
    for (const float value : nonfinite_values) {
        for (std::size_t index = 0u; index < pivot.size(); ++index) {
            std::array<float, 3u> invalid_pivot = pivot;
            std::array<float, 3u> invalid_offsets = offsets;
            invalid_pivot[index] = value;
            invalid_offsets[index] = value;
            if (!expects_stage_data_error(
                    [&]() { make_pc_stage_transform(valid, invalid_pivot, offsets, false, StageTransformPrecision::Double); },
                    "nonfinite pivot was accepted") ||
                !expects_stage_data_error(
                    [&]() { make_pc_stage_transform(valid, pivot, invalid_offsets, false, StageTransformPrecision::Double); },
                    "nonfinite offset was accepted")) {
                return false;
            }
        }
    }

    const float outside_limit = std::nextafter(
        kInputLimit,
        std::numeric_limits<float>::infinity());
    for (const float value : {outside_limit, -outside_limit}) {
        std::array<float, 3u> invalid_pivot = pivot;
        std::array<float, 3u> invalid_offsets = offsets;
        invalid_pivot[0u] = value;
        invalid_offsets[0u] = value;
        if (!expects_stage_data_error(
                [&]() { make_pc_stage_transform(valid, invalid_pivot, offsets, false, StageTransformPrecision::Double); },
                "out-of-range pivot was accepted") ||
            !expects_stage_data_error(
                [&]() { make_pc_stage_transform(valid, pivot, invalid_offsets, false, StageTransformPrecision::Double); },
                "out-of-range offset was accepted")) {
            return false;
        }
    }

    const std::array<float, 16u> matrix = make_pc_stage_transform(
        valid,
        {kInputLimit, -kInputLimit, kInputLimit},
        {-kInputLimit, kInputLimit, -kInputLimit},
        true, StageTransformPrecision::Double);
    for (const float value : matrix) {
        if (!check(std::isfinite(value), "exact input boundary produced a nonfinite matrix")) {
            return false;
        }
    }
    return true;
}

bool test_explicit_single_precision() {
    const StageMapPlacement base = {0u, 0u, 1u, 0u, false, false};
    for (const std::uint16_t rotation : std::array<std::uint16_t, 4u>{{0u, 0x4000u, 0x8000u, 0xC000u}}) {
        StageMapPlacement placement = base;
        placement.rotation = rotation;
        const auto single = make_pc_stage_transform(
            placement, {0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 0.0f}, false, StageTransformPrecision::Single);
        const auto wide = make_pc_stage_transform(
            placement, {0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 0.0f}, false, StageTransformPrecision::Double);
        for (std::size_t i = 0u; i < single.size(); ++i) {
            const std::uint32_t expected = i == 12u ? (rotation == 0u || rotation == 0xC000u ? 0u : 0x42800000u) :
                                          i == 13u ? (rotation < 0x8000u ? 0xC2800000u : 0u) : float_bits(wide[i]);
            if (!check(float_bits(single[i]) == expected, "single-precision matrix bits differ")) {
                return false;
            }
        }
    }
    const float unit = std::numeric_limits<float>::denorm_min();
    const auto subnormal = make_pc_stage_transform(
        base, {0.0f, 0.0f, 2.0f * unit}, {0.0f, 0.0f, 6.0f * unit}, false, StageTransformPrecision::Single);
    return check(float_bits(subnormal[14u]) == 0x80000000u, "single-precision intermediate underflowed early") &&
           expects_stage_data_error(
               [&]() { make_pc_stage_transform(base, {0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 0.0f}, false,
                                                static_cast<StageTransformPrecision>(2)); },
               "unsupported precision was accepted");
}

}

int main() {
    return test_explicit_single_precision() &&
                   test_identity_scale_pivot_cancellation() &&
                   test_offsets_depth_and_model_id_independence() &&
                   test_all_quarter_turn_and_flip_combinations() &&
                   test_rejects_invalid_input_and_accepts_boundaries()
               ? 0
               : 1;
}
