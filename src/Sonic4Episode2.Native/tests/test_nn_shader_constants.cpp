#include "nn_shader_constants.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <exception>
#include <stdexcept>
#include <utility>
#include <vector>

namespace {

constexpr std::uint32_t kStageFlags = 0x60000002u;
constexpr std::uint32_t kMaterialPointerFlags = 0x10000000u;
constexpr std::uint32_t kMaterialDescriptorUnlitBit = 0x2u;
constexpr std::uint32_t kMaterialStageMask = 0x2u;

using MatrixBits = std::array<std::uint32_t, 16u>;
using Float4Bits = std::array<std::uint32_t, 4u>;

bool check(bool condition, const char* message) {
    if (condition) {
        return true;
    }
    std::fprintf(stderr, "%s\n", message);
    return false;
}

std::uint32_t float_bits(const float& value) {
    static_assert(sizeof(value) == sizeof(std::uint32_t));
    std::uint32_t bits = 0u;
    std::memcpy(&bits, &value, sizeof(bits));
    return bits;
}

NnShaderMatrix matrix_from_bits(const MatrixBits& bits) {
    NnShaderMatrix matrix{};
    for (std::size_t index = 0u; index < matrix.size(); ++index) {
        std::memcpy(&matrix[index], &bits[index], sizeof(bits[index]));
    }
    return matrix;
}

MatrixBits matrix_bits(const NnShaderMatrix& matrix) {
    MatrixBits bits{};
    for (std::size_t index = 0u; index < matrix.size(); ++index) {
        bits[index] = float_bits(matrix[index]);
    }
    return bits;
}

class IntegerMatrixBackend final : public CameraMatrixBackend {
public:
    mutable std::vector<CameraMatrix> left_inputs;
    mutable std::vector<CameraMatrix> right_inputs;
    mutable std::vector<CameraPrecision> precisions;

    CameraMatrix rotation_z(float, CameraPrecision) const override {
        throw std::logic_error("unexpected rotation request");
    }

    CameraMatrix multiply(
        const CameraMatrix& left,
        const CameraMatrix& right,
        CameraPrecision precision) const override {
        left_inputs.push_back(left);
        right_inputs.push_back(right);
        precisions.push_back(precision);
        CameraMatrix result{};
        for (std::size_t row = 0u; row < 4u; ++row) {
            for (std::size_t column = 0u; column < 4u; ++column) {
                std::int32_t value = 0;
                for (std::size_t index = 0u; index < 4u; ++index) {
                    value += static_cast<std::int32_t>(left[row * 4u + index]) *
                             static_cast<std::int32_t>(right[index * 4u + column]);
                }
                result[row * 4u + column] = static_cast<float>(value);
            }
        }
        return result;
    }

