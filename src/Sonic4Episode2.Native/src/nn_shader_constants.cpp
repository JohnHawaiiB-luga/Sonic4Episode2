#include "nn_shader_constants.h"

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <stdexcept>

namespace {

constexpr std::uint32_t kUnlitStageFlags = 0x60000002u;
constexpr std::uint32_t kFiniteExponentMask = 0x7f800000u;
constexpr std::uint32_t kSignMask = 0x80000000u;
constexpr std::size_t kUnlitDiffuseTermIndex = 1u;
constexpr std::size_t kUnlitColorTermCount = 4u;
constexpr std::size_t kUnlitBaseAlphaWord = 3u;
constexpr std::uint32_t kUnlitMaterialStageMask = 0x2u;

void require(bool condition, const char* message) {
    if (!condition) {
        throw std::invalid_argument(message);
    }
}

std::uint32_t float_bits(const float& value) {
    static_assert(sizeof(float) == sizeof(std::uint32_t));
    std::uint32_t bits = 0u;
    std::memcpy(&bits, &value, sizeof(bits));
    return bits;
}

std::int32_t decode_signed_coordinate(std::uint32_t bits) {
    static_assert(sizeof(std::int32_t) == sizeof(bits));
    std::int32_t coordinate = 0;
    std::memcpy(&coordinate, &bits, sizeof(coordinate));
    return coordinate;
}

bool is_finite_float_bits(std::uint32_t bits) {
    return (bits & kFiniteExponentMask) != kFiniteExponentMask;
}

void validate_model_precision(CameraPrecision precision) {
    require(
        precision == CameraPrecision::Single || precision == CameraPrecision::Double,
        "Unlit model matrices precision is unsupported.");
}

void validate_finite_matrix(const NnShaderMatrix& matrix, const char* message) {
    for (const float& value : matrix) {
        require(is_finite_float_bits(float_bits(value)), message);
    }
}

void validate_context(const NnMaterialProfileContext& context) {
    require(
        context.texture_stage_limit >= 1u && context.texture_stage_limit <= 8u,
        "Unlit texture matrix texture-stage limit is out of range.");
    require(context.draw_flags_high == 0u, "Unlit texture matrix high draw flags are unsupported.");
    require(
        context.draw_flags_low == 0u || context.draw_flags_low == 0x400u || context.draw_flags_low == 0x800u,
        "Unlit texture matrix low draw flags are unsupported.");
}

void validate_stage(const NnMaterialStageData& stage) {
    require(stage.raw_words.size() == 16u, "Unlit texture matrix stage metadata size is unsupported.");
    require(stage.raw_words[0u] == stage.flags, "Unlit texture matrix stage flags do not match metadata.");
    require(stage.texture_index >= 0, "Unlit texture matrix texture index is negative.");
    require(
        stage.raw_words[1u] == static_cast<std::uint32_t>(stage.texture_index),
        "Unlit texture matrix texture index does not match metadata.");
    require(stage.flags == kUnlitStageFlags, "Unlit texture matrix stage flags are unsupported.");
    const std::int32_t coordinate = decode_signed_coordinate(stage.raw_words[2u]);
    require(
        coordinate == -3 || (coordinate >= 0 && coordinate <= 3),
        "Unlit texture matrix coordinate is unsupported.");
    require(is_finite_float_bits(stage.raw_words[6u]), "Unlit texture matrix U scale is nonfinite.");
    require(is_finite_float_bits(stage.raw_words[7u]), "Unlit texture matrix V scale is nonfinite.");
}

void set_float_bits(NnShaderMatrix& matrix, std::size_t index, std::uint32_t bits) {
    static_assert(sizeof(float) == sizeof(bits));
    std::memcpy(&matrix[index], &bits, sizeof(bits));
}

}

NnUnlitMaterialConstants nn_unlit_material_constants(
    const NnMaterialData& material,
    const NnMaterialProfileContext& context) {
    const NnShaderProfileKeyInput profile = nn_unlit_material_profile(material, context);
    require(context.draw_flags_low == 0u, "Unlit material constants low draw flags are unsupported.");
    require(context.draw_flags_high == 0u, "Unlit material constants high draw flags are unsupported.");
    require(material.stages.size() == 1u, "Unlit material constants require one material stage.");
    require(profile.base_map, "Unlit material constants require an active base map.");
    require(profile.transform_mode == 0u, "Unlit material constants transformed profile is unsupported.");
    require(
        material.descriptor_bits[4u] == kUnlitMaterialStageMask,
        "Unlit material constants stage mask is unsupported.");
    require(
        material.color_terms_bits.size() == kUnlitColorTermCount,
        "Unlit material constants color term count is unsupported.");

    const std::array<std::uint32_t, 4u>& diffuse_bits = material.color_terms_bits[kUnlitDiffuseTermIndex];
    for (std::uint32_t bits : diffuse_bits) {
        require(is_finite_float_bits(bits), "Unlit material constants diffuse contains a nonfinite value.");
    }
    const std::uint32_t base_alpha_bits = material.stages[0u].raw_words[kUnlitBaseAlphaWord];
    require(is_finite_float_bits(base_alpha_bits), "Unlit material constants base alpha is nonfinite.");

    NnUnlitMaterialConstants result{};
    static_assert(sizeof(float) == sizeof(std::uint32_t));
    std::memcpy(result.diffuse.data(), diffuse_bits.data(), sizeof(diffuse_bits));
    std::memcpy(&result.base_alpha, &base_alpha_bits, sizeof(base_alpha_bits));
    return result;
}

NnShaderMatrix nn_unlit_texture_matrix(
    const NnMaterialStageData& stage,
    const NnMaterialProfileContext& context) {
    validate_context(context);
    validate_stage(stage);

    const std::uint32_t scale_u_bits = stage.raw_words[6u];
    const std::uint32_t scale_v_bits = stage.raw_words[7u];
    NnShaderMatrix matrix{};
    for (std::size_t index = 0u; index < matrix.size(); ++index) {
        set_float_bits(matrix, index, 0u);
    }
    set_float_bits(matrix, 0u, scale_u_bits);
    set_float_bits(matrix, 1u, scale_u_bits & kSignMask);
    set_float_bits(matrix, 2u, scale_u_bits & kSignMask);
    set_float_bits(matrix, 4u, scale_v_bits & kSignMask);
    set_float_bits(matrix, 5u, scale_v_bits);
    set_float_bits(matrix, 6u, scale_v_bits & kSignMask);
    set_float_bits(matrix, 15u, 0x3f800000u);
    return matrix;
}

NnShaderMatrix nn_shader_matrix_columns(const NnShaderMatrix& matrix) {
    NnShaderMatrix columns{};
    for (std::size_t row = 0u; row < 4u; ++row) {
        for (std::size_t column = 0u; column < 4u; ++column) {
            set_float_bits(columns, column * 4u + row, float_bits(matrix[row * 4u + column]));
        }
    }
    return columns;
}

NnModelMatrixConstants nn_unlit_model_matrices(
    const NnShaderMatrix& model,
    const NnShaderMatrix& view,
    const NnShaderMatrix& projection,
    CameraPrecision precision,
    const CameraMatrixBackend& backend) {
    validate_model_precision(precision);
    validate_finite_matrix(model, "Unlit model matrix contains a nonfinite value.");
    validate_finite_matrix(view, "Unlit view matrix contains a nonfinite value.");
    validate_finite_matrix(projection, "Unlit projection matrix contains a nonfinite value.");

    const NnShaderMatrix model_view = backend.multiply(model, view, precision);
    return {model_view, backend.multiply(model_view, projection, precision)};
}
