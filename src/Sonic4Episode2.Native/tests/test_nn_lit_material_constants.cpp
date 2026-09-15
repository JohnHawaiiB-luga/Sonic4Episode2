#include "nn_lit_material_constants.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <exception>
#include <stdexcept>
#include <vector>

namespace {

constexpr std::uint32_t kLitPointerFlags = 0x30000000u;
constexpr std::uint32_t kStageFlags = 0x60000002u;
constexpr std::uint32_t kQuietNan = 0x7fc00000u;
constexpr std::size_t kAmbientTerm = 0u;
constexpr std::size_t kSpecularTerm = 2u;
constexpr std::size_t kEmissionTerm = 3u;

using ColorBits = std::array<std::uint32_t, 4u>;
using ConstantBits = std::array<std::uint32_t, 22u>;

bool check(bool condition, const char* message) {
    if (condition) {
        return true;
    }
    std::fprintf(stderr, "%s\n", message);
    return false;
}

float float_from_bits(std::uint32_t bits) {
    static_assert(sizeof(float) == sizeof(bits));
    float value = 0.0f;
    std::memcpy(&value, &bits, sizeof(value));
    return value;
}

std::uint32_t float_bits(float value) {
    std::uint32_t bits = 0u;
    std::memcpy(&bits, &value, sizeof(bits));
    return bits;
}

bool same_stage(const NnMaterialStageData& left, const NnMaterialStageData& right) {
    return left.flags == right.flags && left.texture_index == right.texture_index && left.raw_words == right.raw_words;
}

bool same_material(const NnMaterialData& left, const NnMaterialData& right) {
    if (left.pointer_flags != right.pointer_flags || left.descriptor_bits != right.descriptor_bits ||
        left.color_terms_bits != right.color_terms_bits || left.color_flags != right.color_flags ||
        left.shininess_bits != right.shininess_bits ||
        left.specular_intensity_bits != right.specular_intensity_bits || left.user_profile != right.user_profile ||
        left.stages.size() != right.stages.size() ||
        left.render_state.has_value() != right.render_state.has_value()) {
        return false;
    }
    if (left.render_state.has_value() && left.render_state->words != right.render_state->words) {
        return false;
    }
    for (std::size_t index = 0u; index < left.stages.size(); ++index) {
        if (!same_stage(left.stages[index], right.stages[index])) {
            return false;
        }
    }
    return true;
}

bool same_context(const NnMaterialProfileContext& left, const NnMaterialProfileContext& right) {
    return left.subobject_flags == right.subobject_flags &&
           left.vertex_format == right.vertex_format &&
           left.draw_flags_low == right.draw_flags_low &&
           left.draw_flags_high == right.draw_flags_high &&
           left.texture_stage_limit == right.texture_stage_limit;
}

bool same_color_bits(const std::array<float, 4u>& left, const std::array<float, 4u>& right) {
    for (std::size_t index = 0u; index < left.size(); ++index) {
        if (float_bits(left[index]) != float_bits(right[index])) {
            return false;
        }
    }
    return true;
}

NnMaterialStageData make_stage(std::uint32_t base_alpha_bits = 0x3f800000u) {
    NnMaterialStageData stage{};
    stage.flags = kStageFlags;
    stage.texture_index = 7;
    stage.raw_words.resize(16u);
    stage.raw_words[0u] = kStageFlags;
    stage.raw_words[1u] = 7u;
    stage.raw_words[2u] = 0u;
    stage.raw_words[3u] = base_alpha_bits;
    return stage;
}

NnMaterialData make_material(std::uint32_t base_alpha_bits = 0x3f800000u) {
    NnMaterialData material{};
    material.pointer_flags = kLitPointerFlags;
    material.descriptor_bits[4u] = 0x2u;
    material.descriptor_bits[5u] = 1u;
    material.color_terms_bits = std::vector<ColorBits>{
        ColorBits{{0x3e828f5cu, 0x3efced91u, 0x3f0a7efau, 0x3f800000u}},
        ColorBits{{0x3f800000u, 0x3f800000u, 0x3f800000u, 0x3f800000u}},
        ColorBits{{0x00000000u, 0x00000000u, 0x00000000u, 0x3f800000u}},
        ColorBits{{0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u}},
    };
    material.color_flags = 0u;
    material.shininess_bits = 0x41000000u;
    material.specular_intensity_bits = 0x3f800000u;
    material.user_profile = 5u;
    material.stages.push_back(make_stage(base_alpha_bits));
    return material;
}

NnMaterialProfileContext make_context() {
    NnMaterialProfileContext context{};
    context.texture_stage_limit = 1u;
    return context;
}

std::array<float, 4u> color_from_bits(const ColorBits& bits) {
    return {{
        float_from_bits(bits[0u]),
        float_from_bits(bits[1u]),
        float_from_bits(bits[2u]),
        float_from_bits(bits[3u]),
    }};
}

ConstantBits constant_bits(const NnLitMaterialConstants& constants) {
    return {{
        float_bits(constants.ambient[0u]),
        float_bits(constants.ambient[1u]),
        float_bits(constants.ambient[2u]),
        float_bits(constants.ambient[3u]),
        float_bits(constants.diffuse[0u]),
        float_bits(constants.diffuse[1u]),
        float_bits(constants.diffuse[2u]),
        float_bits(constants.diffuse[3u]),
        float_bits(constants.specular[0u]),
        float_bits(constants.specular[1u]),
        float_bits(constants.specular[2u]),
        float_bits(constants.specular[3u]),
        float_bits(constants.emission[0u]),
        float_bits(constants.emission[1u]),
        float_bits(constants.emission[2u]),
        float_bits(constants.emission[3u]),
        float_bits(constants.shininess),
        float_bits(constants.scene_color[0u]),
        float_bits(constants.scene_color[1u]),
        float_bits(constants.scene_color[2u]),
        float_bits(constants.scene_color[3u]),
        float_bits(constants.base_alpha),
    }};
}

bool test_maps_actual_model164_fixture() {
    NnMaterialData material = make_material();
    material.color_flags = 0x2u;
    NnMaterialProfileContext context = make_context();
    const std::array<float, 4u> global_ambient = color_from_bits(
        ColorBits{{0x3efffffdu, 0x3f199998u, 0x3f4cccccu, 0x00000000u}});
    const NnMaterialData original_material = material;
    const NnMaterialProfileContext original_context = context;
    const std::array<float, 4u> original_global_ambient = global_ambient;
    const ConstantBits expected = {{
        0x3e828f5cu, 0x3efced91u, 0x3f0a7efau, 0x3f800000u,
        0x3f800000u, 0x3f800000u, 0x3f800000u, 0x3f800000u,
        0x00000000u, 0x00000000u, 0x00000000u, 0x3f800000u,
        0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u,
        0x41000000u,
        0x3e028f5au, 0x3e97c1bcu, 0x3edd97f6u, 0x00000000u,
        0x3f800000u,
    }};

    const NnLitMaterialConstants single = nn_lit_material_constants(
        material,
        context,
        global_ambient,
        CameraPrecision::Single);
    const NnLitMaterialConstants double_precision = nn_lit_material_constants(
        material,
        context,
        global_ambient,
        CameraPrecision::Double);
    return check(constant_bits(single) == expected, "model 164 material constants mismatch at 24-bit precision") &&
           check(constant_bits(double_precision) == expected, "model 164 material constants mismatch at 53-bit precision") &&
           check(same_material(material, original_material), "model 164 material constants modified material input") &&
           check(same_context(context, original_context), "model 164 material constants modified context input") &&
           check(
               same_color_bits(global_ambient, original_global_ambient),
               "model 164 material constants modified global ambient input");
}

bool test_preserves_nonzero_emission_and_precision_rounding() {
    NnMaterialData material = make_material();
    material.color_terms_bits[kEmissionTerm] = {{
        0x3e800000u,
        0xbf000000u,
        0x3f400000u,
        0x3f200000u,
    }};
    NnMaterialProfileContext context = make_context();
    const std::array<float, 4u> global_ambient = color_from_bits(
        ColorBits{{0x3e99999au, 0x3f333333u, 0x3f4ccccdu, 0x3f666666u}});
    const NnMaterialData original_material = material;
    const NnMaterialProfileContext original_context = context;
    const std::array<float, 4u> original_global_ambient = global_ambient;
    const ConstantBits expected_single = {{
        0x3e828f5cu, 0x3efced91u, 0x3f0a7efau, 0x3f800000u,
        0x3f800000u, 0x3f800000u, 0x3f800000u, 0x3f800000u,
        0x00000000u, 0x00000000u, 0x00000000u, 0x3f800000u,
        0x3e800000u, 0xbf000000u, 0x3f400000u, 0x3f200000u,
        0x41000000u,
        0x3e6e5604u, 0x3d086590u, 0x3f66cbfcu, 0x3f666666u,
        0x3f800000u,
    }};
    const ConstantBits expected_double = {{
        0x3e828f5cu, 0x3efced91u, 0x3f0a7efau, 0x3f800000u,
        0x3f800000u, 0x3f800000u, 0x3f800000u, 0x3f800000u,
        0x00000000u, 0x00000000u, 0x00000000u, 0x3f800000u,
        0x3e800000u, 0xbf000000u, 0x3f400000u, 0x3f200000u,
        0x41000000u,
        0x3e6e5604u, 0x3d086591u, 0x3f66cbfbu, 0x3f666666u,
        0x3f800000u,
    }};

    const NnLitMaterialConstants single = nn_lit_material_constants(
        material,
        context,
        global_ambient,
        CameraPrecision::Single);
    const NnLitMaterialConstants double_precision = nn_lit_material_constants(
        material,
        context,
        global_ambient,
        CameraPrecision::Double);
    return check(constant_bits(single) == expected_single, "nonzero emission constants mismatch at 24-bit precision") &&
           check(
               constant_bits(double_precision) == expected_double,
               "nonzero emission constants mismatch at 53-bit precision") &&
           check(
               float_bits(single.scene_color[1u]) != float_bits(double_precision.scene_color[1u]) &&
                   float_bits(single.scene_color[2u]) != float_bits(double_precision.scene_color[2u]),
               "nonzero emission fixture did not retain its recorded precision-sensitive results") &&
           check(same_material(material, original_material), "nonzero emission constants modified material input") &&
           check(same_context(context, original_context), "nonzero emission constants modified context input") &&
           check(
               same_color_bits(global_ambient, original_global_ambient),
               "nonzero emission constants modified global ambient input");
}

bool test_applies_specular_flags_and_signed_zero_rules() {
    NnMaterialData copied = make_material(0x80000000u);
    copied.specular_intensity_bits = 0x80000000u;
    copied.color_terms_bits[kSpecularTerm] = {{
        0x3fc00000u,
        0xc0000000u,
        0x80000000u,
        0xbf000000u,
    }};
    copied.color_terms_bits[kAmbientTerm][3u] = 0x80000000u;
    NnMaterialData scaled = copied;
    scaled.color_flags = 0x2u;
    NnMaterialProfileContext context = make_context();
    const std::array<float, 4u> global_ambient = color_from_bits(
        ColorBits{{0x00000000u, 0x00000000u, 0x00000000u, 0x3f800000u}});
    const NnMaterialData original_copied = copied;
    const NnMaterialData original_scaled = scaled;
    const NnMaterialProfileContext original_context = context;
    const std::array<float, 4u> original_global_ambient = global_ambient;

    const NnLitMaterialConstants copied_constants = nn_lit_material_constants(
        copied,
        context,
        global_ambient,
        CameraPrecision::Single);
    const NnLitMaterialConstants scaled_constants = nn_lit_material_constants(
        scaled,
        context,
        global_ambient,
        CameraPrecision::Double);
    NnMaterialData nonzero = scaled;
    nonzero.specular_intensity_bits = 0x3f000000u;
    const NnLitMaterialConstants nonzero_constants = nn_lit_material_constants(
        nonzero, context, global_ambient, CameraPrecision::Double);
    return check(
               float_bits(nonzero_constants.specular[0u]) == 0x3f400000u &&
                   float_bits(nonzero_constants.specular[1u]) == 0xbf800000u &&
                   float_bits(nonzero_constants.specular[2u]) == 0x80000000u &&
                   float_bits(nonzero_constants.specular[3u]) == 0xbf000000u,
               "nonzero specular intensity did not scale RGB while preserving alpha") &&
           check(
               float_bits(copied_constants.specular[0u]) == 0x3fc00000u &&
                   float_bits(copied_constants.specular[1u]) == 0xc0000000u &&
                   float_bits(copied_constants.specular[2u]) == 0x80000000u &&
                   float_bits(copied_constants.specular[3u]) == 0xbf000000u,
               "color-flags-zero material did not copy specular constants") &&
           check(
               float_bits(scaled_constants.specular[0u]) == 0x80000000u &&
                   float_bits(scaled_constants.specular[1u]) == 0x00000000u &&
                   float_bits(scaled_constants.specular[2u]) == 0x00000000u &&
                   float_bits(scaled_constants.specular[3u]) == 0xbf000000u,
               "color-flags-two material did not multiply specular RGB with signed-zero intensity") &&
           check(
               float_bits(copied_constants.scene_color[3u]) == 0x00000000u &&
                   float_bits(scaled_constants.scene_color[3u]) == 0x00000000u,
               "scene alpha did not add the original positive zero") &&
           check(
               float_bits(copied_constants.base_alpha) == 0x80000000u &&
                   float_bits(scaled_constants.base_alpha) == 0x80000000u,
               "base alpha did not preserve signed zero") &&
           check(same_material(copied, original_copied), "copy-mode material constants modified material input") &&
           check(same_material(scaled, original_scaled), "scale-mode material constants modified material input") &&
           check(same_context(context, original_context), "specular mode material constants modified context input") &&
           check(
               same_color_bits(global_ambient, original_global_ambient),
               "specular mode material constants modified global ambient input");
}

bool test_ignores_unconsumed_stage_words() {
    NnMaterialData material = make_material();
    material.stages[0u].raw_words[4u] = 0x7fc00000u;
    material.stages[0u].raw_words[5u] = 0xff800000u;
    NnMaterialProfileContext context = make_context();
    const std::array<float, 4u> global_ambient = color_from_bits(
        ColorBits{{0x3f800000u, 0x3f800000u, 0x3f800000u, 0x3f800000u}});
    const NnMaterialData original_material = material;
    const NnMaterialProfileContext original_context = context;
    const std::array<float, 4u> original_global_ambient = global_ambient;

    const NnLitMaterialConstants constants = nn_lit_material_constants(
        material,
        context,
        global_ambient,
        CameraPrecision::Single);
    return check(float_bits(constants.base_alpha) == 0x3f800000u, "unconsumed stage-word fixture changed base alpha") &&
           check(same_material(material, original_material), "unconsumed stage words modified material input") &&
           check(same_context(context, original_context), "unconsumed stage words modified context input") &&
           check(
               same_color_bits(global_ambient, original_global_ambient),
               "unconsumed stage words modified global ambient input");
}

bool test_weighted_transform_modes_match_static_suppression() {
    NnMaterialData material = make_material();
    material.color_flags = 0x2u;
    material.color_terms_bits[kEmissionTerm] = {{
        0x3e800000u,
        0xbf000000u,
        0x3f400000u,
        0x3f200000u,
    }};
    NnMaterialProfileContext static_context = make_context();
    static_context.subobject_flags = 0x100u;
    static_context.vertex_format = 0x17003u;
    static_context.draw_flags_low = 0x20000u;
    static_context.draw_flags_high = 8u;
    NnMaterialProfileContext weighted_mode_one_context = static_context;
    weighted_mode_one_context.subobject_flags = 0x201u;
    NnMaterialProfileContext weighted_mode_two_context = weighted_mode_one_context;
    weighted_mode_two_context.vertex_format = 0x17403u;
    const std::array<float, 4u> global_ambient = color_from_bits(
        ColorBits{{0x3e99999au, 0x3f333333u, 0x3f4ccccdu, 0x3f666666u}});
    const NnMaterialData original_material = material;
    const NnMaterialProfileContext original_static_context = static_context;
    const NnMaterialProfileContext original_weighted_mode_one_context = weighted_mode_one_context;
    const NnMaterialProfileContext original_weighted_mode_two_context = weighted_mode_two_context;
    const std::array<float, 4u> original_global_ambient = global_ambient;
    const std::array<CameraPrecision, 2u> precisions = {{CameraPrecision::Single, CameraPrecision::Double}};

    if (!check(
            (weighted_mode_one_context.subobject_flags & 0x100u) == 0u,
            "weighted constants fixture includes the static flag")) {
        return false;
    }
    for (CameraPrecision precision : precisions) {
        const NnLitMaterialConstants static_constants = nn_lit_material_constants(
            material,
            static_context,
            global_ambient,
            precision);
        const NnLitMaterialConstants weighted_mode_one_constants = nn_lit_material_constants(
            material,
            weighted_mode_one_context,
            global_ambient,
            precision);
        const NnLitMaterialConstants weighted_mode_two_constants = nn_lit_material_constants(
            material,
            weighted_mode_two_context,
            global_ambient,
            precision);
        if (!check(
                constant_bits(static_constants) == constant_bits(weighted_mode_one_constants),
                "mode-one weighted constants differ from static suppression") ||
            !check(
                constant_bits(static_constants) == constant_bits(weighted_mode_two_constants),
                "mode-two weighted constants differ from static suppression")) {
            return false;
        }
    }
    return check(same_material(material, original_material), "weighted constants modified material input") &&
           check(same_context(static_context, original_static_context), "weighted constants modified static context") &&
           check(
               same_context(weighted_mode_one_context, original_weighted_mode_one_context),
               "weighted constants modified mode-one context") &&
           check(
               same_context(weighted_mode_two_context, original_weighted_mode_two_context),
               "weighted constants modified mode-two context") &&
           check(
               same_color_bits(global_ambient, original_global_ambient),
               "weighted constants modified global ambient input");
}

template <typename Mutate>
bool rejects_invalid_input(Mutate&& mutate, const char* message) {
    NnMaterialData material = make_material();
    NnMaterialProfileContext context = make_context();
    std::array<float, 4u> global_ambient = color_from_bits(
        ColorBits{{0x3f800000u, 0x3f800000u, 0x3f800000u, 0x3f800000u}});
    CameraPrecision precision = CameraPrecision::Single;
    mutate(material, context, global_ambient, precision);
    const NnMaterialData original_material = material;
    const NnMaterialProfileContext original_context = context;
    const std::array<float, 4u> original_global_ambient = global_ambient;

    try {
        static_cast<void>(nn_lit_material_constants(material, context, global_ambient, precision));
    } catch (const std::invalid_argument&) {
        return check(same_material(material, original_material), "rejected material constants modified material input") &&
               check(same_context(context, original_context), "rejected material constants modified context input") &&
               check(
                   same_color_bits(global_ambient, original_global_ambient),
                   "rejected material constants modified global ambient input");
    } catch (const std::exception&) {
        return check(false, message);
    }
    return check(false, message);
}

bool test_rejects_unsupported_profiles_and_flags() {
    return rejects_invalid_input(
               [](NnMaterialData& material, NnMaterialProfileContext&, std::array<float, 4u>&, CameraPrecision&) {
                   material.stages.clear();
                   material.descriptor_bits[5u] = 0u;
               },
               "material constants accepted a material without a base stage") &&
           rejects_invalid_input(
               [](NnMaterialData& material, NnMaterialProfileContext&, std::array<float, 4u>&, CameraPrecision&) {
                   material.stages.push_back(make_stage());
                   material.descriptor_bits[5u] = 2u;
               },
               "material constants accepted more than one base stage") &&
           rejects_invalid_input(
               [](NnMaterialData&, NnMaterialProfileContext& context, std::array<float, 4u>&, CameraPrecision&) {
                   context.texture_stage_limit = 0u;
               },
               "material constants accepted a disabled base stage") &&
           rejects_invalid_input(
               [](NnMaterialData&, NnMaterialProfileContext& context, std::array<float, 4u>&, CameraPrecision&) {
                   context.texture_stage_limit = 9u;
               },
               "material constants accepted an out-of-range stage limit") &&
           rejects_invalid_input(
               [](NnMaterialData& material, NnMaterialProfileContext&, std::array<float, 4u>&, CameraPrecision&) {
                   material.descriptor_bits[4u] = 0u;
               },
               "material constants accepted a missing stage descriptor mask") &&
           rejects_invalid_input(
               [](NnMaterialData& material, NnMaterialProfileContext&, std::array<float, 4u>&, CameraPrecision&) {
                   material.descriptor_bits[4u] = 0x3u;
               },
               "material constants accepted a nonexact stage descriptor mask") &&
           rejects_invalid_input(
               [](NnMaterialData& material, NnMaterialProfileContext&, std::array<float, 4u>&, CameraPrecision&) {
                   material.color_flags = 1u;
               },
               "material constants accepted unsupported color flags") &&
           rejects_invalid_input(
               [](NnMaterialData& material, NnMaterialProfileContext&, std::array<float, 4u>&, CameraPrecision&) {
                   material.color_flags = 0x80000002u;
               },
               "material constants accepted color flags outside the exact supported values") &&
           rejects_invalid_input(
               [](NnMaterialData&, NnMaterialProfileContext&, std::array<float, 4u>&, CameraPrecision& precision) {
                   precision = static_cast<CameraPrecision>(77);
               },
               "material constants accepted unsupported precision") &&
           rejects_invalid_input(
               [](NnMaterialData& material, NnMaterialProfileContext&, std::array<float, 4u>&, CameraPrecision&) {
                   material.pointer_flags = 0x10000000u;
               },
               "material constants did not retain lit-profile admission") &&
           rejects_invalid_input(
               [](NnMaterialData& material, NnMaterialProfileContext&, std::array<float, 4u>&, CameraPrecision&) {
                   material.descriptor_bits[0u] = 1u;
               },
               "material constants accepted invalid lit descriptor flags") &&
           rejects_invalid_input(
               [](NnMaterialData& material, NnMaterialProfileContext&, std::array<float, 4u>&, CameraPrecision&) {
                   material.descriptor_bits[5u] = 0u;
               },
               "material constants accepted mismatched stage metadata") &&
           rejects_invalid_input(
               [](NnMaterialData& material, NnMaterialProfileContext&, std::array<float, 4u>&, CameraPrecision&) {
                   material.color_terms_bits.pop_back();
               },
               "material constants accepted an incomplete color-term array") &&
           rejects_invalid_input(
               [](NnMaterialData& material, NnMaterialProfileContext&, std::array<float, 4u>&, CameraPrecision&) {
                   material.user_profile.reset();
               },
               "material constants accepted a missing lit user profile") &&
           rejects_invalid_input(
               [](NnMaterialData& material, NnMaterialProfileContext&, std::array<float, 4u>&, CameraPrecision&) {
                   material.user_profile = 64u;
               },
               "material constants accepted an out-of-range lit user profile") &&
           rejects_invalid_input(
               [](NnMaterialData&, NnMaterialProfileContext& context, std::array<float, 4u>&, CameraPrecision&) {
                   context.draw_flags_low = 0x400u;
               },
               "material constants accepted unsupported lit low draw flags") &&
           rejects_invalid_input(
               [](NnMaterialData&, NnMaterialProfileContext& context, std::array<float, 4u>&, CameraPrecision&) {
                   context.draw_flags_high = 1u;
               },
               "material constants accepted unsupported lit high draw flags") &&
           rejects_invalid_input(
               [](NnMaterialData& material, NnMaterialProfileContext&, std::array<float, 4u>&, CameraPrecision&) {
                   material.stages[0u].raw_words.resize(15u);
               },
               "material constants accepted malformed base-stage metadata");
}

bool test_rejects_nonfinite_consumed_values_and_overflow() {
    for (std::size_t term = 0u; term < 4u; ++term) {
        for (std::size_t channel = 0u; channel < 4u; ++channel) {
            if (!rejects_invalid_input(
                    [term, channel](
                        NnMaterialData& material,
                        NnMaterialProfileContext&,
                        std::array<float, 4u>&,
                        CameraPrecision&) { material.color_terms_bits[term][channel] = kQuietNan; },
                    "material constants accepted a nonfinite color term")) {
                return false;
            }
        }
    }
    for (std::size_t channel = 0u; channel < 4u; ++channel) {
        if (!rejects_invalid_input(
                [channel](
                    NnMaterialData&,
                    NnMaterialProfileContext&,
                    std::array<float, 4u>& global_ambient,
                    CameraPrecision&) { global_ambient[channel] = float_from_bits(kQuietNan); },
                "material constants accepted nonfinite global ambient")) {
            return false;
        }
    }
    return rejects_invalid_input(
               [](NnMaterialData& material, NnMaterialProfileContext&, std::array<float, 4u>&, CameraPrecision&) {
                   material.shininess_bits = kQuietNan;
               },
               "material constants accepted nonfinite shininess") &&
           rejects_invalid_input(
               [](NnMaterialData& material, NnMaterialProfileContext&, std::array<float, 4u>&, CameraPrecision&) {
                   material.specular_intensity_bits = kQuietNan;
               },
               "material constants accepted nonfinite specular intensity") &&
           rejects_invalid_input(
               [](NnMaterialData& material, NnMaterialProfileContext&, std::array<float, 4u>&, CameraPrecision&) {
                   material.stages[0u].raw_words[3u] = kQuietNan;
               },
               "material constants accepted nonfinite base alpha") &&
           rejects_invalid_input(
               [](NnMaterialData& material, NnMaterialProfileContext&, std::array<float, 4u>& global_ambient, CameraPrecision&) {
                   material.color_terms_bits[kAmbientTerm][0u] = 0x7f7fffffu;
                   global_ambient[0u] = float_from_bits(0x7f7fffffu);
               },
               "material constants accepted an overflowing scene color");
}

}

int main() {
    return test_maps_actual_model164_fixture() &&
                    test_preserves_nonzero_emission_and_precision_rounding() &&
                    test_applies_specular_flags_and_signed_zero_rules() &&
                    test_ignores_unconsumed_stage_words() &&
                    test_weighted_transform_modes_match_static_suppression() &&
                    test_rejects_unsupported_profiles_and_flags() &&
                    test_rejects_nonfinite_consumed_values_and_overflow()
                ? 0
                : 1;
}