    CameraMatrix perspective_fov_rh(const CameraProjection&, CameraPrecision) const override {
        throw std::logic_error("unexpected projection request");
    }
};

NnShaderMatrix model_fixture() {
    return {{
        2.0f, 0.0f, 0.0f, 0.0f,
        0.0f, 1.0f, 0.0f, 0.0f,
        0.0f, 0.0f, 1.0f, 0.0f,
        3.0f, 0.0f, 0.0f, 1.0f,
    }};
}

NnShaderMatrix view_fixture() {
    return {{
        1.0f, 0.0f, 0.0f, 0.0f,
        0.0f, 1.0f, 0.0f, 0.0f,
        0.0f, 0.0f, 1.0f, 0.0f,
        4.0f, 0.0f, 0.0f, 1.0f,
    }};
}

NnShaderMatrix projection_fixture() {
    return {{
        1.0f, 0.0f, 0.0f, 0.0f,
        0.0f, 3.0f, 0.0f, 0.0f,
        0.0f, 0.0f, 4.0f, 0.0f,
        0.0f, 0.0f, 5.0f, 1.0f,
    }};
}

bool same_stage(const NnMaterialStageData& left, const NnMaterialStageData& right) {
    return left.flags == right.flags && left.texture_index == right.texture_index && left.raw_words == right.raw_words;
}

bool same_material(const NnMaterialData& left, const NnMaterialData& right) {
    if (left.pointer_flags != right.pointer_flags || left.descriptor_bits != right.descriptor_bits ||
        left.color_terms_bits != right.color_terms_bits || left.color_flags != right.color_flags ||
        left.shininess_bits != right.shininess_bits ||
        left.specular_intensity_bits != right.specular_intensity_bits || left.stages.size() != right.stages.size() ||
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

NnMaterialStageData make_stage(
    std::uint32_t scale_u_bits = 0x3f800000u,
    std::uint32_t scale_v_bits = 0x3f800000u,
    std::int32_t coordinate = 0) {
    NnMaterialStageData stage{};
    stage.flags = kStageFlags;
    stage.texture_index = 7;
    stage.raw_words.resize(16u);
    stage.raw_words[0u] = stage.flags;
    stage.raw_words[1u] = static_cast<std::uint32_t>(stage.texture_index);
    stage.raw_words[2u] = static_cast<std::uint32_t>(coordinate);
    stage.raw_words[3u] = 0x3f800000u;
    stage.raw_words[6u] = scale_u_bits;
    stage.raw_words[7u] = scale_v_bits;
    stage.raw_words[8u] = 0x00010001u;
    return stage;
}

NnMaterialData make_material() {
    NnMaterialData material{};
    material.pointer_flags = kMaterialPointerFlags;
    material.descriptor_bits[0u] = kMaterialDescriptorUnlitBit;
    material.descriptor_bits[4u] = kMaterialStageMask;
    material.descriptor_bits[5u] = 1u;
    material.color_terms_bits = std::vector<std::array<std::uint32_t, 4u>>{
        {{0x7FC00001u, 0x7F800000u, 0xFF800000u, 0x80000000u}},
        {{0x3E99999Au, 0xBF000000u, 0x00000001u, 0x80000000u}},
        {{0x7FC00002u, 0x7F800001u, 0xFF800001u, 0x00000000u}},
        {{0x7FC00003u, 0x7F800002u, 0xFF800002u, 0x00000001u}},
    };
    material.color_flags = 0x80000002u;
    material.shininess_bits = 0x7FC00004u;
    material.specular_intensity_bits = 0xFF800000u;
    material.stages.push_back(make_stage());
    return material;
}

bool check_material_constant_bits(
    const NnUnlitMaterialConstants& actual,
    const Float4Bits& diffuse,
    std::uint32_t base_alpha_bits,
    const char* message) {
    for (std::size_t index = 0u; index < diffuse.size(); ++index) {
        if (!check(float_bits(actual.diffuse[index]) == diffuse[index], message)) {
            return false;
        }
    }
    return check(float_bits(actual.base_alpha) == base_alpha_bits, message);
}

bool check_matrix_bits(
    const NnShaderMatrix& actual,
    const MatrixBits& expected,
    const char* message) {
    return check(matrix_bits(actual) == expected, message);
}

bool test_builds_selected_unlit_matrix_without_mutating_inputs() {
    NnMaterialStageData stage = make_stage(0x40000000u, 0x40400000u, 3);
    NnMaterialProfileContext context{};
    context.texture_stage_limit = 1u;
    const NnMaterialStageData original_stage = stage;
    const NnMaterialProfileContext original_context = context;
    const MatrixBits expected = {{
        0x40000000u, 0x00000000u, 0x00000000u, 0x00000000u,
        0x00000000u, 0x40400000u, 0x00000000u, 0x00000000u,
        0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u,
        0x00000000u, 0x00000000u, 0x00000000u, 0x3f800000u,
    }};

    const NnShaderMatrix actual = nn_unlit_texture_matrix(stage, context);
    return check_matrix_bits(actual, expected, "selected unlit texture matrix bits mismatch") &&
           check(same_stage(stage, original_stage), "selected unlit texture matrix modified stage input") &&
           check(same_context(context, original_context), "selected unlit texture matrix modified context input");
}

bool test_preserves_selected_material_constant_bits_without_mutating_inputs() {
    const Float4Bits diffuse = {{0x3E99999Au, 0xBF000000u, 0x00000001u, 0x80000000u}};
    const std::array<std::uint32_t, 6u> base_alpha_bits = {{
        0x00000000u,
        0x80000000u,
        0x3E800000u,
        0x3F800000u,
        0x40000000u,
        0xBF800000u,
    }};
    const std::array<std::uint32_t, 3u> color_flags = {{0u, 2u, 0x80000002u}};
    for (std::uint32_t color_flag : color_flags) {
        for (std::uint32_t base_alpha : base_alpha_bits) {
            NnMaterialData material = make_material();
            material.color_terms_bits[1u] = diffuse;
            material.color_flags = color_flag;
            material.stages[0u].raw_words[3u] = base_alpha;
            NnMaterialProfileContext context{};
            context.texture_stage_limit = 1u;
            const NnMaterialData original_material = material;
            const NnMaterialProfileContext original_context = context;
            const NnUnlitMaterialConstants actual = nn_unlit_material_constants(material, context);
            if (!check_material_constant_bits(
                    actual,
                    diffuse,
                    base_alpha,
                    "unlit material constants changed finite source bits") ||
                !check(same_material(material, original_material), "unlit material constants modified material input") ||
                !check(same_context(context, original_context), "unlit material constants modified context input")) {
                return false;
            }
        }
    }
    return true;
}

bool test_preserves_signed_zero_and_subnormal_scale_bits() {
    NnMaterialProfileContext context{};
    context.texture_stage_limit = 8u;
    const MatrixBits signed_zero_expected = {{
        0x80000000u, 0x80000000u, 0x80000000u, 0x00000000u,
        0x00000000u, 0x00000001u, 0x00000000u, 0x00000000u,
        0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u,
        0x00000000u, 0x00000000u, 0x00000000u, 0x3f800000u,
    }};
    const MatrixBits signed_subnormal_expected = {{
        0x00000001u, 0x00000000u, 0x00000000u, 0x00000000u,
        0x80000000u, 0x80000001u, 0x80000000u, 0x00000000u,
        0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u,
        0x00000000u, 0x00000000u, 0x00000000u, 0x3f800000u,
    }};
    const NnMaterialStageData signed_zero_stage = make_stage(0x80000000u, 0x00000001u, -3);
    const NnMaterialStageData signed_subnormal_stage = make_stage(0x00000001u, 0x80000001u, 0);

    return check_matrix_bits(
               nn_unlit_texture_matrix(signed_zero_stage, context),
               signed_zero_expected,
               "signed-zero or positive-subnormal scale bits changed") &&
           check_matrix_bits(
               nn_unlit_texture_matrix(signed_subnormal_stage, context),
               signed_subnormal_expected,
               "signed-subnormal scale or signed zero-product bits changed");
}

bool test_uses_context_domain_and_ignores_offsets() {
    const MatrixBits expected = {{
        0x3f800000u, 0x00000000u, 0x00000000u, 0x00000000u,
        0x00000000u, 0x3f800000u, 0x00000000u, 0x00000000u,
        0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u,
        0x00000000u, 0x00000000u, 0x00000000u, 0x3f800000u,
    }};
    const std::array<std::uint32_t, 3u> draw_flags = {{0u, 0x400u, 0x800u}};
    const std::array<std::int32_t, 5u> coordinates = {{-3, 0, 1, 2, 3}};
    NnMaterialStageData stage = make_stage();
    stage.raw_words[4u] = 0x7fc00001u;
    stage.raw_words[5u] = 0xff800000u;
    const NnMaterialStageData original_stage = stage;

    for (std::uint32_t draw_flags_low : draw_flags) {
        for (std::int32_t coordinate : coordinates) {
            stage.raw_words[2u] = static_cast<std::uint32_t>(coordinate);
            const NnMaterialStageData stage_before = stage;
            NnMaterialProfileContext context{};
            context.draw_flags_low = draw_flags_low;
            context.texture_stage_limit = coordinate == -3 ? 8u : 1u;
            const NnMaterialProfileContext context_before = context;
            if (!check_matrix_bits(
                    nn_unlit_texture_matrix(stage, context),
                    expected,
                    "supported texture context did not retain the selected matrix") ||
                !check(same_stage(stage, stage_before), "supported texture context modified stage input") ||
                !check(same_context(context, context_before), "supported texture context modified context input")) {
                return false;
            }
        }
    }
    return check(
        original_stage.raw_words[4u] == 0x7fc00001u && original_stage.raw_words[5u] == 0xff800000u,
        "offset fixture lost its distinct source words");
}

bool test_packs_generic_matrix_columns_by_bits() {
    const MatrixBits source_bits = {{
        0x3f800000u, 0x80000000u, 0x00000001u, 0x40400000u,
        0xbf800000u, 0x3f000000u, 0x80000001u, 0x7fc01234u,
        0x00800000u, 0x7f7fffffu, 0x00000000u, 0x3f800000u,
        0x3f4ccccdu, 0xbf000000u, 0x3e800000u, 0xc0000000u,
    }};
    const MatrixBits expected_bits = {{
        0x3f800000u, 0xbf800000u, 0x00800000u, 0x3f4ccccdu,
        0x80000000u, 0x3f000000u, 0x7f7fffffu, 0xbf000000u,
        0x00000001u, 0x80000001u, 0x00000000u, 0x3e800000u,
        0x40400000u, 0x7fc01234u, 0x3f800000u, 0xc0000000u,
    }};
    const NnShaderMatrix source = matrix_from_bits(source_bits);
    const NnShaderMatrix packed = nn_shader_matrix_columns(source);
    return check_matrix_bits(packed, expected_bits, "matrix column packing changed float word bits") &&
           check_matrix_bits(source, source_bits, "matrix column packing modified its input");
}

bool test_builds_noncommutative_model_matrices_through_backend() {
    const NnShaderMatrix model = model_fixture();
    const NnShaderMatrix view = view_fixture();
    const NnShaderMatrix projection = projection_fixture();
    const NnShaderMatrix expected_model_view = {{
        2.0f, 0.0f, 0.0f, 0.0f,
        0.0f, 1.0f, 0.0f, 0.0f,
        0.0f, 0.0f, 1.0f, 0.0f,
        7.0f, 0.0f, 0.0f, 1.0f,
    }};
    const NnShaderMatrix expected_model_view_projection = {{
        2.0f, 0.0f, 0.0f, 0.0f,
        0.0f, 3.0f, 0.0f, 0.0f,
        0.0f, 0.0f, 4.0f, 0.0f,
        7.0f, 0.0f, 5.0f, 1.0f,
    }};
    IntegerMatrixBackend backend;
    const NnModelMatrixConstants single = nn_unlit_model_matrices(
        model,
        view,
        projection,
        CameraPrecision::Single,
        backend);

    if (!check(single.model_view == expected_model_view, "model-view operand order mismatch") ||
        !check(
            single.model_view_projection == expected_model_view_projection,
            "model-view-projection operand order mismatch") ||
        !check(
            backend.left_inputs == std::vector<CameraMatrix>{model, expected_model_view} &&
                backend.right_inputs == std::vector<CameraMatrix>{view, projection} &&
                backend.precisions == std::vector<CameraPrecision>{CameraPrecision::Single, CameraPrecision::Single},
            "model matrix backend calls or single precision forwarding mismatch") ||
        !check(
            matrix_bits(model) == matrix_bits(model_fixture()) &&
                matrix_bits(view) == matrix_bits(view_fixture()) &&
                matrix_bits(projection) == matrix_bits(projection_fixture()),
            "model matrix construction modified an input")) {
        return false;
    }

    backend.left_inputs.clear();
    backend.right_inputs.clear();
    backend.precisions.clear();
    const NnModelMatrixConstants extended = nn_unlit_model_matrices(
        model,
        view,
        projection,
        CameraPrecision::Double,
        backend);
    return check(extended.model_view == expected_model_view, "double precision model-view mismatch") &&
           check(
               extended.model_view_projection == expected_model_view_projection,
               "double precision model-view-projection mismatch") &&
           check(
               backend.precisions == std::vector<CameraPrecision>{CameraPrecision::Double, CameraPrecision::Double},
               "double precision was not forwarded to every model matrix multiply");
}

template <typename Mutate>
bool rejects_invalid_model_input(Mutate&& mutate, const char* message) {
    NnShaderMatrix model = model_fixture();
    NnShaderMatrix view = view_fixture();
    NnShaderMatrix projection = projection_fixture();
    CameraPrecision precision = CameraPrecision::Single;
    mutate(model, view, projection, precision);
    const MatrixBits original_model = matrix_bits(model);
    const MatrixBits original_view = matrix_bits(view);
    const MatrixBits original_projection = matrix_bits(projection);
    IntegerMatrixBackend backend;
    try {
        static_cast<void>(nn_unlit_model_matrices(model, view, projection, precision, backend));
    } catch (const std::invalid_argument&) {
        return check(
                   backend.left_inputs.empty() && backend.right_inputs.empty() && backend.precisions.empty(),
                   "invalid model matrices invoked the backend") &&
               check(matrix_bits(model) == original_model, "rejected model matrix was modified") &&
               check(matrix_bits(view) == original_view, "rejected view matrix was modified") &&
               check(matrix_bits(projection) == original_projection, "rejected projection matrix was modified");
    } catch (const std::exception&) {
        return check(false, message);
    }
    return check(false, message);
}

bool test_rejects_invalid_model_matrix_domain_before_backend() {
    return rejects_invalid_model_input(
               [](NnShaderMatrix& model, NnShaderMatrix&, NnShaderMatrix&, CameraPrecision&) {
                   const std::uint32_t nan = 0x7fc00000u;
                   std::memcpy(&model[0u], &nan, sizeof(nan));
               },
               "NaN model matrix word was accepted") &&
           rejects_invalid_model_input(
               [](NnShaderMatrix&, NnShaderMatrix& view, NnShaderMatrix&, CameraPrecision&) {
                   const std::uint32_t infinity = 0x7f800000u;
                   std::memcpy(&view[5u], &infinity, sizeof(infinity));
               },
               "infinite view matrix word was accepted") &&
           rejects_invalid_model_input(
               [](NnShaderMatrix&, NnShaderMatrix&, NnShaderMatrix& projection, CameraPrecision&) {
                   const std::uint32_t infinity = 0xff800000u;
                   std::memcpy(&projection[10u], &infinity, sizeof(infinity));
               },
               "infinite projection matrix word was accepted") &&
           rejects_invalid_model_input(
               [](NnShaderMatrix&, NnShaderMatrix&, NnShaderMatrix&, CameraPrecision& precision) {
                   precision = static_cast<CameraPrecision>(99);
               },
               "unsupported model matrix precision was accepted");
}

template <typename Mutate>
bool rejects_invalid_material_constant_input(Mutate&& mutate, const char* message) {
    NnMaterialData material = make_material();
    NnMaterialProfileContext context{};
    context.texture_stage_limit = 1u;
    mutate(material, context);
    const NnMaterialData original_material = material;
    const NnMaterialProfileContext original_context = context;
    try {
        static_cast<void>(nn_unlit_material_constants(material, context));
    } catch (const std::invalid_argument&) {
        return check(same_material(material, original_material), "rejected material constants modified material input") &&
               check(same_context(context, original_context), "rejected material constants modified context input");
    } catch (const std::exception&) {
        return check(false, message);
    }
    return check(false, message);
}

bool test_rejects_unsupported_material_constant_domain() {
    return rejects_invalid_material_constant_input(
               [](NnMaterialData& material, NnMaterialProfileContext&) { material.color_terms_bits.pop_back(); },
               "three color terms were accepted") &&
           rejects_invalid_material_constant_input(
               [](NnMaterialData& material, NnMaterialProfileContext&) {
                   material.color_terms_bits.push_back(material.color_terms_bits[0u]);
               },
               "five color terms were accepted") &&
           rejects_invalid_material_constant_input(
               [](NnMaterialData& material, NnMaterialProfileContext&) { material.color_terms_bits.clear(); },
               "empty color terms were accepted") &&
           rejects_invalid_material_constant_input(
               [](NnMaterialData& material, NnMaterialProfileContext&) {
                   material.color_terms_bits[1u][0u] = 0x7FC00000u;
               },
               "NaN diffuse component was accepted") &&
           rejects_invalid_material_constant_input(
               [](NnMaterialData& material, NnMaterialProfileContext&) {
                   material.color_terms_bits[1u][1u] = 0x7F800000u;
               },
               "infinite diffuse component was accepted") &&
           rejects_invalid_material_constant_input(
               [](NnMaterialData& material, NnMaterialProfileContext&) {
                   material.stages[0u].raw_words[3u] = 0x7FC00000u;
               },
               "NaN base alpha was accepted") &&
           rejects_invalid_material_constant_input(
               [](NnMaterialData& material, NnMaterialProfileContext&) {
                   material.stages[0u].raw_words[3u] = 0xFF800000u;
               },
               "infinite base alpha was accepted") &&
           rejects_invalid_material_constant_input(
               [](NnMaterialData&, NnMaterialProfileContext& context) { context.draw_flags_high = 1u; },
               "nonzero high draw flags were accepted") &&
           rejects_invalid_material_constant_input(
               [](NnMaterialData&, NnMaterialProfileContext& context) { context.draw_flags_low = 0x400u; },
               "nonzero low draw flags were accepted") &&
           rejects_invalid_material_constant_input(
               [](NnMaterialData& material, NnMaterialProfileContext&) { material.descriptor_bits[4u] = 1u; },
               "nonselected material stage mask was accepted") &&
           rejects_invalid_material_constant_input(
               [](NnMaterialData& material, NnMaterialProfileContext&) {
                   material.stages.clear();
                   material.descriptor_bits[5u] = 0u;
               },
               "absent base-map stage was accepted") &&
           rejects_invalid_material_constant_input(
               [](NnMaterialData&, NnMaterialProfileContext& context) { context.texture_stage_limit = 0u; },
               "base-map-disabled context was accepted") &&
           rejects_invalid_material_constant_input(
               [](NnMaterialData& material, NnMaterialProfileContext&) {
                   material.stages.push_back(make_stage());
                   material.descriptor_bits[5u] = 2u;
               },
               "multiple material stages were accepted") &&
           rejects_invalid_material_constant_input(
               [](NnMaterialData&, NnMaterialProfileContext& context) { context.vertex_format = 0x7000u; },
               "transformed material profile was accepted");
}

template <typename Mutate>
bool rejects_invalid_input(Mutate&& mutate, const char* message) {
    NnMaterialStageData stage = make_stage();
    NnMaterialProfileContext context{};
    context.texture_stage_limit = 1u;
    mutate(stage, context);
    const NnMaterialStageData original_stage = stage;
    const NnMaterialProfileContext original_context = context;
    try {
        static_cast<void>(nn_unlit_texture_matrix(stage, context));
    } catch (const std::invalid_argument&) {
        return check(same_stage(stage, original_stage), "rejected texture matrix stage was modified") &&
               check(same_context(context, original_context), "rejected texture matrix context was modified");
    } catch (const std::exception&) {
        return check(false, message);
    }
    return check(false, message);
}

bool test_rejects_unsupported_or_malformed_inputs() {
    return rejects_invalid_input(
               [](NnMaterialStageData& stage, NnMaterialProfileContext&) { stage.raw_words.resize(15u); },
               "short texture stage metadata was accepted") &&
           rejects_invalid_input(
               [](NnMaterialStageData& stage, NnMaterialProfileContext&) { stage.raw_words[0u] = 0u; },
               "stage flag metadata mismatch was accepted") &&
           rejects_invalid_input(
               [](NnMaterialStageData& stage, NnMaterialProfileContext&) {
                   stage.flags = 0x60000003u;
                   stage.raw_words[0u] = stage.flags;
               },
               "unsupported texture stage flags were accepted") &&
           rejects_invalid_input(
               [](NnMaterialStageData& stage, NnMaterialProfileContext&) { stage.raw_words[1u] = 8u; },
               "texture index metadata mismatch was accepted") &&
           rejects_invalid_input(
               [](NnMaterialStageData& stage, NnMaterialProfileContext&) {
                   stage.texture_index = -1;
                   stage.raw_words[1u] = 0xffffffffu;
               },
               "negative texture index was accepted") &&
           rejects_invalid_input(
               [](NnMaterialStageData& stage, NnMaterialProfileContext&) { stage.raw_words[2u] = 0xffffffffu; },
               "unsupported negative-one coordinate was accepted") &&
           rejects_invalid_input(
               [](NnMaterialStageData& stage, NnMaterialProfileContext&) { stage.raw_words[2u] = 0xfffffffeu; },
               "unsupported negative-two coordinate was accepted") &&
           rejects_invalid_input(
               [](NnMaterialStageData& stage, NnMaterialProfileContext&) { stage.raw_words[2u] = 4u; },
               "out-of-range texture coordinate was accepted") &&
           rejects_invalid_input(
               [](NnMaterialStageData& stage, NnMaterialProfileContext&) { stage.raw_words[6u] = 0x7f800000u; },
               "infinite texture U scale was accepted") &&
           rejects_invalid_input(
               [](NnMaterialStageData& stage, NnMaterialProfileContext&) { stage.raw_words[7u] = 0x7fc00000u; },
               "NaN texture V scale was accepted") &&
           rejects_invalid_input(
               [](NnMaterialStageData&, NnMaterialProfileContext& context) { context.texture_stage_limit = 0u; },
               "zero texture-stage limit was accepted") &&
           rejects_invalid_input(
               [](NnMaterialStageData&, NnMaterialProfileContext& context) { context.texture_stage_limit = 9u; },
               "large texture-stage limit was accepted") &&
           rejects_invalid_input(
               [](NnMaterialStageData&, NnMaterialProfileContext& context) { context.draw_flags_low = 0x1u; },
               "unsupported low draw flags were accepted") &&
           rejects_invalid_input(
               [](NnMaterialStageData&, NnMaterialProfileContext& context) { context.draw_flags_high = 1u; },
               "unsupported high draw flags were accepted");
}

}

int main() {
    return test_builds_selected_unlit_matrix_without_mutating_inputs() &&
                   test_preserves_selected_material_constant_bits_without_mutating_inputs() &&
                   test_preserves_signed_zero_and_subnormal_scale_bits() &&
                   test_uses_context_domain_and_ignores_offsets() &&
                   test_packs_generic_matrix_columns_by_bits() &&
                   test_builds_noncommutative_model_matrices_through_backend() &&
                   test_rejects_invalid_model_matrix_domain_before_backend() &&
                   test_rejects_unsupported_material_constant_domain() &&
                   test_rejects_unsupported_or_malformed_inputs()
               ? 0
               : 1;
}
