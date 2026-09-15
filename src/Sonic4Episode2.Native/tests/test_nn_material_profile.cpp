#include "nn_material_profile.h"
#include "nn_shader_name.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <exception>
#include <stdexcept>
#include <utility>
#include <vector>

namespace {

constexpr std::uint32_t kPointerFlags = 0x10000000u;
constexpr std::uint32_t kDescriptorUnlitBit = 0x2u;
constexpr std::uint32_t kStageFlags = 0x60000002u;
constexpr std::uint32_t kLitPointerFlags = 0x30000000u;
constexpr std::size_t kLitSpecularTermIndex = 2u;

bool check(bool condition, const char* message) {
    if (condition) {
        return true;
    }
    std::fprintf(stderr, "%s\n", message);
    return false;
}

bool same_profile(const NnShaderProfileKeyInput& left, const NnShaderProfileKeyInput& right) {
    return left.vertex_features == right.vertex_features &&
           left.transform_mode == right.transform_mode &&
           left.lighting_features == right.lighting_features &&
           left.parallel_lights == right.parallel_lights &&
           left.point_lights == right.point_lights &&
           left.normal_map_type == right.normal_map_type &&
           left.base_map == right.base_map &&
           left.decal_maps == right.decal_maps &&
           left.standard_maps == right.standard_maps &&
           left.user_maps == right.user_maps &&
           left.shadow_maps == right.shadow_maps &&
           left.user_samplers == right.user_samplers &&
           left.texture_coordinates == right.texture_coordinates &&
           left.user_profile == right.user_profile &&
           left.drawobject_profile == right.drawobject_profile;
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

NnMaterialStageData make_stage(std::int32_t texture_index, std::int32_t coordinate) {
    NnMaterialStageData stage{};
    stage.flags = kStageFlags;
    stage.texture_index = texture_index;
    stage.raw_words.resize(16u);
    stage.raw_words[0u] = stage.flags;
    stage.raw_words[1u] = static_cast<std::uint32_t>(texture_index);
    stage.raw_words[2u] = static_cast<std::uint32_t>(coordinate);
    return stage;
}

NnMaterialData make_material(std::vector<NnMaterialStageData> stages = {}) {
    NnMaterialData material{};
    material.pointer_flags = kPointerFlags;
    material.descriptor_bits[0u] = kDescriptorUnlitBit;
    material.descriptor_bits[5u] = static_cast<std::uint32_t>(stages.size());
    material.color_flags = 0x80000002u;
    material.shininess_bits = 0x7FC00001u;
    material.specular_intensity_bits = 0u;
    material.stages = std::move(stages);
    return material;
}

NnMaterialData make_lit_material(std::vector<NnMaterialStageData> stages = {}) {
    NnMaterialData material{};
    material.pointer_flags = kLitPointerFlags;
    material.descriptor_bits[4u] = 0xffffffffu;
    material.descriptor_bits[5u] = static_cast<std::uint32_t>(stages.size());
    material.color_terms_bits = std::vector<std::array<std::uint32_t, 4u>>{
        {{0x3e99999au, 0x3f000000u, 0x3f400000u, 0x3f800000u}},
        {{0x3f800000u, 0x40000000u, 0x40400000u, 0x3f800000u}},
        {{0u, 0u, 0u, 0x3f800000u}},
        {{0x3dccccddu, 0x3e4ccccdu, 0x3e99999au, 0x3f800000u}},
    };
    material.color_flags = 0x80000002u;
    material.shininess_bits = 0x7fc00001u;
    material.specular_intensity_bits = 0x3f800000u;
    material.user_profile = 5u;
    material.stages = std::move(stages);
    return material;
}

NnShaderProfileKeyInput make_lit_profile(std::uint32_t user_profile) {
    NnShaderProfileKeyInput profile{};
    profile.lighting_features = 1u;
    profile.user_profile = user_profile;
    return profile;
}

bool test_maps_geometry_and_transform() {
    NnMaterialData material = make_material();
    NnMaterialProfileContext context{};
    context.vertex_format = 0x5000741eu;
    const NnMaterialData original_material = material;
    const NnMaterialProfileContext original_context = context;

    NnShaderProfileKeyInput geometry_expected{};
    geometry_expected.vertex_features = 0x1fu;
    geometry_expected.transform_mode = 2u;
    const NnShaderProfileKeyInput actual = nn_unlit_material_profile(material, context);

    NnMaterialProfileContext dynamic_context = context;
    dynamic_context.vertex_format = 0x5000701eu;
    const NnMaterialProfileContext original_dynamic_context = dynamic_context;
    NnShaderProfileKeyInput dynamic_expected = geometry_expected;
    dynamic_expected.transform_mode = 1u;
    const NnShaderProfileKeyInput dynamic_actual = nn_unlit_material_profile(material, dynamic_context);

    NnMaterialProfileContext static_context = context;
    static_context.subobject_flags = 0x100u;
    const NnMaterialProfileContext original_static_context = static_context;
    NnShaderProfileKeyInput static_expected = geometry_expected;
    static_expected.transform_mode = 0u;
    const NnShaderProfileKeyInput static_actual = nn_unlit_material_profile(material, static_context);
    return check(same_profile(actual, geometry_expected), "unlit geometry profile mismatch") &&
           check(same_profile(dynamic_actual, dynamic_expected), "unlit dynamic transform mode mismatch") &&
           check(same_profile(static_actual, static_expected), "unlit subobject transform suppression mismatch") &&
           check(same_material(material, original_material), "unlit geometry profiling modified material input") &&
           check(same_context(context, original_context), "unlit geometry profiling modified context input") &&
           check(same_context(dynamic_context, original_dynamic_context),
                 "unlit dynamic geometry profiling modified context input") &&
           check(same_context(static_context, original_static_context),
                 "unlit static geometry profiling modified context input");
}

bool test_enables_a_valid_base_stage_without_index_special_cases() {
    NnMaterialData indexed = make_material({make_stage(7, -2)});
    NnMaterialProfileContext context{};
    context.texture_stage_limit = 1u;
    const NnMaterialData original_material = indexed;
    const NnMaterialProfileContext original_context = context;

    NnShaderProfileKeyInput expected{};
    expected.base_map = true;
    expected.texture_coordinates[0u] = -2;
    const NnShaderProfileKeyInput indexed_profile = nn_unlit_material_profile(indexed, context);

    const NnMaterialData another_index = make_material({make_stage(0, -2)});
    const NnShaderProfileKeyInput another_profile = nn_unlit_material_profile(another_index, context);
    return check(same_profile(indexed_profile, expected), "valid unlit base stage profile mismatch") &&
           check(same_profile(another_profile, expected), "unlit material profile special-cased texture index") &&
           check(same_material(indexed, original_material), "unlit base-stage profiling modified material input") &&
           check(same_context(context, original_context), "unlit base-stage profiling modified context input");
}

bool test_suppresses_valid_base_stage_when_disabled() {
    const NnMaterialData material = make_material({make_stage(3, 3)});
    NnMaterialProfileContext limit_context{};
    limit_context.texture_stage_limit = 0u;
    NnMaterialProfileContext draw_context{};
    draw_context.texture_stage_limit = 8u;
    draw_context.draw_flags_low = 0x800u;
    NnMaterialProfileContext active_context{};
    active_context.texture_stage_limit = 8u;
    active_context.draw_flags_low = 0x400u;

    NnShaderProfileKeyInput active_expected{};
    active_expected.base_map = true;
    active_expected.texture_coordinates[0u] = 3;
    return check(
               same_profile(nn_unlit_material_profile(material, limit_context), NnShaderProfileKeyInput{}),
               "texture-limit suppression retained a base map") &&
           check(
               same_profile(nn_unlit_material_profile(material, draw_context), NnShaderProfileKeyInput{}),
               "draw-flag suppression retained a base map") &&
           check(
               same_profile(nn_unlit_material_profile(material, active_context), active_expected),
               "ordinary unlit draw flags did not preserve a base map");
}

template <typename Mutate>
bool rejects_invalid_input(Mutate&& mutate, const char* message) {
    NnMaterialData material = make_material({make_stage(3, 0)});
    NnMaterialProfileContext context{};
    context.texture_stage_limit = 1u;
    mutate(material, context);
    const NnMaterialData original_material = material;
    const NnMaterialProfileContext original_context = context;
    try {
        static_cast<void>(nn_unlit_material_profile(material, context));
    } catch (const std::invalid_argument&) {
        return check(same_material(material, original_material), "rejected unlit material input was modified") &&
               check(same_context(context, original_context), "rejected unlit material context was modified");
    } catch (const std::exception&) {
        return check(false, message);
    }
    return check(false, message);
}

bool test_rejects_out_of_domain_or_malformed_materials() {
    return rejects_invalid_input(
               [](NnMaterialData& material, NnMaterialProfileContext&) { material.pointer_flags = 0u; },
               "nonstandard material pointer flags were accepted") &&
           rejects_invalid_input(
               [](NnMaterialData& material, NnMaterialProfileContext&) { material.descriptor_bits[0u] = 0u; },
               "material without unlit descriptor bit was accepted") &&
           rejects_invalid_input(
               [](NnMaterialData& material, NnMaterialProfileContext&) { material.descriptor_bits[5u] = 0u; },
               "mismatched material stage count was accepted") &&
           rejects_invalid_input(
               [](NnMaterialData& material, NnMaterialProfileContext&) {
                   material.stages.push_back(make_stage(4, 0));
                   material.descriptor_bits[5u] = 2u;
               },
               "multiple unlit stages were accepted") &&
           rejects_invalid_input(
               [](NnMaterialData&, NnMaterialProfileContext& context) { context.texture_stage_limit = 9u; },
               "texture-stage limit above the supported domain was accepted") &&
           rejects_invalid_input(
               [](NnMaterialData&, NnMaterialProfileContext& context) { context.draw_flags_low = 0x1u; },
               "unsupported low draw flags were accepted") &&
           rejects_invalid_input(
               [](NnMaterialData&, NnMaterialProfileContext& context) { context.draw_flags_high = 1u; },
               "unsupported high draw flags were accepted") &&
           rejects_invalid_input(
               [](NnMaterialData& material, NnMaterialProfileContext&) { material.stages[0u].raw_words.resize(15u); },
               "short unlit stage metadata was accepted") &&
           rejects_invalid_input(
               [](NnMaterialData& material, NnMaterialProfileContext&) { material.stages[0u].raw_words[0u] = 0u; },
               "stage flags/raw metadata mismatch was accepted") &&
           rejects_invalid_input(
               [](NnMaterialData& material, NnMaterialProfileContext&) { material.stages[0u].raw_words[1u] = 4u; },
               "texture index/raw metadata mismatch was accepted") &&
           rejects_invalid_input(
               [](NnMaterialData& material, NnMaterialProfileContext&) {
                   material.stages[0u].texture_index = -1;
                   material.stages[0u].raw_words[1u] = 0xffffffffu;
               },
               "negative unlit texture index was accepted") &&
           rejects_invalid_input(
               [](NnMaterialData& material, NnMaterialProfileContext&) {
                   material.stages[0u].flags = 0x60000003u;
                   material.stages[0u].raw_words[0u] = 0x60000003u;
               },
               "unsupported unlit stage flags were accepted") &&
           rejects_invalid_input(
               [](NnMaterialData& material, NnMaterialProfileContext&) { material.stages[0u].raw_words[2u] = 4u; },
               "out-of-range unlit coordinate was accepted") &&
           rejects_invalid_input(
               [](NnMaterialData& material, NnMaterialProfileContext&) {
                   material.stages[0u].raw_words[2u] = 0xfffffffcu;
               },
               "low out-of-range unlit coordinate was accepted") &&
           rejects_invalid_input(
               [](NnMaterialData& material, NnMaterialProfileContext& context) {
                   material.stages[0u].raw_words.resize(15u);
                   context.texture_stage_limit = 0u;
               },
               "disabled malformed unlit stage metadata was accepted");
}

bool test_maps_actual_lit_fixture_profile_name_and_key() {
    NnMaterialData material = make_lit_material({make_stage(7, 0)});
    NnMaterialProfileContext context{};
    context.subobject_flags = 0x101u;
    context.vertex_format = 0x1001bu;
    context.texture_stage_limit = 1u;
    const NnMaterialData original_material = material;
    const NnMaterialProfileContext original_context = context;

    NnShaderProfileKeyInput expected = make_lit_profile(5u);
    expected.vertex_features = 0x7u;
    expected.base_map = true;
    const std::array<std::uint8_t, 16u> expected_key = {{
        0x87u, 0x00u, 0x01u, 0x00u, 0x00u, 0x6cu, 0xdbu, 0xb6u,
        0x15u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u,
    }};

    const NnShaderProfileKeyInput actual = nn_lit_material_profile(material, context);
    const std::array<std::uint8_t, 16u> actual_key = nn_shader_profile_key(actual);
    return check(same_profile(actual, expected), "actual lit material profile mismatch") &&
           check(actual_key == expected_key, "actual lit material profile key mismatch") &&
           check(
               nn_shader_archive_basename(actual_key) == "000000000000ARDMRC00002047",
               "actual lit material archive basename mismatch") &&
           check(same_material(material, original_material), "actual lit material profiling modified material input") &&
           check(same_context(context, original_context), "actual lit material profiling modified context input");
}

bool test_maps_standard_lit_material_without_extension() {
    NnMaterialData extended = make_lit_material({make_stage(3, 0)});
    extended.user_profile = 0u;
    NnMaterialData standard = extended;
    standard.pointer_flags = kPointerFlags;
    standard.user_profile.reset();
    const NnMaterialData original = standard;
    const NnMaterialProfileContext context{0x201u, 0x17403u, 0x20000u, 8u, 1u};
    const auto standard_profile = nn_lit_material_profile(standard, context);
    const auto extended_profile = nn_lit_material_profile(extended, context);
    if (!check(same_profile(standard_profile, extended_profile),
            "standard lit material differs from an explicit zero user profile")
            || !check(same_material(standard, original), "standard lit material input was modified")) {
        return false;
    }
    NnMaterialData ring = standard;
    ring.stages[0u] = make_stage(0, -1);
    ring.specular_intensity_bits = 0x3f800000u;
    ring.color_terms_bits[kLitSpecularTermIndex] = {{0x3f008312u, 0x3f008312u, 0x3eb6c8b4u, 0x3f800000u}};
    const NnMaterialProfileContext ring_context{0x101u, 3u, 0x20000u, 2u, 1u};
    if (!check(nn_shader_archive_basename(nn_shader_profile_key(
            nn_lit_material_profile(ring, ring_context))) == "0000000000000RDMR8000022C4",
            "standard ring reflection profile mismatch")) {
        return false;
    }
    ring.specular_intensity_bits = 0xbf800000u;
    if (!check(nn_lit_material_profile(ring, ring_context).lighting_features == 3u,
               "negative nonzero specular intensity suppressed the specular profile")) {
        return false;
    }
    ring.specular_intensity_bits = 0x80000000u;
    if (!check(nn_shader_archive_basename(nn_shader_profile_key(
            nn_lit_material_profile(ring, ring_context))) == "0000000000000RDMR800002244",
            "zero intensity retained the specular profile")) {
        return false;
    }
    ring.specular_intensity_bits = 0x3f800000u;
    ring.color_terms_bits[kLitSpecularTermIndex] = {{0xbf800000u, 0u, 0u, 0x3f800000u}};
    ring.pointer_flags = 0x30000000u;
    ring.user_profile = 0u;
    if (!check(nn_lit_material_profile(ring, ring_context).lighting_features == 3u,
               "extended material suppressed negative nonzero specular RGB")) {
        return false;
    }
    standard.user_profile = 1u;
    try {
        nn_lit_material_profile(standard, context);
    } catch (const std::invalid_argument&) {
        return true;
    }
    return check(false, "standard lit material accepted an inconsistent user extension");
}

bool test_maps_lit_parallel_light_counts_and_user_profiles() {
    const std::array<std::uint32_t, 3u> user_profiles = {{0u, 5u, 63u}};
    for (std::uint32_t light_count = 0u; light_count <= 3u; ++light_count) {
        for (std::uint32_t user_profile : user_profiles) {
            NnMaterialData material = make_lit_material();
            material.user_profile = user_profile;
            NnMaterialProfileContext context{};
            context.draw_flags_low = light_count << 16u;
            const NnMaterialData original_material = material;
            const NnMaterialProfileContext original_context = context;

            NnShaderProfileKeyInput expected = make_lit_profile(user_profile);
            expected.parallel_lights = light_count;
            const NnShaderProfileKeyInput actual = nn_lit_material_profile(material, context);
            if (!check(same_profile(actual, expected), "lit parallel-light/user-profile mapping mismatch") ||
                !check(same_material(material, original_material), "lit light-count profiling modified material input") ||
                !check(same_context(context, original_context), "lit light-count profiling modified context input")) {
                return false;
            }
        }
    }
    return true;
}

bool test_maps_lit_drawobject_profiles_for_weighted_contexts() {
    const std::array<std::uint32_t, 2u> drawobject_selectors = {{4u, 8u}};
    const std::array<std::uint32_t, 3u> subobject_flags = {{0x100u, 0x201u, 0x201u}};
    const std::array<std::uint32_t, 3u> vertex_formats = {{0x17003u, 0x17003u, 0x17403u}};
    const std::array<std::uint32_t, 3u> transform_modes = {{0u, 1u, 2u}};
    if (!check((subobject_flags[1u] & 0x100u) == 0u, "weighted lit fixture includes the static flag")) {
        return false;
    }

    for (std::uint32_t drawobject_selector : drawobject_selectors) {
        for (std::uint32_t light_count = 0u; light_count <= 3u; ++light_count) {
            for (std::size_t index = 0u; index < subobject_flags.size(); ++index) {
                NnMaterialData material = make_lit_material({make_stage(7, 0)});
                material.user_profile = 1u;
                NnMaterialProfileContext context{};
                context.subobject_flags = subobject_flags[index];
                context.vertex_format = vertex_formats[index];
                context.draw_flags_low = light_count << 16u;
                context.draw_flags_high = drawobject_selector;
                context.texture_stage_limit = 1u;
                const NnMaterialData original_material = material;
                const NnMaterialProfileContext original_context = context;

                NnShaderProfileKeyInput expected = make_lit_profile(1u);
                expected.vertex_features = 0x4u;
                expected.transform_mode = transform_modes[index];
                expected.parallel_lights = light_count;
                expected.drawobject_profile = drawobject_selector >> 2u;
                expected.base_map = true;
                const NnShaderProfileKeyInput actual = nn_lit_material_profile(material, context);
                if (!check(same_profile(actual, expected), "weighted lit material profile mismatch") ||
                    !check(same_material(material, original_material), "weighted lit profiling modified material input") ||
                    !check(same_context(context, original_context), "weighted lit profiling modified context input")) {
                    return false;
                }
            }
        }
    }

    return true;
}

bool test_enables_suppresses_and_omits_lit_base_stages() {
    NnMaterialData staged_material = make_lit_material({make_stage(3, -3)});
    NnMaterialProfileContext active_context{};
    active_context.texture_stage_limit = 8u;
    NnMaterialProfileContext suppressed_context{};
    suppressed_context.texture_stage_limit = 0u;
    const NnMaterialData original_staged_material = staged_material;
    const NnMaterialProfileContext original_active_context = active_context;
    const NnMaterialProfileContext original_suppressed_context = suppressed_context;

    NnShaderProfileKeyInput active_expected = make_lit_profile(5u);
    active_expected.base_map = true;
    active_expected.texture_coordinates[0u] = -3;
    const NnShaderProfileKeyInput suppressed_expected = make_lit_profile(5u);
    const NnShaderProfileKeyInput active = nn_lit_material_profile(staged_material, active_context);
    const NnShaderProfileKeyInput suppressed = nn_lit_material_profile(staged_material, suppressed_context);

    NnMaterialData no_stage_material = make_lit_material();
    NnMaterialProfileContext no_stage_context{};
    no_stage_context.texture_stage_limit = 1u;
    const NnMaterialData original_no_stage_material = no_stage_material;
    const NnMaterialProfileContext original_no_stage_context = no_stage_context;
    const NnShaderProfileKeyInput no_stage = nn_lit_material_profile(no_stage_material, no_stage_context);
    return check(same_profile(active, active_expected), "active lit base-stage profile mismatch") &&
           check(same_profile(suppressed, suppressed_expected), "suppressed lit base stage retained a base map") &&
           check(same_profile(no_stage, suppressed_expected), "no-stage lit material retained a base map") &&
           check(same_material(staged_material, original_staged_material), "lit stage profiling modified material input") &&
           check(same_context(active_context, original_active_context), "active lit stage profiling modified context input") &&
           check(
               same_context(suppressed_context, original_suppressed_context),
               "suppressed lit stage profiling modified context input") &&
           check(same_material(no_stage_material, original_no_stage_material), "no-stage lit profiling modified material input") &&
           check(same_context(no_stage_context, original_no_stage_context), "no-stage lit profiling modified context input");
}

bool test_reuses_geometry_helpers_for_lit_profiles() {
    NnMaterialData material = make_lit_material();
    NnMaterialProfileContext transformed_context{};
    transformed_context.vertex_format = 0x5000741eu;
    NnMaterialProfileContext dynamic_context = transformed_context;
    dynamic_context.vertex_format = 0x5000701eu;
    NnMaterialProfileContext static_context = transformed_context;
    static_context.subobject_flags = 0x101u;
    NnMaterialProfileContext arbitrary_context{};
    arbitrary_context.subobject_flags = 0xffffffffu;
    arbitrary_context.vertex_format = 0xffffffffu;
    const NnMaterialData original_material = material;
    const NnMaterialProfileContext original_transformed_context = transformed_context;
    const NnMaterialProfileContext original_dynamic_context = dynamic_context;
    const NnMaterialProfileContext original_static_context = static_context;
    const NnMaterialProfileContext original_arbitrary_context = arbitrary_context;

    NnShaderProfileKeyInput transformed_expected = make_lit_profile(5u);
    transformed_expected.vertex_features = 0x1fu;
    transformed_expected.transform_mode = 2u;
    NnShaderProfileKeyInput dynamic_expected = transformed_expected;
    dynamic_expected.transform_mode = 1u;
    NnShaderProfileKeyInput static_expected = transformed_expected;
    static_expected.transform_mode = 0u;
    NnShaderProfileKeyInput arbitrary_expected = make_lit_profile(5u);
    arbitrary_expected.vertex_features = 0x1fu;

    return check(
               same_profile(nn_lit_material_profile(material, transformed_context), transformed_expected),
               "lit transformed geometry profile mismatch") &&
           check(
               same_profile(nn_lit_material_profile(material, dynamic_context), dynamic_expected),
               "lit dynamic geometry profile mismatch") &&
           check(
               same_profile(nn_lit_material_profile(material, static_context), static_expected),
               "lit static geometry profile mismatch") &&
           check(
               same_profile(nn_lit_material_profile(material, arbitrary_context), arbitrary_expected),
               "lit arbitrary geometry profile mismatch") &&
           check(same_material(material, original_material), "lit geometry profiling modified material input") &&
           check(
               same_context(transformed_context, original_transformed_context),
               "lit transformed geometry profiling modified context input") &&
           check(
               same_context(dynamic_context, original_dynamic_context),
               "lit dynamic geometry profiling modified context input") &&
           check(
               same_context(static_context, original_static_context),
               "lit static geometry profiling modified context input") &&
           check(
               same_context(arbitrary_context, original_arbitrary_context),
               "lit arbitrary geometry profiling modified context input");
}

bool test_accepts_signed_zero_and_finite_nonspecular_lit_inputs() {
    NnMaterialData zero_intensity_material = make_lit_material();
    zero_intensity_material.specular_intensity_bits = 0x80000000u;
    zero_intensity_material.color_terms_bits[kLitSpecularTermIndex] = {{
        0x3f800000u,
        0x80000000u,
        0xbf800000u,
        0x3f800000u,
    }};
    NnMaterialData zero_specular_material = make_lit_material();
    zero_specular_material.specular_intensity_bits = 0xff7fffffu;
    zero_specular_material.color_terms_bits[kLitSpecularTermIndex] = {{
        0x80000000u,
        0u,
        0x80000000u,
        0x3f800000u,
    }};
    NnMaterialData finite_nonspecular_material = make_lit_material();
    finite_nonspecular_material.specular_intensity_bits = 0u;
    finite_nonspecular_material.color_terms_bits[0u] = {{
        0x00000001u,
        0x80000001u,
        0x3f800000u,
        0x3f800000u,
    }};
    finite_nonspecular_material.color_terms_bits[1u] = {{
        0x3f000000u,
        0xbf000000u,
        0x7f7fffffu,
        0x3f800000u,
    }};
    finite_nonspecular_material.color_terms_bits[kLitSpecularTermIndex] = {{
        0x3f800000u,
        0xbf800000u,
        0x7f7fffffu,
        0x3f800000u,
    }};
    finite_nonspecular_material.color_terms_bits[3u] = {{
        0xff7fffffu,
        0x3e99999au,
        0x3e4ccccdu,
        0x3f800000u,
    }};
    const NnMaterialData original_zero_intensity_material = zero_intensity_material;
    const NnMaterialData original_zero_specular_material = zero_specular_material;
    const NnMaterialData original_finite_nonspecular_material = finite_nonspecular_material;
    const NnMaterialProfileContext context{};
    const NnShaderProfileKeyInput expected = make_lit_profile(5u);

    return check(
               same_profile(nn_lit_material_profile(zero_intensity_material, context), expected),
               "negative-zero lit intensity did not suppress finite specular RGB") &&
           check(
               same_profile(nn_lit_material_profile(zero_specular_material, context), expected),
               "negative-zero lit specular RGB did not suppress finite intensity") &&
           check(
               same_profile(nn_lit_material_profile(finite_nonspecular_material, context), expected),
               "finite nonspecular lit material was rejected") &&
           check(
               same_material(zero_intensity_material, original_zero_intensity_material),
               "signed-zero lit intensity profiling modified material input") &&
           check(
               same_material(zero_specular_material, original_zero_specular_material),
               "signed-zero lit RGB profiling modified material input") &&
           check(
               same_material(finite_nonspecular_material, original_finite_nonspecular_material),
               "finite nonspecular lit profiling modified material input");
}

template <typename Mutate>
bool rejects_invalid_lit_input(Mutate&& mutate, const char* message) {
    NnMaterialData material = make_lit_material({make_stage(3, 0)});
    NnMaterialProfileContext context{};
    context.texture_stage_limit = 1u;
    mutate(material, context);
    const NnMaterialData original_material = material;
    const NnMaterialProfileContext original_context = context;
    try {
        static_cast<void>(nn_lit_material_profile(material, context));
    } catch (const std::invalid_argument&) {
        return check(same_material(material, original_material), "rejected lit material input was modified") &&
               check(same_context(context, original_context), "rejected lit material context was modified");
    } catch (const std::exception&) {
        return check(false, message);
    }
    return check(false, message);
}

bool test_rejects_lit_out_of_domain_or_malformed_materials() {
    return rejects_invalid_lit_input(
               [](NnMaterialData& material, NnMaterialProfileContext&) { material.pointer_flags = 0x20000000u; },
               "non-lit material pointer flags were accepted") &&
           rejects_invalid_lit_input(
               [](NnMaterialData& material, NnMaterialProfileContext&) { material.pointer_flags = 0x30000001u; },
               "lit material pointer flags with unsupported bits were accepted") &&
           rejects_invalid_lit_input(
               [](NnMaterialData& material, NnMaterialProfileContext&) { material.descriptor_bits[0u] = 0x1u; },
               "nonzero lit descriptor flags were accepted") &&
           rejects_invalid_lit_input(
               [](NnMaterialData& material, NnMaterialProfileContext&) { material.descriptor_bits[5u] = 0u; },
               "mismatched lit stage count was accepted") &&
           rejects_invalid_lit_input(
               [](NnMaterialData& material, NnMaterialProfileContext&) {
                   material.stages.push_back(make_stage(4, 0));
                   material.descriptor_bits[5u] = 2u;
               },
               "multiple lit stages were accepted") &&
           rejects_invalid_lit_input(
               [](NnMaterialData& material, NnMaterialProfileContext&) { material.color_terms_bits.pop_back(); },
               "short lit color-term array was accepted") &&
           rejects_invalid_lit_input(
               [](NnMaterialData& material, NnMaterialProfileContext&) {
                   material.color_terms_bits.push_back(material.color_terms_bits[0u]);
               },
               "long lit color-term array was accepted") &&
           rejects_invalid_lit_input(
               [](NnMaterialData& material, NnMaterialProfileContext&) { material.user_profile.reset(); },
               "lit material without a user profile was accepted") &&
           rejects_invalid_lit_input(
               [](NnMaterialData& material, NnMaterialProfileContext&) { material.user_profile = 64u; },
               "out-of-range lit user profile was accepted") &&
           rejects_invalid_lit_input(
               [](NnMaterialData&, NnMaterialProfileContext& context) { context.texture_stage_limit = 9u; },
               "out-of-range lit texture-stage limit was accepted") &&
           rejects_invalid_lit_input(
               [](NnMaterialData&, NnMaterialProfileContext& context) { context.draw_flags_low = 0x30001u; },
               "unsupported lit low draw flags were accepted") &&
           rejects_invalid_lit_input(
               [](NnMaterialData&, NnMaterialProfileContext& context) { context.draw_flags_low = 0x400u; },
               "unlit-only low draw flags were accepted by the lit material profile") &&
           rejects_invalid_lit_input(
               [](NnMaterialData&, NnMaterialProfileContext& context) { context.draw_flags_high = 1u; },
               "unsupported lit high draw flags were accepted") &&
           rejects_invalid_lit_input(
               [](NnMaterialData&, NnMaterialProfileContext& context) { context.draw_flags_high = 3u; },
               "unsupported mixed lit high draw flags were accepted") &&
           rejects_invalid_lit_input(
               [](NnMaterialData&, NnMaterialProfileContext& context) { context.draw_flags_high = 0x10u; },
               "unsupported lit high draw selector 0x10 was accepted") &&
           rejects_invalid_lit_input(
               [](NnMaterialData&, NnMaterialProfileContext& context) { context.draw_flags_high = 0xcu; },
               "mixed lit high draw selectors were accepted") &&
           rejects_invalid_lit_input(
               [](NnMaterialData&, NnMaterialProfileContext& context) { context.draw_flags_high = 0x100u; },
               "large lit high draw selector was accepted") &&
           rejects_invalid_lit_input(
               [](NnMaterialData& material, NnMaterialProfileContext&) { material.stages[0u].raw_words.resize(15u); },
               "short lit stage metadata was accepted") &&
           rejects_invalid_lit_input(
               [](NnMaterialData& material, NnMaterialProfileContext&) { material.stages[0u].raw_words[0u] = 0u; },
               "lit stage raw flags outside the supported domain were accepted") &&
           rejects_invalid_lit_input(
               [](NnMaterialData& material, NnMaterialProfileContext&) {
                   material.stages[0u].flags = 0x60000003u;
                   material.stages[0u].raw_words[0u] = 0x60000003u;
               },
               "lit stage flags outside the supported domain were accepted") &&
           rejects_invalid_lit_input(
               [](NnMaterialData& material, NnMaterialProfileContext&) { material.stages[0u].raw_words[1u] = 4u; },
               "lit texture index/raw metadata mismatch was accepted") &&
           rejects_invalid_lit_input(
               [](NnMaterialData& material, NnMaterialProfileContext&) {
                   material.stages[0u].texture_index = -1;
                   material.stages[0u].raw_words[1u] = 0xffffffffu;
               },
               "negative lit texture index was accepted") &&
           rejects_invalid_lit_input(
               [](NnMaterialData& material, NnMaterialProfileContext&) { material.stages[0u].raw_words[2u] = 4u; },
               "high out-of-range lit coordinate was accepted") &&
           rejects_invalid_lit_input(
               [](NnMaterialData& material, NnMaterialProfileContext&) {
                   material.stages[0u].raw_words[2u] = 0xfffffffcu;
               },
               "low out-of-range lit coordinate was accepted");
}

bool test_rejects_lit_nonfinite_specular_values() {
    const std::array<std::uint32_t, 3u> nonfinite_values = {{
        0x7f800000u,
        0xff800000u,
        0x7fc00000u,
    }};
    for (std::uint32_t bits : nonfinite_values) {
        if (!rejects_invalid_lit_input(
                [bits](NnMaterialData& material, NnMaterialProfileContext&) {
                    material.specular_intensity_bits = bits;
                },
                "nonfinite lit specular intensity was accepted")) {
            return false;
        }
        for (std::size_t color_index = 0u; color_index < 3u; ++color_index) {
            if (!rejects_invalid_lit_input(
                    [bits, color_index](NnMaterialData& material, NnMaterialProfileContext&) {
                        material.specular_intensity_bits = 0u;
                        material.color_terms_bits[kLitSpecularTermIndex][color_index] = bits;
                    },
                    "nonfinite lit specular RGB value was accepted")) {
                return false;
            }
        }
    }
    return true;
}

bool test_rejects_malformed_suppressed_lit_stage() {
    return rejects_invalid_lit_input(
        [](NnMaterialData& material, NnMaterialProfileContext& context) {
            material.stages[0u].raw_words.resize(15u);
            context.texture_stage_limit = 0u;
        },
        "suppressed malformed lit stage metadata was accepted");
}

}

int main() {
    return test_maps_geometry_and_transform() &&
                    test_enables_a_valid_base_stage_without_index_special_cases() &&
                    test_suppresses_valid_base_stage_when_disabled() &&
                    test_rejects_out_of_domain_or_malformed_materials() &&
                    test_maps_actual_lit_fixture_profile_name_and_key() &&
                    test_maps_standard_lit_material_without_extension() &&
                    test_maps_lit_parallel_light_counts_and_user_profiles() &&
                    test_maps_lit_drawobject_profiles_for_weighted_contexts() &&
                    test_enables_suppresses_and_omits_lit_base_stages() &&
                    test_reuses_geometry_helpers_for_lit_profiles() &&
                    test_accepts_signed_zero_and_finite_nonspecular_lit_inputs() &&
                    test_rejects_lit_out_of_domain_or_malformed_materials() &&
                    test_rejects_lit_nonfinite_specular_values() &&
                    test_rejects_malformed_suppressed_lit_stage()
                ? 0
                : 1;
}
