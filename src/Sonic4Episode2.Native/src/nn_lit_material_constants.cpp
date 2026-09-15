#include "nn_lit_material_constants.h"

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <stdexcept>

namespace {

constexpr std::uint32_t kFiniteExponentMask = 0x7f800000u;
constexpr std::size_t kAmbientTerm = 0u;
constexpr std::size_t kDiffuseTerm = 1u;
constexpr std::size_t kSpecularTerm = 2u;
constexpr std::size_t kEmissionTerm = 3u;

void require(bool condition, const char* message) {
    if (!condition) {
        throw std::invalid_argument(message);
    }
}

bool is_finite_float_bits(std::uint32_t bits) {
    return (bits & kFiniteExponentMask) != kFiniteExponentMask;
}

float decode_float(std::uint32_t bits) {
    static_assert(sizeof(float) == sizeof(bits), "lit material constants require 32-bit floats");
    float value = 0.0f;
    std::memcpy(&value, &bits, sizeof(value));
    return value;
}

std::array<float, 4u> decode_color(const std::array<std::uint32_t, 4u>& bits) {
    return {{
        decode_float(bits[0u]),
        decode_float(bits[1u]),
        decode_float(bits[2u]),
        decode_float(bits[3u]),
    }};
}

void require_precision(CameraPrecision precision) {
    require(
        precision == CameraPrecision::Single || precision == CameraPrecision::Double,
        "lit material constants precision is unsupported");
}

void require_finite_inputs(
    const NnMaterialData& material,
    const std::array<float, 4u>& global_ambient) {
    for (const std::array<std::uint32_t, 4u>& color : material.color_terms_bits) {
        for (std::uint32_t bits : color) {
            require(is_finite_float_bits(bits), "lit material constants contain a nonfinite color term");
        }
    }
    require(is_finite_float_bits(material.shininess_bits), "lit material constants shininess is nonfinite");
    require(
        is_finite_float_bits(material.specular_intensity_bits),
        "lit material constants specular intensity is nonfinite");
    require(
        is_finite_float_bits(material.stages[0u].raw_words[3u]),
        "lit material constants base alpha is nonfinite");
    for (float value : global_ambient) {
        require(std::isfinite(value), "lit material constants global ambient is nonfinite");
    }
}

struct Arithmetic {
    CameraPrecision precision;

    double rounded(double value) const {
        require(std::isfinite(value), "lit material constants arithmetic is nonfinite");
        if (precision == CameraPrecision::Double) {
            return value;
        }
        int exponent = 0;
        const float mantissa = static_cast<float>(std::frexp(value, &exponent));
        const double result = std::ldexp(static_cast<double>(mantissa), exponent);
        require(std::isfinite(result), "lit material constants arithmetic is nonfinite");
        return result;
    }

    double add(double left, double right) const {
        return rounded(left + right);
    }

    double multiply(double left, double right) const {
        return rounded(left * right);
    }
};

float spill_finite(double value) {
    require(std::isfinite(value), "lit material constants arithmetic is nonfinite");
    const float result = static_cast<float>(value);
    require(std::isfinite(result), "lit material constants arithmetic is nonfinite");
    return result;
}

}

NnLitMaterialConstants nn_lit_material_constants(
    const NnMaterialData& material,
    const NnMaterialProfileContext& context,
    const std::array<float, 4u>& global_ambient,
    CameraPrecision precision) {
    require_precision(precision);
    const NnShaderProfileKeyInput profile = nn_lit_material_profile(material, context);
    require(material.stages.size() == 1u, "lit material constants require one base stage");
    require(
        context.texture_stage_limit >= 1u && context.texture_stage_limit <= 8u,
        "lit material constants texture-stage limit is unsupported");
    require(profile.base_map, "lit material constants require an active base stage");
    require(material.descriptor_bits[4u] == 0x2u, "lit material constants stage descriptor is unsupported");
    require(
        material.color_flags == 0u || material.color_flags == 0x2u,
        "lit material constants color flags are unsupported");
    require_finite_inputs(material, global_ambient);

    NnLitMaterialConstants result{};
    result.ambient = decode_color(material.color_terms_bits[kAmbientTerm]);
    result.diffuse = decode_color(material.color_terms_bits[kDiffuseTerm]);
    result.specular = decode_color(material.color_terms_bits[kSpecularTerm]);
    result.emission = decode_color(material.color_terms_bits[kEmissionTerm]);
    result.shininess = decode_float(material.shininess_bits);
    result.base_alpha = decode_float(material.stages[0u].raw_words[3u]);

    const Arithmetic arithmetic{precision};
    if (material.color_flags == 0x2u) {
        const float intensity = decode_float(material.specular_intensity_bits);
        for (std::size_t channel = 0u; channel < 3u; ++channel) {
            result.specular[channel] = spill_finite(arithmetic.multiply(result.specular[channel], intensity));
        }
    }

    result.scene_color[0u] = spill_finite(arithmetic.add(
        arithmetic.multiply(result.emission[0u], result.emission[3u]),
        arithmetic.multiply(global_ambient[0u], result.ambient[0u])));
    result.scene_color[1u] = spill_finite(arithmetic.add(
        arithmetic.multiply(global_ambient[1u], result.ambient[1u]),
        arithmetic.multiply(result.emission[1u], result.emission[3u])));
    result.scene_color[2u] = spill_finite(arithmetic.add(
        arithmetic.multiply(result.emission[3u], result.emission[2u]),
        arithmetic.multiply(result.ambient[2u], global_ambient[2u])));
    result.scene_color[3u] = spill_finite(arithmetic.add(
        arithmetic.multiply(result.ambient[3u], global_ambient[3u]),
        0.0));
    return result;
}
