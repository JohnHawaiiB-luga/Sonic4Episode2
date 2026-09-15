#include "nn_material_profile.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <stdexcept>

namespace {

constexpr std::uint32_t kUnlitMaterialPointerFlags = 0x10000000u;
constexpr std::uint32_t kUnlitDescriptorBit = 0x2u;
constexpr std::uint32_t kUnlitStageFlags = 0x60000002u;
constexpr std::uint32_t kLitMaterialPointerFlags = 0x30000000u;
constexpr std::uint32_t kLitStandardMaterialPointerFlags = 0x10000000u;
constexpr std::uint32_t kLitStageFlags = 0x60000002u;
constexpr std::uint32_t kLitAllowedDrawFlagsLow = 0x30000u;
constexpr std::uint32_t kFiniteExponentMask = 0x7f800000u;
constexpr std::uint32_t kFloatMagnitudeMask = 0x7fffffffu;
constexpr std::size_t kLitColorTermCount = 4u;
constexpr std::size_t kLitSpecularTermIndex = 2u;
constexpr std::uint32_t kStaticSubobjectFlag = 0x100u;
constexpr std::uint32_t kTransformVertexFormatMask = 0xf000u;
constexpr std::uint32_t kTransformVertexFormatValue = 0x7000u;

void require(bool condition, const char* message) {
    if (!condition) {
        throw std::invalid_argument(message);
    }
}

std::int32_t decode_signed_coordinate(std::uint32_t bits) {
    static_assert(sizeof(std::int32_t) == sizeof(bits));
    std::int32_t coordinate = 0;
    std::memcpy(&coordinate, &bits, sizeof(coordinate));
    return coordinate;
}

void validate_context(const NnMaterialProfileContext& context) {
    require(context.texture_stage_limit <= 8u, "Unlit material texture-stage limit is out of range.");
    require(context.draw_flags_high == 0u, "Unlit material high draw flags are unsupported.");
    require(
        context.draw_flags_low == 0u || context.draw_flags_low == 0x400u || context.draw_flags_low == 0x800u,
        "Unlit material low draw flags are unsupported.");
}

std::int32_t validate_stage(const NnMaterialStageData& stage) {
    require(stage.raw_words.size() == 16u, "Unlit material stage metadata size is unsupported.");
    require(stage.raw_words[0u] == stage.flags, "Unlit material stage flags do not match metadata.");
    require(stage.texture_index >= 0, "Unlit material texture index is negative.");
    require(
        stage.raw_words[1u] == static_cast<std::uint32_t>(stage.texture_index),
        "Unlit material texture index does not match metadata.");
    require(stage.flags == kUnlitStageFlags, "Unlit material stage flags are unsupported.");
    const std::int32_t coordinate = decode_signed_coordinate(stage.raw_words[2u]);
    require(
        coordinate >= -3 && coordinate <= 3,
        "Unlit material texture coordinate is out of range.");
    return coordinate;
}

void validate_material(const NnMaterialData& material) {
    require(
        material.pointer_flags == kUnlitMaterialPointerFlags,
        "Unlit material pointer flags are unsupported.");
    require(
        (material.descriptor_bits[0u] & kUnlitDescriptorBit) != 0u,
        "Unlit material descriptor does not enable the unlit branch.");
    require(
        static_cast<std::size_t>(material.descriptor_bits[5u]) == material.stages.size(),
        "Unlit material stage count does not match metadata.");
    require(material.stages.size() <= 1u, "Unlit material stage count is unsupported.");
}

bool is_finite_float_bits(std::uint32_t bits) {
    return (bits & kFiniteExponentMask) != kFiniteExponentMask;
}

bool is_zero_float_bits(std::uint32_t bits) {
    return (bits & kFloatMagnitudeMask) == 0u;
}

void validate_lit_context(const NnMaterialProfileContext& context) {
    require(context.texture_stage_limit <= 8u, "Lit material texture-stage limit is out of range.");
    require(
        context.draw_flags_high == 0u || context.draw_flags_high == 2u || context.draw_flags_high == 4u ||
            context.draw_flags_high == 8u,
        "Lit material high draw flags are unsupported.");
    require(
        (context.draw_flags_low & ~kLitAllowedDrawFlagsLow) == 0u,
        "Lit material low draw flags are unsupported.");
}

std::int32_t validate_lit_stage(const NnMaterialStageData& stage) {
    require(stage.raw_words.size() == 16u, "Lit material stage metadata size is unsupported.");
    require(stage.flags == kLitStageFlags, "Lit material stage flags are unsupported.");
    require(stage.raw_words[0u] == kLitStageFlags, "Lit material stage metadata flags are unsupported.");
    require(stage.texture_index >= 0, "Lit material texture index is negative.");
    require(
        stage.raw_words[1u] == static_cast<std::uint32_t>(stage.texture_index),
        "Lit material texture index does not match metadata.");
    const std::int32_t coordinate = decode_signed_coordinate(stage.raw_words[2u]);
    require(
        coordinate >= -3 && coordinate <= 3,
        "Lit material texture coordinate is out of range.");
    return coordinate;
}

void validate_lit_material(const NnMaterialData& material) {
    const bool extended = material.pointer_flags == kLitMaterialPointerFlags;
    require(
        extended || material.pointer_flags == kLitStandardMaterialPointerFlags,
        "Lit material pointer flags are unsupported.");
    require(material.descriptor_bits[0u] == 0u, "Lit material descriptor flags are unsupported.");
    require(
        static_cast<std::size_t>(material.descriptor_bits[5u]) == material.stages.size(),
        "Lit material stage count does not match metadata.");
    require(material.stages.size() <= 1u, "Lit material stage count is unsupported.");
    require(material.color_terms_bits.size() == kLitColorTermCount, "Lit material color-term count is unsupported.");
    if (extended) {
        require(material.user_profile.has_value(), "Lit material user profile is missing.");
        require(*material.user_profile <= 63u, "Lit material user profile is out of range.");
    } else {
        require(!material.user_profile.has_value(), "Standard lit material has an unexpected user profile.");
    }
    require(is_finite_float_bits(material.specular_intensity_bits), "Lit material specular intensity is nonfinite.");

    const std::array<std::uint32_t, 4u>& specular_bits = material.color_terms_bits[kLitSpecularTermIndex];
    for (std::size_t index = 0u; index < 3u; ++index) {
        require(is_finite_float_bits(specular_bits[index]), "Lit material specular RGB contains a nonfinite value.");
    }
}

std::uint32_t vertex_features(std::uint32_t vertex_format) {
    std::uint32_t features = 0u;
    if ((vertex_format & 0x18u) != 0u) {
        features |= 0x1u;
    }
    if ((vertex_format & 0x10u) != 0u) {
        features |= 0x2u;
    }
    if ((vertex_format & 0x6u) != 0u) {
        features |= 0x4u;
    }
    if ((vertex_format & 0x40000000u) != 0u) {
        features |= 0x8u;
    }
    if ((vertex_format & 0x10000000u) != 0u) {
        features |= 0x10u;
    }
    return features;
}

std::uint32_t transform_mode(const NnMaterialProfileContext& context) {
    if ((context.subobject_flags & kStaticSubobjectFlag) != 0u ||
        (context.vertex_format & kTransformVertexFormatMask) != kTransformVertexFormatValue) {
        return 0u;
    }
    return (context.vertex_format & 0x400u) != 0u ? 2u : 1u;
}

}

