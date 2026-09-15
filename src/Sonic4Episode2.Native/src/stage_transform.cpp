#include "stage_transform.h"

#include "nn_trig.h"

#include <cmath>
#include <cstddef>
#include <cstdint>

namespace {

constexpr float kInputLimit = 16777216.0f;
constexpr float kStageScale = 3.2f;

[[noreturn]] void fail(const char* message) {
    throw StageDataError(message);
}

void require_finite_bounded(const std::array<float, 3u>& values, const char* message) {
    for (const float value : values) {
        if (!std::isfinite(value) || std::fabs(value) > kInputLimit) {
            fail(message);
        }
    }
}

void require_supported_rotation(std::uint16_t rotation) {
    if (rotation != 0x0000u && rotation != 0x4000u && rotation != 0x8000u &&
        rotation != 0xC000u) {
        fail("stage transform rotation is unsupported");
    }
}

struct Arithmetic {
    StageTransformPrecision precision;

    double rounded(double value) const {
        if (precision == StageTransformPrecision::Double) {
            return value;
        }
        // x87 precision limits the significand, preserving the exponent until a float store.
        int exponent = 0;
        const float mantissa = static_cast<float>(std::frexp(value, &exponent));
        return std::ldexp(static_cast<double>(mantissa), exponent);
    }

    double add(double left, double right) const { return rounded(left + right); }
    double subtract(double left, double right) const { return rounded(left - right); }
    double multiply(double left, double right) const { return rounded(left * right); }
};

float translated_component(
    const std::array<float, 16u>& matrix,
    std::size_t row,
    float x,
    float y,
    float z,
    const Arithmetic& arithmetic) {
    const double x_product = arithmetic.multiply(matrix[row], x);
    const double y_product = arithmetic.multiply(matrix[row + 4u], y);
    const double z_product = arithmetic.multiply(matrix[row + 8u], z);
    const double xy_sum = row == 0u ? arithmetic.add(y_product, x_product) : arithmetic.add(x_product, y_product);
    const double xyz_sum = arithmetic.add(xy_sum, z_product);
    return static_cast<float>(arithmetic.add(xyz_sum, matrix[row + 12u]));
}

void translate_in_place(std::array<float, 16u>& matrix, float x, float y, float z, const Arithmetic& arithmetic) {
    const float translated_x = translated_component(matrix, 0u, x, y, z, arithmetic);
    const float translated_y = translated_component(matrix, 1u, x, y, z, arithmetic);
    const float translated_z = translated_component(matrix, 2u, x, y, z, arithmetic);
    matrix[12u] = translated_x;
    matrix[13u] = translated_y;
    matrix[14u] = translated_z;
    matrix[15u] = 1.0f;
}

void rotate_z_in_place(std::array<float, 16u>& matrix, std::uint16_t rotation, const Arithmetic& arithmetic) {
    if (rotation == 0u) {
        return;
    }

    const float sine = nn_sin(rotation);
    const float cosine = nn_cos(rotation);
    for (std::size_t row = 0u; row < 3u; ++row) {
        const float old_x = matrix[row];
        const float old_y = matrix[row + 4u];
        const double y_sine = arithmetic.multiply(old_y, sine);
        const double x_cosine = arithmetic.multiply(old_x, cosine);
        const double y_cosine = arithmetic.multiply(old_y, cosine);
        const double x_sine = arithmetic.multiply(old_x, sine);
        matrix[row] = static_cast<float>(arithmetic.add(y_sine, x_cosine));
        matrix[row + 4u] = static_cast<float>(arithmetic.subtract(y_cosine, x_sine));
    }
}

float scaled_component(float value, float scale, const Arithmetic& arithmetic) {
    return static_cast<float>(arithmetic.multiply(value, scale));
}

void scale_in_place(std::array<float, 16u>& matrix, float x, float y, float z, const Arithmetic& arithmetic) {
    for (std::size_t row = 0u; row < 3u; ++row) {
        matrix[row] = scaled_component(matrix[row], x, arithmetic);
        matrix[row + 4u] = scaled_component(matrix[row + 4u], y, arithmetic);
        matrix[row + 8u] = scaled_component(matrix[row + 8u], z, arithmetic);
    }
    matrix[3u] = 0.0f;
    matrix[7u] = 0.0f;
    matrix[11u] = 0.0f;
}

}

std::array<float, 16u> make_pc_stage_transform(
    const StageMapPlacement& placement,
    const std::array<float, 3u>& pivot,
    const std::array<float, 3u>& offsets,
    bool lower_depth,
    StageTransformPrecision precision) {
    if (precision != StageTransformPrecision::Single && precision != StageTransformPrecision::Double) {
        fail("stage transform precision is unsupported");
    }
    require_supported_rotation(placement.rotation);
    require_finite_bounded(pivot, "stage transform pivot is nonfinite or out of range");
    require_finite_bounded(offsets, "stage transform offsets are nonfinite or out of range");

    const Arithmetic arithmetic{precision};
    const float stage_x = static_cast<float>(arithmetic.add(
        arithmetic.multiply(arithmetic.add(placement.anchor_x, 0.5), 64.0), offsets[0u]));
    const float stage_y = static_cast<float>(arithmetic.subtract(
        arithmetic.multiply(arithmetic.subtract(-static_cast<double>(placement.anchor_y), 0.5), 64.0), offsets[1u]));
    const float stage_z = static_cast<float>(arithmetic.subtract(offsets[2u], lower_depth ? 32.0 : 0.0));
    const float pivot_x = static_cast<float>(arithmetic.subtract(-static_cast<double>(pivot[0u]), 10.0));
    const float pivot_y = static_cast<float>(arithmetic.subtract(-static_cast<double>(pivot[1u]), 10.0));
    const float pivot_z = -pivot[2u];
    const float scale_x = placement.flip_x ? -kStageScale : kStageScale;
    const float scale_y = placement.flip_y ? -kStageScale : kStageScale;

    std::array<float, 16u> matrix = {{
        1.0f, 0.0f, 0.0f, 0.0f,
        0.0f, 1.0f, 0.0f, 0.0f,
        0.0f, 0.0f, 1.0f, 0.0f,
        0.0f, 0.0f, 0.0f, 1.0f,
    }};
    translate_in_place(matrix, stage_x, stage_y, stage_z, arithmetic);
    rotate_z_in_place(matrix, placement.rotation, arithmetic);
    scale_in_place(matrix, scale_x, scale_y, kStageScale, arithmetic);
    translate_in_place(matrix, pivot_x, pivot_y, pivot_z, arithmetic);
    return matrix;
}
