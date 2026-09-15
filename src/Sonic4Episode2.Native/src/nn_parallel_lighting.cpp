#include "nn_parallel_lighting.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <stdexcept>

namespace {

constexpr std::array<std::size_t, 9u> kConsumedMatrixIndices = {{0u, 1u, 2u, 4u, 5u, 6u, 8u, 9u, 10u}};

void require(bool condition, const char* message) {
    if (!condition) {
        throw std::invalid_argument(message);
    }
}

bool is_finite_float_bits(std::uint32_t bits) {
    return (bits & 0x7F800000u) != 0x7F800000u;
}

float decode_float(std::uint32_t bits) {
    static_assert(sizeof(float) == sizeof(bits), "parallel lighting requires 32-bit floats");
    float value = 0.0f;
    std::memcpy(&value, &bits, sizeof(value));
    return value;
}

std::uint32_t encode_float(float value) {
    std::uint32_t bits = 0u;
    std::memcpy(&bits, &value, sizeof(bits));
    return bits;
}

void require_precision(CameraPrecision precision) {
    require(
        precision == CameraPrecision::Single || precision == CameraPrecision::Double,
        "parallel lighting precision is unsupported");
}

void require_matrix(const NnShaderMatrix& matrix) {
    for (std::size_t index : kConsumedMatrixIndices) {
        require(std::isfinite(matrix[index]), "parallel lighting matrix contains a nonfinite value");
    }
}

void require_light(const StageParallelLight& light) {
    for (std::uint32_t bits : light.color_bits) {
        require(is_finite_float_bits(bits), "parallel lighting color contains a nonfinite value");
    }
    require(is_finite_float_bits(light.intensity_bits), "parallel lighting intensity is nonfinite");
    for (std::uint32_t bits : light.direction_bits) {
        require(is_finite_float_bits(bits), "parallel lighting direction contains a nonfinite value");
    }
}

struct Arithmetic {
    CameraPrecision precision;

    double rounded(double value) const {
        require(std::isfinite(value), "parallel lighting arithmetic is nonfinite");
        if (precision == CameraPrecision::Double) {
            return value;
        }
        int exponent = 0;
        const float mantissa = static_cast<float>(std::frexp(value, &exponent));
        const double result = std::ldexp(static_cast<double>(mantissa), exponent);
        require(std::isfinite(result), "parallel lighting arithmetic is nonfinite");
        return result;
    }

    double add(double left, double right) const {
        return rounded(left + right);
    }

    double multiply(double left, double right) const {
        return rounded(left * right);
    }
};

float spill(double value) {
    require(std::isfinite(value), "parallel lighting arithmetic is nonfinite");
    const float result = static_cast<float>(value);
    require(std::isfinite(result), "parallel lighting arithmetic is nonfinite");
    return result;
}


std::array<std::uint32_t, 3u> transformed_direction_bits(
    const StageParallelLight& light,
    const NnShaderMatrix& matrix,
    const Arithmetic& arithmetic) {
    const float x = decode_float(light.direction_bits[0u]);
    const float y = decode_float(light.direction_bits[1u]);
    const float z = decode_float(light.direction_bits[2u]);
    return {{
        encode_float(spill(arithmetic.add(
            arithmetic.add(arithmetic.multiply(matrix[4u], y), arithmetic.multiply(matrix[0u], x)),
            arithmetic.multiply(matrix[8u], z)))),
        encode_float(spill(arithmetic.add(
            arithmetic.add(arithmetic.multiply(matrix[1u], x), arithmetic.multiply(matrix[5u], y)),
            arithmetic.multiply(matrix[9u], z)))),
        encode_float(spill(arithmetic.add(
            arithmetic.add(arithmetic.multiply(matrix[2u], x), arithmetic.multiply(matrix[6u], y)),
            arithmetic.multiply(matrix[10u], z)))),
    }};
}

float sign_flipped_float(std::uint32_t bits) {
    return decode_float(bits ^ 0x80000000u);
}

}

NnParallelLightingConstants nn_parallel_lighting_constants(
    const std::array<StageParallelLight, 8u>& lights,
    std::uint32_t enable_mask,
    const NnShaderMatrix& light_matrix,
    CameraPrecision precision) {
    require_precision(precision);
    require(enable_mask <= 0xFFu, "parallel lighting enable mask is unsupported");
    require_matrix(light_matrix);

    const Arithmetic arithmetic{precision};
    StageLighting transformed_lighting{};
    std::array<std::size_t, 4u> selected_sources{};
    std::size_t selected_count = 0u;
    for (std::size_t source_index = 0u; source_index < lights.size(); ++source_index) {
        if ((enable_mask & (std::uint32_t{1u} << source_index)) == 0u) {
            continue;
        }
        if (selected_count == selected_sources.size()) {
            break;
        }
        const StageParallelLight& source = lights[source_index];
        require_light(source);
        transformed_lighting.lights[selected_count].direction_bits =
            transformed_direction_bits(source, light_matrix, arithmetic);
        selected_sources[selected_count] = source_index;
        ++selected_count;
    }

    const StageLighting normalized_lighting = normalize_stage_lighting(transformed_lighting, precision);
    NnParallelLightingConstants result{};
    result.count = static_cast<std::uint32_t>(selected_count);
    for (std::size_t result_index = 0u; result_index < selected_count; ++result_index) {
        const StageParallelLight& source = lights[selected_sources[result_index]];
        const float intensity = decode_float(source.intensity_bits);
        NnParallelLightConstants& destination = result.lights[result_index];
        if (intensity == 1.0f) {
            for (std::size_t channel = 0u; channel < 3u; ++channel) {
                destination.diffuse[channel] = decode_float(source.color_bits[channel]);
            }
        } else {
            for (std::size_t channel = 0u; channel < 3u; ++channel) {
                destination.diffuse[channel] = spill(
                    arithmetic.multiply(decode_float(source.color_bits[channel]), intensity));
            }
        }
        destination.diffuse[3u] = decode_float(source.color_bits[3u]);
        destination.specular = destination.diffuse;
        destination.position[0u] = sign_flipped_float(normalized_lighting.lights[result_index].direction_bits[0u]);
        destination.position[1u] = sign_flipped_float(normalized_lighting.lights[result_index].direction_bits[1u]);
        destination.position[2u] = sign_flipped_float(normalized_lighting.lights[result_index].direction_bits[2u]);
        destination.position[3u] = 0.0f;
    }
    return result;
}

NnParallelLightingConstants nn_parallel_lighting_constants(
    const std::array<StageParallelLight, 8u>& lights,
    std::uint32_t enable_mask,
    const NnShaderMatrix& light_matrix,
    CameraPrecision precision,
    const std::array<float, 4u>& light_tint) {
    require_precision(precision);
    require(enable_mask <= 0xFFu, "parallel lighting enable mask is unsupported");
    const Arithmetic arithmetic{precision};
    auto adjusted = lights;
    for (std::size_t index = 0u; index < adjusted.size(); ++index) {
        if ((enable_mask & (std::uint32_t{1u} << index)) == 0u) continue;
        for (std::size_t channel = 0u; channel < 3u; ++channel) {
            require(std::isfinite(light_tint[channel]), "parallel lighting tint is nonfinite");
            require(is_finite_float_bits(lights[index].color_bits[channel]),
                    "parallel lighting color contains a nonfinite value");
            adjusted[index].color_bits[channel] = encode_float(spill(std::min(1.0,
                arithmetic.multiply(decode_float(lights[index].color_bits[channel]), light_tint[channel]))));
        }
    }
    return nn_parallel_lighting_constants(adjusted, enable_mask, light_matrix, precision);
}
