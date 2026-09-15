#include "nn_material_state.h"

#include <array>
#include <cstdint>
#include <stdexcept>

namespace sonic4ep2::d3d9 {
namespace {

constexpr std::uint32_t kUnlitDescriptorBit = 0x2u;
constexpr std::uint32_t kAllowedDescriptorFlags = 0x1f07u;
constexpr std::uint32_t kAllowedLogicFlags = 0x1bu;
constexpr std::uint32_t kLitDescriptorStageMask = 0x2u;

struct StateDiagnostics {
    const char* logic_error;
    const char* blend_factor_error;
    const char* blend_operation_error;
    const char* comparison_error;
};

constexpr StateDiagnostics kUnlitStateDiagnostics{
    "Unlit material state logic flags are unsupported.",
    "Unlit material state blend factor is unsupported.",
    "Unlit material state blend operation is unsupported.",
    "Unlit material state comparison is unsupported.",
};

constexpr StateDiagnostics kLitStateDiagnostics{
    "Lit material state logic flags are unsupported.",
    "Lit material state blend factor is unsupported.",
    "Lit material state blend operation is unsupported.",
    "Lit material state comparison is unsupported.",
};

void require(bool condition, const char* message) {
    if (!condition) {
        throw std::invalid_argument(message);
    }
}

std::uint16_t low_word(std::uint32_t value) {
    return static_cast<std::uint16_t>(value & 0xffffu);
}

std::uint16_t high_word(std::uint32_t value) {
    return static_cast<std::uint16_t>(value >> 16u);
}

BlendFactor blend_factor(std::uint16_t value, const StateDiagnostics& diagnostics) {
    switch (value) {
    case 1u:
        return BlendFactor::Zero;
    case 2u:
        return BlendFactor::One;
    case 3u:
        return BlendFactor::SourceColor;
    case 4u:
        return BlendFactor::InverseSourceColor;
    case 5u:
        return BlendFactor::SourceAlpha;
    case 6u:
        return BlendFactor::InverseSourceAlpha;
    case 7u:
        return BlendFactor::DestinationAlpha;
    case 8u:
        return BlendFactor::InverseDestinationAlpha;
    case 9u:
        return BlendFactor::DestinationColor;
    case 10u:
        return BlendFactor::InverseDestinationColor;
    case 11u:
        return BlendFactor::SourceAlphaSaturate;
    case 14u:
        return BlendFactor::BlendFactor;
    case 15u:
        return BlendFactor::InverseBlendFactor;
    default:
        throw std::invalid_argument(diagnostics.blend_factor_error);
    }
}

BlendOperation blend_operation(std::uint16_t value, const StateDiagnostics& diagnostics) {
    switch (value) {
    case 1u:
        return BlendOperation::Add;
    case 2u:
        return BlendOperation::Subtract;
    case 3u:
        return BlendOperation::ReverseSubtract;
    case 4u:
        return BlendOperation::Min;
    case 5u:
        return BlendOperation::Max;
    default:
        throw std::invalid_argument(diagnostics.blend_operation_error);
    }
}

CompareFunction compare_function(std::uint16_t value, const StateDiagnostics& diagnostics) {
    switch (value) {
    case 1u:
        return CompareFunction::Never;
    case 2u:
        return CompareFunction::Less;
    case 3u:
        return CompareFunction::Equal;
    case 4u:
        return CompareFunction::LessEqual;
    case 5u:
        return CompareFunction::Greater;
    case 6u:
        return CompareFunction::NotEqual;
    case 7u:
        return CompareFunction::GreaterEqual;
    case 8u:
        return CompareFunction::Always;
    default:
        throw std::invalid_argument(diagnostics.comparison_error);
    }
}

std::uint32_t argb_to_rgba(std::uint32_t argb) {
    return ((argb & 0x00ffffffu) << 8u) | (argb >> 24u);
}

NnUnlitMaterialState translate_material_state(
    std::uint32_t descriptor_flags,
    const NnMaterialStateData& material_state,
    const RenderStateDescription& prior,
    bool fog_requested,
    const StateDiagnostics& diagnostics) {
    const std::array<std::uint32_t, 7u>& logic = material_state.words;
    const std::uint32_t logic_flags = logic[0u];
    require((logic_flags & ~kAllowedLogicFlags) == 0u, diagnostics.logic_error);

    const bool blend_enabled = (logic_flags & 0x1u) != 0u;
    const bool separate_alpha = blend_enabled && (logic_flags & 0x2u) != 0u;
    const bool alpha_test_enabled = (logic_flags & 0x8u) != 0u;
    const bool depth_enabled = (logic_flags & 0x10u) != 0u;

    BlendFactor source_color = BlendFactor::One;
    BlendFactor destination_color = BlendFactor::Zero;
    BlendOperation color_operation = BlendOperation::Add;
    if (blend_enabled) {
        source_color = blend_factor(low_word(logic[1u]), diagnostics);
        destination_color = blend_factor(high_word(logic[1u]), diagnostics);
        color_operation = blend_operation(low_word(logic[4u]), diagnostics);
    }

    BlendFactor source_alpha = BlendFactor::One;
    BlendFactor destination_alpha = BlendFactor::Zero;
    BlendOperation alpha_operation = BlendOperation::Add;
    if (separate_alpha) {
        source_alpha = blend_factor(low_word(logic[2u]), diagnostics);
        destination_alpha = blend_factor(high_word(logic[2u]), diagnostics);
        alpha_operation = color_operation;
    }

    CompareFunction alpha_compare = prior.alpha_test.compare;
    std::uint8_t alpha_reference = prior.alpha_test.reference;
    if (alpha_test_enabled) {
        alpha_compare = compare_function(low_word(logic[5u]), diagnostics);
        alpha_reference = static_cast<std::uint8_t>(logic[6u] & 0xffu);
    }

    CompareFunction depth_compare = prior.depth_stencil.depth_compare;
    if (depth_enabled) {
        depth_compare = compare_function(high_word(logic[5u]), diagnostics);
    }

    NnUnlitMaterialState result{prior, fog_requested && (descriptor_flags & 0x4u) == 0u};
    RenderStateDescription& render_state = result.render_state;
    render_state.rasterizer.fill_mode = FillMode::Solid;
    render_state.rasterizer.cull_mode =
        (descriptor_flags & 0x1u) != 0u ? CullMode::None : CullMode::Clockwise;
    render_state.depth_stencil.depth_write = (descriptor_flags & 0x100u) == 0u;
    render_state.color_write_mask = static_cast<std::uint8_t>((~(descriptor_flags >> 9u)) & 0x0fu);

    render_state.blend.enabled = blend_enabled;
    render_state.blend.source_color = source_color;
    render_state.blend.destination_color = destination_color;
    render_state.blend.color_operation = color_operation;
    render_state.blend.separate_alpha = separate_alpha;
    render_state.blend.source_alpha = source_alpha;
    render_state.blend.destination_alpha = destination_alpha;
    render_state.blend.alpha_operation = alpha_operation;
    render_state.blend.blend_factor_rgba = argb_to_rgba(logic[3u]);

    render_state.alpha_test.enabled = alpha_test_enabled;
    if (alpha_test_enabled) {
        render_state.alpha_test.compare = alpha_compare;
        render_state.alpha_test.reference = alpha_reference;
    }

    render_state.depth_stencil.depth_enable = depth_enabled;
    if (depth_enabled) {
        render_state.depth_stencil.depth_compare = depth_compare;
    }

    return result;
}

}

NnUnlitMaterialState nn_unlit_material_state(
    const NnMaterialData& material,
    const NnMaterialProfileContext& context,
    const RenderStateDescription& prior,
    bool fog_requested) {
    const NnShaderProfileKeyInput profile = nn_unlit_material_profile(material, context);
    require(context.draw_flags_low == 0u, "Unlit material state low draw flags are unsupported.");
    require(context.draw_flags_high == 0u, "Unlit material state high draw flags are unsupported.");
    require(profile.transform_mode == 0u, "Unlit material state transformed profile is unsupported.");
    require(material.stages.size() == 1u && profile.base_map, "Unlit material state requires an active base map.");

    const std::uint32_t descriptor_flags = material.descriptor_bits[0u];
    require(
        (descriptor_flags & kUnlitDescriptorBit) != 0u,
        "Unlit material state descriptor does not enable the unlit branch.");
    require(
        (descriptor_flags & ~kAllowedDescriptorFlags) == 0u,
        "Unlit material state descriptor flags are unsupported.");
    require(material.render_state.has_value(), "Unlit material state requires render-state logic.");

    return translate_material_state(
        descriptor_flags,
        *material.render_state,
        prior,
        fog_requested,
        kUnlitStateDiagnostics);
}

NnLitMaterialState nn_lit_material_state(
    const NnMaterialData& material,
    const NnMaterialProfileContext& context,
    const RenderStateDescription& prior,
    bool fog_requested) {
    const NnShaderProfileKeyInput profile = nn_lit_material_profile(material, context);
    require(material.stages.size() == 1u && profile.base_map, "Lit material state requires an active base map.");
    require(
        material.descriptor_bits[4u] == kLitDescriptorStageMask,
        "Lit material state stage descriptor is unsupported.");
    require(material.render_state.has_value(), "Lit material state requires render-state logic.");

    const NnUnlitMaterialState translated = translate_material_state(
        material.descriptor_bits[0u],
        *material.render_state,
        prior,
        fog_requested,
        kLitStateDiagnostics);
    return {translated.render_state, translated.fog_enabled};
}

}
