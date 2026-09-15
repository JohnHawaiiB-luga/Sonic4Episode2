#include "nn_material_state.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <cstdio>
#include <exception>
#include <stdexcept>
#include <utility>
#include <vector>

namespace {

constexpr std::uint32_t kPointerFlags = 0x10000000u;
constexpr std::uint32_t kUnlitDescriptorBit = 0x2u;
constexpr std::uint32_t kLitPointerFlags = 0x30000000u;
constexpr std::uint32_t kLitDescriptorStageMask = 0x2u;
constexpr std::uint32_t kStageFlags = 0x60000002u;

using LogicWords = std::array<std::uint32_t, 7u>;

bool check(bool condition, const char* message) {
    if (condition) {
        return true;
    }
    std::fprintf(stderr, "%s\n", message);
    return false;
}

bool same_blend(const sonic4ep2::d3d9::BlendDescription& left, const sonic4ep2::d3d9::BlendDescription& right) {
    return left.enabled == right.enabled &&
           left.source_color == right.source_color &&
           left.destination_color == right.destination_color &&
           left.color_operation == right.color_operation &&
           left.separate_alpha == right.separate_alpha &&
           left.source_alpha == right.source_alpha &&
           left.destination_alpha == right.destination_alpha &&
           left.alpha_operation == right.alpha_operation &&
           left.blend_factor_rgba == right.blend_factor_rgba;
}

bool same_stencil_face(
    const sonic4ep2::d3d9::StencilFaceDescription& left,
    const sonic4ep2::d3d9::StencilFaceDescription& right) {
    return left.compare == right.compare &&
           left.fail == right.fail &&
           left.depth_fail == right.depth_fail &&
           left.pass == right.pass;
}

bool same_depth_stencil(
    const sonic4ep2::d3d9::DepthStencilDescription& left,
    const sonic4ep2::d3d9::DepthStencilDescription& right) {
    return left.depth_enable == right.depth_enable &&
           left.depth_write == right.depth_write &&
           left.depth_compare == right.depth_compare &&
           left.stencil_enable == right.stencil_enable &&
           left.stencil_reference == right.stencil_reference &&
           left.stencil_read_mask == right.stencil_read_mask &&
           left.stencil_write_mask == right.stencil_write_mask &&
           same_stencil_face(left.clockwise, right.clockwise) &&
           same_stencil_face(left.counter_clockwise, right.counter_clockwise) &&
           left.two_sided_stencil_enable == right.two_sided_stencil_enable;
}

bool same_alpha_test(
    const sonic4ep2::d3d9::AlphaTestDescription& left,
    const sonic4ep2::d3d9::AlphaTestDescription& right) {
    return left.enabled == right.enabled && left.compare == right.compare && left.reference == right.reference;
}

bool same_scissor(
    const sonic4ep2::d3d9::ScissorRectangle& left,
    const sonic4ep2::d3d9::ScissorRectangle& right) {
    return left.left == right.left && left.top == right.top && left.right == right.right && left.bottom == right.bottom;
}

bool same_rasterizer(
    const sonic4ep2::d3d9::RasterizerDescription& left,
    const sonic4ep2::d3d9::RasterizerDescription& right) {
    return left.cull_mode == right.cull_mode &&
           left.fill_mode == right.fill_mode &&
           left.scissor_enable == right.scissor_enable &&
           left.multisample_antialias == right.multisample_antialias &&
           left.antialiased_line_enable == right.antialiased_line_enable &&
           left.srgb_write_enable == right.srgb_write_enable &&
           same_scissor(left.scissor_rectangle, right.scissor_rectangle);
}

bool same_render_state(
    const sonic4ep2::d3d9::RenderStateDescription& left,
    const sonic4ep2::d3d9::RenderStateDescription& right) {
    return same_blend(left.blend, right.blend) &&
           same_depth_stencil(left.depth_stencil, right.depth_stencil) &&
           same_alpha_test(left.alpha_test, right.alpha_test) &&
           left.color_write_mask == right.color_write_mask &&
           same_rasterizer(left.rasterizer, right.rasterizer);
}

bool same_stage(const NnMaterialStageData& left, const NnMaterialStageData& right) {
    return left.flags == right.flags && left.texture_index == right.texture_index && left.raw_words == right.raw_words;
}

bool same_material(const NnMaterialData& left, const NnMaterialData& right) {
    if (left.pointer_flags != right.pointer_flags ||
        left.descriptor_bits != right.descriptor_bits ||
        left.color_terms_bits != right.color_terms_bits ||
        left.color_flags != right.color_flags ||
        left.shininess_bits != right.shininess_bits ||
        left.specular_intensity_bits != right.specular_intensity_bits ||
        left.user_profile != right.user_profile ||
        left.render_state.has_value() != right.render_state.has_value() ||
        left.stages.size() != right.stages.size()) {
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

NnMaterialStageData make_stage() {
    NnMaterialStageData stage{};
    stage.flags = kStageFlags;
    stage.texture_index = 7;
    stage.raw_words.resize(16u);
    stage.raw_words[0u] = stage.flags;
    stage.raw_words[1u] = static_cast<std::uint32_t>(stage.texture_index);
    stage.raw_words[2u] = 0u;
    return stage;
}

NnMaterialData make_material(LogicWords logic, std::uint32_t descriptor_flags = kUnlitDescriptorBit) {
    NnMaterialData material{};
    material.pointer_flags = kPointerFlags;
    material.descriptor_bits[0u] = descriptor_flags;
    material.descriptor_bits[4u] = 0x2u;
    material.descriptor_bits[5u] = 1u;
    material.color_terms_bits = std::vector<std::array<std::uint32_t, 4u>>{
        {{0x3f800000u, 0x40000000u, 0x40400000u, 0x40800000u}},
        {{0x40a00000u, 0x40c00000u, 0x40e00000u, 0x41000000u}},
    };
    material.color_flags = 0x80000002u;
    material.shininess_bits = 0x3f000000u;
    material.specular_intensity_bits = 0x3f800000u;
    NnMaterialStateData state{};
    state.words = logic;
    material.render_state = state;
    material.stages.push_back(make_stage());
    return material;
}

NnMaterialData make_lit_material(LogicWords logic) {
    NnMaterialData material{};
    material.pointer_flags = kLitPointerFlags;
    material.descriptor_bits[4u] = kLitDescriptorStageMask;
    material.descriptor_bits[5u] = 1u;
    material.color_terms_bits = std::vector<std::array<std::uint32_t, 4u>>{
        {{0x3e828f5cu, 0x3efced91u, 0x3f0a7efau, 0x3f800000u}},
        {{0x3f800000u, 0x3f800000u, 0x3f800000u, 0x3f800000u}},
        {{0x00000000u, 0x00000000u, 0x00000000u, 0x3f800000u}},
        {{0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u}},
    };
    material.color_flags = 0x2u;
    material.shininess_bits = 0x41000000u;
    material.specular_intensity_bits = 0x3f800000u;
    material.user_profile = 5u;
    NnMaterialStateData state{};
    state.words = logic;
    material.render_state = state;
    material.stages.push_back(make_stage());
    return material;
}

NnMaterialProfileContext make_context() {
    NnMaterialProfileContext context{};
    context.texture_stage_limit = 1u;
    return context;
}

NnMaterialProfileContext make_lit_context(std::uint32_t draw_flags_low = 0u) {
    NnMaterialProfileContext context = make_context();
    context.draw_flags_low = draw_flags_low;
    return context;
}

sonic4ep2::d3d9::RenderStateDescription prior_state() {
    using namespace sonic4ep2::d3d9;

    RenderStateDescription state{};
    state.blend = BlendDescription{
        true,
        BlendFactor::InverseDestinationColor,
        BlendFactor::SourceColor,
        BlendOperation::Max,
        true,
        BlendFactor::DestinationAlpha,
        BlendFactor::InverseSourceAlpha,
        BlendOperation::Min,
        0x13579bdfu,
    };
    state.depth_stencil = DepthStencilDescription{
        false,
        false,
        CompareFunction::NotEqual,
        true,
        0x5au,
        0x12u,
        0x34u,
        StencilFaceDescription{
            CompareFunction::Greater,
            StencilOperation::Replace,
            StencilOperation::Increment,
            StencilOperation::Decrement,
        },
        StencilFaceDescription{
            CompareFunction::Less,
            StencilOperation::Invert,
            StencilOperation::DecrementSaturate,
            StencilOperation::IncrementSaturate,
        },
        false,
    };
    state.alpha_test = AlphaTestDescription{true, CompareFunction::Greater, 0x7bu};
    state.color_write_mask = 0x0du;
    state.rasterizer = RasterizerDescription{
        CullMode::CounterClockwise,
        FillMode::Wireframe,
        true,
        true,
        true,
        true,
        ScissorRectangle{1, 2, 7, 9},
    };
    return state;
}

LogicWords model164_logic() {
    return {{
        0x10u,
        0x00060005u,
        0x00060005u,
        0x00000000u,
        0x15050001u,
        0x00040005u,
        0x00000000u,
    }};
}

sonic4ep2::d3d9::RenderStateDescription model164_expected_state(
    const sonic4ep2::d3d9::RenderStateDescription& prior) {
    using namespace sonic4ep2::d3d9;

    RenderStateDescription expected = prior;
    expected.blend.enabled = false;
    expected.blend.source_color = BlendFactor::One;
    expected.blend.destination_color = BlendFactor::Zero;
    expected.blend.color_operation = BlendOperation::Add;
    expected.blend.separate_alpha = false;
    expected.blend.source_alpha = BlendFactor::One;
    expected.blend.destination_alpha = BlendFactor::Zero;
    expected.blend.alpha_operation = BlendOperation::Add;
    expected.blend.blend_factor_rgba = 0u;
    expected.depth_stencil.depth_enable = true;
    expected.depth_stencil.depth_write = true;
    expected.depth_stencil.depth_compare = CompareFunction::LessEqual;
    expected.alpha_test.enabled = false;
    expected.color_write_mask = 0x0fu;
    expected.rasterizer.fill_mode = FillMode::Solid;
    expected.rasterizer.cull_mode = CullMode::Clockwise;
    return expected;
}

bool test_selected_state_preserves_all_unassigned_prior_fields() {
    using namespace sonic4ep2::d3d9;

    const LogicWords logic = {{
        0x11u,
        0x00010009u,
        0x00060005u,
        0u,
        0x15050001u,
        0x00040005u,
        0u,
    }};
    NnMaterialData material = make_material(logic, 0x1102u);
    NnMaterialProfileContext context = make_context();
    RenderStateDescription prior = prior_state();
    const NnMaterialData original_material = material;
    const NnMaterialProfileContext original_context = context;
    const RenderStateDescription original_prior = prior;

    const NnUnlitMaterialState actual = nn_unlit_material_state(material, context, prior, false);
    RenderStateDescription expected = prior;
    expected.blend.enabled = true;
    expected.blend.source_color = BlendFactor::DestinationColor;
    expected.blend.destination_color = BlendFactor::Zero;
    expected.blend.color_operation = BlendOperation::Add;
    expected.blend.separate_alpha = false;
    expected.blend.source_alpha = BlendFactor::One;
    expected.blend.destination_alpha = BlendFactor::Zero;
    expected.blend.alpha_operation = BlendOperation::Add;
    expected.blend.blend_factor_rgba = 0u;
    expected.depth_stencil.depth_enable = true;
    expected.depth_stencil.depth_write = false;
    expected.depth_stencil.depth_compare = CompareFunction::LessEqual;
    expected.alpha_test.enabled = false;
    expected.color_write_mask = 0x07u;
    expected.rasterizer.fill_mode = FillMode::Solid;
    expected.rasterizer.cull_mode = CullMode::Clockwise;

    return check(same_render_state(actual.render_state, expected), "selected material state conversion mismatch") &&
           check(!actual.fog_enabled, "selected material state did not preserve disabled fog") &&
           check(
               actual.render_state.alpha_test.compare == prior.alpha_test.compare &&
                   actual.render_state.alpha_test.reference == prior.alpha_test.reference,
               "disabled alpha test did not preserve compare or reference") &&
           check(same_material(material, original_material), "selected material state modified material input") &&
           check(same_context(context, original_context), "selected material state modified context input") &&
           check(same_render_state(prior, original_prior), "selected material state modified prior render state");
}

bool test_source_alpha_and_separate_alpha_share_the_color_operation() {
    using namespace sonic4ep2::d3d9;

    const LogicWords logic = {{
        0x3u,
        0x00060005u,
        0x000f000eu,
        0x12030405u,
        0x00050003u,
        0xffffffffu,
        0xffffffffu,
    }};
    NnMaterialData material = make_material(logic, 0x3u);
    NnMaterialProfileContext context = make_context();
    RenderStateDescription prior = prior_state();
    const NnMaterialData original_material = material;
    const NnMaterialProfileContext original_context = context;
    const RenderStateDescription original_prior = prior;

    const NnUnlitMaterialState actual = nn_unlit_material_state(material, context, prior, true);
    RenderStateDescription expected = prior;
    expected.blend.enabled = true;
    expected.blend.source_color = BlendFactor::SourceAlpha;
    expected.blend.destination_color = BlendFactor::InverseSourceAlpha;
    expected.blend.color_operation = BlendOperation::ReverseSubtract;
    expected.blend.separate_alpha = true;
    expected.blend.source_alpha = BlendFactor::BlendFactor;
    expected.blend.destination_alpha = BlendFactor::InverseBlendFactor;
    expected.blend.alpha_operation = BlendOperation::ReverseSubtract;
    expected.blend.blend_factor_rgba = 0x03040512u;
    expected.depth_stencil.depth_enable = false;
    expected.depth_stencil.depth_write = true;
    expected.alpha_test.enabled = false;
    expected.color_write_mask = 0x0fu;
    expected.rasterizer.fill_mode = FillMode::Solid;
    expected.rasterizer.cull_mode = CullMode::None;

    return check(same_render_state(actual.render_state, expected), "separate-alpha material state conversion mismatch") &&
           check(actual.fog_enabled, "separate-alpha material state did not preserve requested fog") &&
           check(
               actual.render_state.blend.alpha_operation == actual.render_state.blend.color_operation,
               "separate alpha used the unused high operation word") &&
           check(
               actual.render_state.depth_stencil.depth_compare == prior.depth_stencil.depth_compare &&
                   actual.render_state.alpha_test.compare == prior.alpha_test.compare &&
                   actual.render_state.alpha_test.reference == prior.alpha_test.reference,
               "inactive depth or alpha comparison fields changed") &&
           check(same_material(material, original_material), "separate-alpha material state modified material input") &&
           check(same_context(context, original_context), "separate-alpha material state modified context input") &&
           check(same_render_state(prior, original_prior), "separate-alpha material state modified prior render state");
}

bool test_blend_disabled_ignores_unused_enums_and_applies_alpha_depth() {
    using namespace sonic4ep2::d3d9;

    const LogicWords logic = {{
        0x18u,
        0x000d000cu,
        0x000d000cu,
        0x80112233u,
        0x00060006u,
        0x00070003u,
        0xabcdef92u,
    }};
    NnMaterialData material = make_material(logic, 0x6u);
    NnMaterialProfileContext context = make_context();
    RenderStateDescription prior = prior_state();
    const NnMaterialData original_material = material;
    const NnMaterialProfileContext original_context = context;
    const RenderStateDescription original_prior = prior;

    const NnUnlitMaterialState actual = nn_unlit_material_state(material, context, prior, true);
    RenderStateDescription expected = prior;
    expected.blend.enabled = false;
    expected.blend.source_color = BlendFactor::One;
    expected.blend.destination_color = BlendFactor::Zero;
    expected.blend.color_operation = BlendOperation::Add;
    expected.blend.separate_alpha = false;
    expected.blend.source_alpha = BlendFactor::One;
    expected.blend.destination_alpha = BlendFactor::Zero;
    expected.blend.alpha_operation = BlendOperation::Add;
    expected.blend.blend_factor_rgba = 0x11223380u;
    expected.depth_stencil.depth_enable = true;
    expected.depth_stencil.depth_write = true;
    expected.depth_stencil.depth_compare = CompareFunction::GreaterEqual;
    expected.alpha_test.enabled = true;
    expected.alpha_test.compare = CompareFunction::Equal;
    expected.alpha_test.reference = 0x92u;
    expected.color_write_mask = 0x0fu;
    expected.rasterizer.fill_mode = FillMode::Solid;
    expected.rasterizer.cull_mode = CullMode::Clockwise;

    return check(same_render_state(actual.render_state, expected), "blend-disabled alpha/depth material state mismatch") &&
           check(!actual.fog_enabled, "descriptor fog disable bit was ignored") &&
           check(same_material(material, original_material), "blend-disabled material state modified material input") &&
           check(same_context(context, original_context), "blend-disabled material state modified context input") &&
           check(same_render_state(prior, original_prior), "blend-disabled material state modified prior render state");
}

bool test_supported_state_enums_in_active_fields() {
    using namespace sonic4ep2::d3d9;

    const std::array<std::pair<std::uint32_t, BlendFactor>, 13u> factors = {{
        {1u, BlendFactor::Zero}, {2u, BlendFactor::One},
        {3u, BlendFactor::SourceColor}, {4u, BlendFactor::InverseSourceColor},
        {5u, BlendFactor::SourceAlpha}, {6u, BlendFactor::InverseSourceAlpha},
        {7u, BlendFactor::DestinationAlpha}, {8u, BlendFactor::InverseDestinationAlpha},
        {9u, BlendFactor::DestinationColor}, {10u, BlendFactor::InverseDestinationColor},
        {11u, BlendFactor::SourceAlphaSaturate}, {14u, BlendFactor::BlendFactor},
        {15u, BlendFactor::InverseBlendFactor},
    }};
    const std::array<std::pair<std::uint32_t, BlendOperation>, 5u> operations = {{
        {1u, BlendOperation::Add}, {2u, BlendOperation::Subtract},
        {3u, BlendOperation::ReverseSubtract}, {4u, BlendOperation::Min},
        {5u, BlendOperation::Max},
    }};
    const std::array<std::pair<std::uint32_t, CompareFunction>, 8u> comparisons = {{
        {1u, CompareFunction::Never}, {2u, CompareFunction::Less},
        {3u, CompareFunction::Equal}, {4u, CompareFunction::LessEqual},
        {5u, CompareFunction::Greater}, {6u, CompareFunction::NotEqual},
        {7u, CompareFunction::GreaterEqual}, {8u, CompareFunction::Always},
    }};
    const LogicWords logic = {{0x1bu, 0x00010002u, 0x00010002u, 0u, 1u, 0x00040005u, 0u}};
    const auto convert = [](const LogicWords& words) {
        return nn_unlit_material_state(make_material(words), make_context(), prior_state(), false).render_state;
    };
    for (const auto& factor : factors) {
        LogicWords words = logic;
        words[1u] = words[2u] = factor.first | (factor.first << 16u);
        const RenderStateDescription actual = convert(words);
        if (!check(actual.blend.source_color == factor.second &&
                       actual.blend.destination_color == factor.second &&
                       actual.blend.source_alpha == factor.second &&
                       actual.blend.destination_alpha == factor.second,
                   "active blend factor conversion mismatch")) {
            return false;
        }
    }
    for (const auto& operation : operations) {
        LogicWords words = logic;
        words[4u] = 0xffff0000u | operation.first;
        const RenderStateDescription actual = convert(words);
        if (!check(actual.blend.color_operation == operation.second &&
                       actual.blend.alpha_operation == operation.second,
                   "active blend operation conversion mismatch")) {
            return false;
        }
    }
    for (const auto& comparison : comparisons) {
        LogicWords words = logic;
        words[5u] = comparison.first | (comparison.first << 16u);
        const RenderStateDescription actual = convert(words);
        if (!check(actual.alpha_test.compare == comparison.second &&
                       actual.depth_stencil.depth_compare == comparison.second,
                   "active comparison conversion mismatch")) {
            return false;
        }
    }
    return true;
}

bool test_lit_model164_state_is_identical_for_supported_draw_selectors() {
    using namespace sonic4ep2::d3d9;

    const std::array<std::uint32_t, 4u> draw_selectors = {{0u, 0x10000u, 0x20000u, 0x30000u}};
    for (std::uint32_t draw_flags_low : draw_selectors) {
        NnMaterialData material = make_lit_material(model164_logic());
        NnMaterialProfileContext context = make_lit_context(draw_flags_low);
        RenderStateDescription prior = prior_state();
        const NnMaterialData original_material = material;
        const NnMaterialProfileContext original_context = context;
        const RenderStateDescription original_prior = prior;
        const RenderStateDescription expected = model164_expected_state(prior);

        const NnLitMaterialState actual = nn_lit_material_state(material, context, prior, true);
        if (!check(same_render_state(actual.render_state, expected), "model 164 lit material state mismatch") ||
            !check(actual.fog_enabled, "model 164 lit material state did not enable requested fog") ||
            !check(same_material(material, original_material), "model 164 lit material state modified material input") ||
            !check(same_context(context, original_context), "model 164 lit material state modified context input") ||
            !check(same_render_state(prior, original_prior), "model 164 lit material state modified prior state")) {
            return false;
        }
    }
    return true;
}

bool test_lit_model164_fog_and_prior_fields() {
    using namespace sonic4ep2::d3d9;

    const std::array<bool, 2u> fog_requests = {{false, true}};
    for (bool fog_requested : fog_requests) {
        NnMaterialData material = make_lit_material(model164_logic());
        NnMaterialProfileContext context = make_lit_context();
        RenderStateDescription prior = prior_state();
        const NnMaterialData original_material = material;
        const NnMaterialProfileContext original_context = context;
        const RenderStateDescription original_prior = prior;
        const RenderStateDescription expected = model164_expected_state(prior);

        const NnLitMaterialState actual = nn_lit_material_state(material, context, prior, fog_requested);
        if (!check(same_render_state(actual.render_state, expected), "lit material state changed unrelated prior fields") ||
            !check(actual.fog_enabled == fog_requested, "lit material state fog result mismatch") ||
            !check(
                actual.render_state.alpha_test.compare == prior.alpha_test.compare &&
                    actual.render_state.alpha_test.reference == prior.alpha_test.reference &&
                    actual.render_state.depth_stencil.stencil_enable == prior.depth_stencil.stencil_enable &&
                    actual.render_state.rasterizer.scissor_rectangle.left == prior.rasterizer.scissor_rectangle.left,
                "lit material state did not preserve untouched prior fields") ||
            !check(same_material(material, original_material), "lit fog material state modified material input") ||
            !check(same_context(context, original_context), "lit fog material state modified context input") ||
            !check(same_render_state(prior, original_prior), "lit fog material state modified prior state")) {
            return false;
        }
    }
    return true;
}

bool test_lit_state_reuses_unlit_translation_for_accepted_logic() {
    using namespace sonic4ep2::d3d9;

    const std::array<LogicWords, 2u> variants = {{
        LogicWords{{0x1bu, 0x00010002u, 0x00010002u, 0u, 1u, 0x00040005u, 0u}},
        LogicWords{{0x18u, 0x000d000cu, 0x000d000cu, 0x80112233u, 0x00060006u, 0x00070003u, 0xabcdef92u}},
    }};
    for (const LogicWords& logic : variants) {
        NnMaterialData unlit_material = make_material(logic);
        NnMaterialData lit_material = make_lit_material(logic);
        NnMaterialProfileContext unlit_context = make_context();
        NnMaterialProfileContext lit_context = make_lit_context();
        RenderStateDescription unlit_prior = prior_state();
        RenderStateDescription lit_prior = prior_state();
        const NnMaterialData original_unlit_material = unlit_material;
        const NnMaterialData original_lit_material = lit_material;
        const NnMaterialProfileContext original_unlit_context = unlit_context;
        const NnMaterialProfileContext original_lit_context = lit_context;
        const RenderStateDescription original_unlit_prior = unlit_prior;
        const RenderStateDescription original_lit_prior = lit_prior;

        const NnUnlitMaterialState unlit = nn_unlit_material_state(
            unlit_material,
            unlit_context,
            unlit_prior,
            true);
        const NnLitMaterialState lit = nn_lit_material_state(lit_material, lit_context, lit_prior, true);
        if (!check(same_render_state(lit.render_state, unlit.render_state), "lit state diverged from unlit translation") ||
            !check(lit.fog_enabled == unlit.fog_enabled, "lit state fog diverged from unlit translation") ||
            !check(same_material(unlit_material, original_unlit_material), "unlit comparison modified material input") ||
            !check(same_material(lit_material, original_lit_material), "lit comparison modified material input") ||
            !check(same_context(unlit_context, original_unlit_context), "unlit comparison modified context input") ||
            !check(same_context(lit_context, original_lit_context), "lit comparison modified context input") ||
            !check(same_render_state(unlit_prior, original_unlit_prior), "unlit comparison modified prior state") ||
            !check(same_render_state(lit_prior, original_lit_prior), "lit comparison modified prior state")) {
            return false;
        }
    }
    return true;
}

bool test_lit_weighted_transform_modes_match_static_suppression() {
    using namespace sonic4ep2::d3d9;

    const LogicWords logic = {{
        0x18u,
        0x000d000cu,
        0x000d000cu,
        0x80112233u,
        0x00060006u,
        0x00070003u,
        0xabcdef92u,
    }};
    NnMaterialData material = make_lit_material(logic);
    NnMaterialProfileContext static_context = make_lit_context(0x20000u);
    static_context.subobject_flags = 0x100u;
    static_context.vertex_format = 0x17003u;
    static_context.draw_flags_high = 8u;
    NnMaterialProfileContext weighted_mode_one_context = static_context;
    weighted_mode_one_context.subobject_flags = 0x201u;
    NnMaterialProfileContext weighted_mode_two_context = weighted_mode_one_context;
    weighted_mode_two_context.vertex_format = 0x17403u;
    RenderStateDescription static_prior = prior_state();
    RenderStateDescription weighted_mode_one_prior = prior_state();
    RenderStateDescription weighted_mode_two_prior = prior_state();
    const NnMaterialData original_material = material;
    const NnMaterialProfileContext original_static_context = static_context;
    const NnMaterialProfileContext original_weighted_mode_one_context = weighted_mode_one_context;
    const NnMaterialProfileContext original_weighted_mode_two_context = weighted_mode_two_context;
    const RenderStateDescription original_static_prior = static_prior;
    const RenderStateDescription original_weighted_mode_one_prior = weighted_mode_one_prior;
    const RenderStateDescription original_weighted_mode_two_prior = weighted_mode_two_prior;

    if (!check(
            (weighted_mode_one_context.subobject_flags & 0x100u) == 0u,
            "weighted state fixture includes the static flag")) {
        return false;
    }

    const NnLitMaterialState static_result = nn_lit_material_state(material, static_context, static_prior, true);
    const NnLitMaterialState weighted_mode_one_result = nn_lit_material_state(
        material,
        weighted_mode_one_context,
        weighted_mode_one_prior,
        true);
    const NnLitMaterialState weighted_mode_two_result = nn_lit_material_state(
        material,
        weighted_mode_two_context,
        weighted_mode_two_prior,
        true);
    return check(
               same_render_state(static_result.render_state, weighted_mode_one_result.render_state),
               "mode-one weighted state differs from static suppression") &&
           check(
               same_render_state(static_result.render_state, weighted_mode_two_result.render_state),
               "mode-two weighted state differs from static suppression") &&
           check(
               static_result.fog_enabled == weighted_mode_one_result.fog_enabled &&
                   static_result.fog_enabled == weighted_mode_two_result.fog_enabled,
               "weighted state fog result differs from static suppression") &&
           check(same_material(material, original_material), "weighted state modified material input") &&
           check(same_context(static_context, original_static_context), "weighted state modified static context") &&
           check(
               same_context(weighted_mode_one_context, original_weighted_mode_one_context),
               "weighted state modified mode-one context") &&
           check(
               same_context(weighted_mode_two_context, original_weighted_mode_two_context),
               "weighted state modified mode-two context") &&
           check(same_render_state(static_prior, original_static_prior), "weighted state modified static prior state") &&
           check(
               same_render_state(weighted_mode_one_prior, original_weighted_mode_one_prior),
               "weighted state modified mode-one prior state") &&
           check(
               same_render_state(weighted_mode_two_prior, original_weighted_mode_two_prior),
               "weighted state modified mode-two prior state");
}

template <typename Mutate>
bool rejects_invalid_input(Mutate&& mutate, const char* message, const char* expected_diagnostic = nullptr) {
    const LogicWords logic = {{
        0x1u,
        0x00010002u,
        0x00010002u,
        0x10203040u,
        0x00010001u,
        0x00010001u,
        0u,
    }};
    NnMaterialData material = make_material(logic);
    NnMaterialProfileContext context = make_context();
    sonic4ep2::d3d9::RenderStateDescription prior = prior_state();
    mutate(material, context, prior);
    const NnMaterialData original_material = material;
    const NnMaterialProfileContext original_context = context;
    const sonic4ep2::d3d9::RenderStateDescription original_prior = prior;

    try {
        static_cast<void>(sonic4ep2::d3d9::nn_unlit_material_state(material, context, prior, true));
    } catch (const std::invalid_argument& error) {
        return check(
                   expected_diagnostic == nullptr || std::strcmp(error.what(), expected_diagnostic) == 0,
                   message) &&
               check(same_material(material, original_material), "rejected material state modified material input") &&
               check(same_context(context, original_context), "rejected material state modified context input") &&
               check(same_render_state(prior, original_prior), "rejected material state modified prior render state");
    } catch (const std::exception&) {
        return check(false, message);
    }
    return check(false, message);
}

template <typename Mutate>
bool rejects_invalid_lit_input(Mutate&& mutate, const char* message, const char* expected_diagnostic = nullptr) {
    NnMaterialData material = make_lit_material(model164_logic());
    NnMaterialProfileContext context = make_lit_context();
    sonic4ep2::d3d9::RenderStateDescription prior = prior_state();
    mutate(material, context, prior);
    const NnMaterialData original_material = material;
    const NnMaterialProfileContext original_context = context;
    const sonic4ep2::d3d9::RenderStateDescription original_prior = prior;

    try {
        static_cast<void>(sonic4ep2::d3d9::nn_lit_material_state(material, context, prior, true));
    } catch (const std::invalid_argument& error) {
        return check(
                   expected_diagnostic == nullptr || std::strcmp(error.what(), expected_diagnostic) == 0,
                   message) &&
               check(same_material(material, original_material), "rejected lit material state modified material input") &&
               check(same_context(context, original_context), "rejected lit material state modified context input") &&
               check(same_render_state(prior, original_prior), "rejected lit material state modified prior state");
    } catch (const std::exception&) {
        return check(false, message);
    }
    return check(false, message);
}

bool test_rejects_unsupported_or_used_invalid_state_inputs() {
    return rejects_invalid_input(
               [](NnMaterialData&, NnMaterialProfileContext& context, sonic4ep2::d3d9::RenderStateDescription&) {
                   context.draw_flags_low = 0x400u;
               },
               "nonzero low draw flags were accepted") &&
           rejects_invalid_input(
               [](NnMaterialData&, NnMaterialProfileContext& context, sonic4ep2::d3d9::RenderStateDescription&) {
                   context.draw_flags_high = 1u;
               },
               "nonzero high draw flags were accepted") &&
           rejects_invalid_input(
               [](NnMaterialData&, NnMaterialProfileContext& context, sonic4ep2::d3d9::RenderStateDescription&) {
                   context.vertex_format = 0x7000u;
               },
               "transformed material profile was accepted") &&
           rejects_invalid_input(
               [](NnMaterialData&, NnMaterialProfileContext& context, sonic4ep2::d3d9::RenderStateDescription&) {
                   context.texture_stage_limit = 0u;
               },
               "base-map-disabled context was accepted") &&
           rejects_invalid_input(
               [](NnMaterialData& material, NnMaterialProfileContext&, sonic4ep2::d3d9::RenderStateDescription&) {
                   material.stages.clear();
                   material.descriptor_bits[5u] = 0u;
               },
               "missing base-map stage was accepted") &&
           rejects_invalid_input(
               [](NnMaterialData& material, NnMaterialProfileContext&, sonic4ep2::d3d9::RenderStateDescription&) {
                   material.descriptor_bits[0u] = 0x2002u;
               },
               "unsupported descriptor flags were accepted") &&
           rejects_invalid_input(
               [](NnMaterialData& material, NnMaterialProfileContext&, sonic4ep2::d3d9::RenderStateDescription&) {
                   material.descriptor_bits[0u] = 0u;
               },
               "descriptor without unlit bit was accepted") &&
           rejects_invalid_input(
               [](NnMaterialData& material, NnMaterialProfileContext&, sonic4ep2::d3d9::RenderStateDescription&) {
                   material.render_state.reset();
               },
               "missing material logic was accepted") &&
           rejects_invalid_input(
               [](NnMaterialData& material, NnMaterialProfileContext&, sonic4ep2::d3d9::RenderStateDescription&) {
                   material.render_state->words[0u] = 0x20u;
               },
               "unsupported logic flags were accepted") &&
           rejects_invalid_input(
               [](NnMaterialData& material, NnMaterialProfileContext&, sonic4ep2::d3d9::RenderStateDescription&) {
                   material.render_state->words[1u] = 0x0001000cu;
               },
               "used blend factor twelve was accepted") &&
           rejects_invalid_input(
               [](NnMaterialData& material, NnMaterialProfileContext&, sonic4ep2::d3d9::RenderStateDescription&) {
                   material.render_state->words[1u] = 0x000d0002u;
               },
               "used blend factor thirteen was accepted") &&
           rejects_invalid_input(
               [](NnMaterialData& material, NnMaterialProfileContext&, sonic4ep2::d3d9::RenderStateDescription&) {
                   material.render_state->words[4u] = 0x00010006u;
               },
               "used blend operation was accepted") &&
           rejects_invalid_input(
               [](NnMaterialData& material, NnMaterialProfileContext&, sonic4ep2::d3d9::RenderStateDescription&) {
                   material.render_state->words[0u] = 0x3u;
                   material.render_state->words[2u] = 0x0001000cu;
               },
               "used separate-alpha blend factor was accepted") &&
           rejects_invalid_input(
               [](NnMaterialData& material, NnMaterialProfileContext&, sonic4ep2::d3d9::RenderStateDescription&) {
                   material.render_state->words[0u] = 0x8u;
                   material.render_state->words[5u] = 0x00000009u;
               },
               "used alpha-test comparison was accepted") &&
           rejects_invalid_input(
               [](NnMaterialData& material, NnMaterialProfileContext&, sonic4ep2::d3d9::RenderStateDescription&) {
                   material.render_state->words[0u] = 0x10u;
                   material.render_state->words[5u] = 0x00090000u;
               },
               "used depth comparison was accepted");
}

bool test_unlit_active_enum_diagnostics_are_preserved() {
    return rejects_invalid_input(
               [](NnMaterialData& material, NnMaterialProfileContext&, sonic4ep2::d3d9::RenderStateDescription&) {
                   material.render_state->words[1u] = 0x0001000cu;
               },
               "unlit blend factor diagnostic changed",
               "Unlit material state blend factor is unsupported.") &&
           rejects_invalid_input(
               [](NnMaterialData& material, NnMaterialProfileContext&, sonic4ep2::d3d9::RenderStateDescription&) {
                   material.render_state->words[4u] = 0x00010006u;
               },
               "unlit blend operation diagnostic changed",
               "Unlit material state blend operation is unsupported.") &&
           rejects_invalid_input(
               [](NnMaterialData& material, NnMaterialProfileContext&, sonic4ep2::d3d9::RenderStateDescription&) {
                   material.render_state->words[0u] = 0x8u;
                   material.render_state->words[5u] = 0x00000009u;
               },
               "unlit comparison diagnostic changed",
               "Unlit material state comparison is unsupported.");
}

bool test_lit_rejects_unsupported_or_invalid_state_inputs() {
    return rejects_invalid_lit_input(
               [](NnMaterialData& material, NnMaterialProfileContext&, sonic4ep2::d3d9::RenderStateDescription&) {
                   material.pointer_flags = kPointerFlags;
               },
               "unlit material pointer was accepted by lit state") &&
           rejects_invalid_lit_input(
               [](NnMaterialData& material, NnMaterialProfileContext&, sonic4ep2::d3d9::RenderStateDescription&) {
                   material.pointer_flags = 0x30000001u;
               },
               "unsupported lit material pointer was accepted") &&
           rejects_invalid_lit_input(
               [](NnMaterialData& material, NnMaterialProfileContext&, sonic4ep2::d3d9::RenderStateDescription&) {
                   material.render_state.reset();
               },
               "lit material state accepted missing logic") &&
           rejects_invalid_lit_input(
               [](NnMaterialData& material, NnMaterialProfileContext&, sonic4ep2::d3d9::RenderStateDescription&) {
                   material.render_state->words[0u] = 0x20u;
               },
               "lit material state accepted unsupported logic flags") &&
           rejects_invalid_lit_input(
               [](NnMaterialData& material, NnMaterialProfileContext&, sonic4ep2::d3d9::RenderStateDescription&) {
                   material.stages[0u].raw_words.resize(15u);
               },
               "lit material state accepted malformed stage metadata") &&
           rejects_invalid_lit_input(
               [](NnMaterialData&, NnMaterialProfileContext& context, sonic4ep2::d3d9::RenderStateDescription&) {
                   context.draw_flags_low = 0x400u;
               },
               "lit material state accepted unsupported low draw flags") &&
           rejects_invalid_lit_input(
               [](NnMaterialData&, NnMaterialProfileContext& context, sonic4ep2::d3d9::RenderStateDescription&) {
                   context.draw_flags_high = 1u;
               },
               "lit material state accepted unsupported high draw flags") &&
           rejects_invalid_lit_input(
               [](NnMaterialData&, NnMaterialProfileContext& context, sonic4ep2::d3d9::RenderStateDescription&) {
                   context.texture_stage_limit = 0u;
               },
               "lit material state accepted an inactive base stage") &&
           rejects_invalid_lit_input(
               [](NnMaterialData&, NnMaterialProfileContext& context, sonic4ep2::d3d9::RenderStateDescription&) {
                   context.texture_stage_limit = 9u;
               },
               "lit material state accepted an out-of-range stage limit") &&
           rejects_invalid_lit_input(
               [](NnMaterialData& material, NnMaterialProfileContext&, sonic4ep2::d3d9::RenderStateDescription&) {
                   material.stages.clear();
                   material.descriptor_bits[5u] = 0u;
               },
               "lit material state accepted no base stage") &&
           rejects_invalid_lit_input(
               [](NnMaterialData& material, NnMaterialProfileContext&, sonic4ep2::d3d9::RenderStateDescription&) {
                   material.stages.push_back(make_stage());
                   material.descriptor_bits[5u] = 2u;
               },
               "lit material state accepted more than one base stage") &&
           rejects_invalid_lit_input(
               [](NnMaterialData& material, NnMaterialProfileContext&, sonic4ep2::d3d9::RenderStateDescription&) {
                   material.descriptor_bits[4u] = 0u;
               },
               "lit material state accepted a missing stage descriptor mask") &&
           rejects_invalid_lit_input(
               [](NnMaterialData& material, NnMaterialProfileContext&, sonic4ep2::d3d9::RenderStateDescription&) {
                   material.descriptor_bits[4u] = 0x3u;
               },
               "lit material state accepted a nonexact stage descriptor mask") &&
           rejects_invalid_lit_input(
               [](NnMaterialData& material, NnMaterialProfileContext&, sonic4ep2::d3d9::RenderStateDescription&) {
                   material.descriptor_bits[0u] = 1u;
               },
               "lit material state accepted unsupported descriptor flags") &&
           rejects_invalid_lit_input(
               [](NnMaterialData& material, NnMaterialProfileContext&, sonic4ep2::d3d9::RenderStateDescription&) {
                   material.user_profile.reset();
               },
               "lit material state accepted a missing user profile") &&
           rejects_invalid_lit_input(
               [](NnMaterialData& material, NnMaterialProfileContext&, sonic4ep2::d3d9::RenderStateDescription&) {
                   material.user_profile = 64u;
               },
               "lit material state accepted an out-of-range user profile");
}

bool test_lit_active_enum_diagnostics_are_lit_scoped() {
    return rejects_invalid_lit_input(
               [](NnMaterialData& material, NnMaterialProfileContext&, sonic4ep2::d3d9::RenderStateDescription&) {
                   material.render_state->words[0u] = 0x1u;
                   material.render_state->words[1u] = 0x0001000cu;
               },
               "lit blend factor diagnostic was not lit-scoped",
               "Lit material state blend factor is unsupported.") &&
           rejects_invalid_lit_input(
               [](NnMaterialData& material, NnMaterialProfileContext&, sonic4ep2::d3d9::RenderStateDescription&) {
                   material.render_state->words[0u] = 0x1u;
                   material.render_state->words[1u] = 0x00010002u;
                   material.render_state->words[4u] = 0x00010006u;
               },
               "lit blend operation diagnostic was not lit-scoped",
               "Lit material state blend operation is unsupported.") &&
           rejects_invalid_lit_input(
               [](NnMaterialData& material, NnMaterialProfileContext&, sonic4ep2::d3d9::RenderStateDescription&) {
                   material.render_state->words[0u] = 0x8u;
                   material.render_state->words[5u] = 0x00000009u;
               },
               "lit alpha comparison diagnostic was not lit-scoped",
               "Lit material state comparison is unsupported.") &&
           rejects_invalid_lit_input(
               [](NnMaterialData& material, NnMaterialProfileContext&, sonic4ep2::d3d9::RenderStateDescription&) {
                   material.render_state->words[0u] = 0x10u;
                   material.render_state->words[5u] = 0x00090000u;
               },
               "lit depth comparison diagnostic was not lit-scoped",
               "Lit material state comparison is unsupported.");
}

}

int main() {
    return test_selected_state_preserves_all_unassigned_prior_fields() &&
                   test_source_alpha_and_separate_alpha_share_the_color_operation() &&
                   test_blend_disabled_ignores_unused_enums_and_applies_alpha_depth() &&
                   test_supported_state_enums_in_active_fields() &&
                   test_rejects_unsupported_or_used_invalid_state_inputs() &&
                   test_unlit_active_enum_diagnostics_are_preserved() &&
                   test_lit_model164_state_is_identical_for_supported_draw_selectors() &&
                   test_lit_model164_fog_and_prior_fields() &&
                   test_lit_state_reuses_unlit_translation_for_accepted_logic() &&
                   test_lit_weighted_transform_modes_match_static_suppression() &&
                   test_lit_rejects_unsupported_or_invalid_state_inputs() &&
                   test_lit_active_enum_diagnostics_are_lit_scoped()
               ? 0
               : 1;
}