NnShaderProfileKeyInput nn_unlit_material_profile(
    const NnMaterialData& material,
    const NnMaterialProfileContext& context) {
    validate_context(context);
    validate_material(material);

    std::int32_t coordinate = 0;
    if (!material.stages.empty()) {
        coordinate = validate_stage(material.stages[0u]);
    }

    NnShaderProfileKeyInput result{};
    result.vertex_features = vertex_features(context.vertex_format);
    result.transform_mode = transform_mode(context);
    if (!material.stages.empty() && context.texture_stage_limit > 0u &&
        (context.draw_flags_low & 0x800u) == 0u) {
        result.base_map = true;
        result.texture_coordinates[0u] = coordinate;
    }
    return result;
}

NnShaderProfileKeyInput nn_lit_material_profile(
    const NnMaterialData& material,
    const NnMaterialProfileContext& context) {
    validate_lit_context(context);
    validate_lit_material(material);

    std::int32_t coordinate = 0;
    if (!material.stages.empty()) {
        coordinate = validate_lit_stage(material.stages[0u]);
    }

    NnShaderProfileKeyInput result{};
    result.vertex_features = vertex_features(context.vertex_format);
    result.transform_mode = transform_mode(context);
    result.lighting_features = 1u;
    if (!is_zero_float_bits(material.specular_intensity_bits)) {
        for (std::size_t channel = 0u; channel < 3u; ++channel) {
            if (!is_zero_float_bits(material.color_terms_bits[kLitSpecularTermIndex][channel])) {
                result.lighting_features |= 2u;
            }
        }
    }
    result.parallel_lights = (context.draw_flags_low >> 16u) & 0x3u;
    result.drawobject_profile = (context.draw_flags_high >> 2u) & 0xffu;
    result.user_profile = material.user_profile.value_or(0u);
    if (!material.stages.empty() && context.texture_stage_limit > 0u) {
        result.base_map = true;
        result.texture_coordinates[0u] = coordinate;
    }
    return result;
}
