#include "nn_static_palette.h"

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <limits>
#include <stdexcept>
#include <utility>
#include <vector>

namespace {

bool check(bool condition, const char* message) {
    if (!condition) {
        std::fprintf(stderr, "%s\n", message);
    }
    return condition;
}

std::uint32_t float_bits(float value) {
    std::uint32_t result = 0u;
    std::memcpy(&result, &value, sizeof(result));
    return result;
}

float float_from_bits(std::uint32_t bits) {
    float result = 0.0f;
    std::memcpy(&result, &bits, sizeof(result));
    return result;
}

CameraMatrix identity_matrix() {
    return {{
        1.0f, 0.0f, 0.0f, 0.0f,
        0.0f, 1.0f, 0.0f, 0.0f,
        0.0f, 0.0f, 1.0f, 0.0f,
        0.0f, 0.0f, 0.0f, 1.0f,
    }};
}

CameraMatrix translation_matrix(float x, float y, float z) {
    CameraMatrix result = identity_matrix();
    result[12u] = x;
    result[13u] = y;
    result[14u] = z;
    return result;
}

CameraMatrix scale_matrix(float x, float y, float z) {
    CameraMatrix result = identity_matrix();
    result[0u] = x;
    result[5u] = y;
    result[10u] = z;
    return result;
}

CameraMatrix multiply_matrices(const CameraMatrix& left, const CameraMatrix& right) {
    CameraMatrix result{};
    for (std::size_t row = 0u; row < 4u; ++row) {
        for (std::size_t column = 0u; column < 4u; ++column) {
            double sum = 0.0;
            for (std::size_t index = 0u; index < 4u; ++index) {
                sum += static_cast<double>(left[row * 4u + index]) *
                       static_cast<double>(right[index * 4u + column]);
            }
            result[row * 4u + column] = static_cast<float>(sum);
        }
    }
    return result;
}

bool same_matrix_bits(const CameraMatrix& left, const CameraMatrix& right) {
    for (std::size_t index = 0u; index < left.size(); ++index) {
        if (float_bits(left[index]) != float_bits(right[index])) {
            return false;
        }
    }
    return true;
}

std::array<std::uint32_t, 16u> matrix_bits(const CameraMatrix& matrix) {
    std::array<std::uint32_t, 16u> result{};
    for (std::size_t index = 0u; index < result.size(); ++index) {
        result[index] = float_bits(matrix[index]);
    }
    return result;
}

NnNodeData make_node(
    std::uint32_t flags,
    std::int16_t matrix_index,
    std::int16_t parent_index,
    std::int16_t child_index,
    std::int16_t sibling_index,
    const std::array<float, 3u>& translation,
    const std::array<float, 3u>& scale,
    const CameraMatrix& inverse_bind) {
    NnNodeData result{};
    result.flags = flags;
    result.matrix_index = matrix_index;
    result.parent_index = parent_index;
    result.child_index = child_index;
    result.sibling_index = sibling_index;
    for (std::size_t index = 0u; index < translation.size(); ++index) {
        result.translation_bits[index] = float_bits(translation[index]);
        result.scale_bits[index] = float_bits(scale[index]);
    }
    result.rotation_a16 = {{0, 0, 0}};
    result.inverse_bind_bits = matrix_bits(inverse_bind);
    return result;
}

NnModelData make_model(std::vector<NnNodeData> nodes, std::uint32_t matrix_palette_count) {
    NnModelData result{};
    result.node_count = static_cast<std::uint32_t>(nodes.size());
    result.matrix_palette_count = matrix_palette_count;
    result.nodes = std::move(nodes);
    return result;
}

class MatrixBackend final : public CameraMatrixBackend {
public:
    mutable std::vector<CameraMatrix> left_inputs;
    mutable std::vector<CameraMatrix> right_inputs;
    mutable std::vector<CameraPrecision> precisions;
    bool produce_nonfinite = false;
    bool produce_nonaffine = false;

    CameraMatrix rotation_z(float, CameraPrecision) const override {
        return identity_matrix();
    }

    CameraMatrix multiply(
        const CameraMatrix& left, const CameraMatrix& right, CameraPrecision precision) const override {
        left_inputs.push_back(left);
        right_inputs.push_back(right);
        precisions.push_back(precision);
        CameraMatrix result = multiply_matrices(left, right);
        if (produce_nonfinite) {
            result[0u] = float_from_bits(0x7fc00001u);
        }
        if (produce_nonaffine) {
            result[3u] = 1.0f;
        }
        return result;
    }

    CameraMatrix perspective_fov_rh(const CameraProjection&, CameraPrecision) const override {
        return identity_matrix();
    }
};

template <typename Callable>
bool expects_invalid_argument(Callable&& callable, const char* message) {
    try {
        std::forward<Callable>(callable)();
    } catch (const std::invalid_argument&) {
        return true;
    } catch (...) {
        std::fprintf(stderr, "%s (wrong exception type)\n", message);
        return false;
    }
    std::fprintf(stderr, "%s\n", message);
    return false;
}

bool test_node_world_remains_separate_from_palette_slots() {
    MatrixBackend backend;
    const auto base = translation_matrix(0.0f, 0.0f, -20.0f);
    const auto model = make_model({
        make_node(0x00000006u, 1, -1, 1, -1, {{40.0f, 0.0f, 0.0f}}, {{1.0f, 1.0f, 1.0f}},
            translation_matrix(-40.0f, 0.0f, 0.0f)),
        make_node(0x00000006u, 0, 0, -1, -1, {{2.0f, 3.0f, 0.0f}}, {{1.0f, 1.0f, 1.0f}},
            translation_matrix(-42.0f, -3.0f, 0.0f)),
    }, 2u);
    for (const auto precision : {CameraPrecision::Single, CameraPrecision::Double}) {
        const auto result = nn_static_node_matrices(model, base, precision, backend);
        if (!check(result.node_world.size() == 2u && result.palette.size() == 2u,
                "static node-world and palette sizes differ") ||
            !check(same_matrix_bits(result.node_world[0u], translation_matrix(40.0f, 0.0f, -20.0f)),
                "root node-world lost its pre-inverse-bind translation") ||
            !check(same_matrix_bits(result.node_world[1u], translation_matrix(42.0f, 3.0f, -20.0f)),
                "child node-world inherited palette coordinates or palette slot order") ||
            !check(same_matrix_bits(result.palette[0u], base) && same_matrix_bits(result.palette[1u], base),
                "inverse-bind cancellation changed while exposing node-world")) {
            return false;
        }
        const auto palette_only = nn_static_matrix_palette(model, base, precision, backend);
        if (!check(same_matrix_bits(palette_only[0u], result.palette[0u]) &&
                same_matrix_bits(palette_only[1u], result.palette[1u]), "palette-only API changed")) {
            return false;
        }
    }
    return true;
}

bool test_bind_cancellation_keeps_the_supplied_base() {
    const CameraMatrix base = {{
        2.0f, 0.0f, 0.0f, 0.0f,
        0.0f, 3.0f, 0.0f, 0.0f,
        0.0f, 0.0f, 4.0f, 0.0f,
        7.0f, 11.0f, 13.0f, 1.0f,
    }};
    const CameraMatrix local = multiply_matrices(
        scale_matrix(2.0f, 0.5f, 4.0f), translation_matrix(5.0f, -2.0f, 3.0f));
    const CameraMatrix inverse_bind = multiply_matrices(
        translation_matrix(-5.0f, 2.0f, -3.0f), scale_matrix(0.5f, 2.0f, 0.25f));
    NnModelData model = make_model(
        {make_node(0x00000002u, 0, -1, -1, -1, {{5.0f, -2.0f, 3.0f}}, {{2.0f, 0.5f, 4.0f}}, inverse_bind)},
        1u);
    const std::array<std::uint32_t, 3u> original_translation = model.nodes[0u].translation_bits;
    const std::array<std::uint32_t, 3u> original_scale = model.nodes[0u].scale_bits;
    const std::array<std::uint32_t, 16u> original_inverse = model.nodes[0u].inverse_bind_bits;
    const CameraMatrix expected_world = multiply_matrices(local, base);
    const CameraMatrix wrong_order = multiply_matrices(expected_world, inverse_bind);
    MatrixBackend backend;

    const std::vector<CameraMatrix> palette = nn_static_matrix_palette(
        model, base, CameraPrecision::Single, backend);

    return check(palette.size() == 1u, "static palette did not emit its declared slot") &&
           check(same_matrix_bits(palette[0u], base), "bind cancellation lost the supplied base matrix") &&
           check(!same_matrix_bits(wrong_order, base), "noncommutative bind fixture cannot detect operand order") &&
           check(
               backend.left_inputs == std::vector<CameraMatrix>({inverse_bind}) &&
                   backend.right_inputs == std::vector<CameraMatrix>({expected_world}) &&
                   backend.precisions == std::vector<CameraPrecision>({CameraPrecision::Single}),
               "static palette did not call the backend as inverse-bind times world") &&
           check(model.nodes[0u].translation_bits == original_translation &&
                     model.nodes[0u].scale_bits == original_scale &&
                     model.nodes[0u].inverse_bind_bits == original_inverse,
                 "static palette mutated serialized node input");
}

bool test_selected_static_flag_family_keeps_the_supplied_base() {
    const CameraMatrix base = {{
        1.5f, 0.0f, 0.0f, 0.0f,
        0.0f, 2.0f, 0.0f, 0.0f,
        0.0f, 0.0f, 0.75f, 0.0f,
        -4.0f, 8.0f, 12.0f, 1.0f,
    }};
    NnNodeData node = make_node(
        0x002000c6u,
        0,
        -1,
        -1,
        -1,
        {{9.0f, -5.0f, 2.0f}},
        {{1.0f, 1.0f, 1.0f}},
        translation_matrix(-9.0f, 5.0f, -2.0f));
    node.rotation_a16 = {{std::numeric_limits<std::int32_t>::min(), 123, -456}};
    node.scale_bits = {{0x7fc00001u, 0x7f800000u, 0xff800000u}};
    NnModelData model = make_model({node}, 1u);
    MatrixBackend backend;

    const std::vector<CameraMatrix> palette = nn_static_matrix_palette(
        model, base, CameraPrecision::Double, backend);

    return check(palette.size() == 1u && same_matrix_bits(palette[0u], base),
                 "supported static flag family changed the supplied base") &&
           check(backend.left_inputs.size() == 1u && backend.right_inputs.size() == 1u &&
                     backend.precisions == std::vector<CameraPrecision>({CameraPrecision::Double}),
                 "supported static flag family did not evaluate its active inverse bind");
}

bool test_traversal_only_node_leaves_inverse_bind_unread() {
    NnNodeData root = make_node(
        0x00000006u,
        -1,
        -1,
        1,
        -1,
        {{1.0f, 0.0f, 0.0f}},
        {{1.0f, 1.0f, 1.0f}},
        identity_matrix());
    root.inverse_bind_bits.fill(0x7fc00001u);
    const NnNodeData child = make_node(
        0x0000000eu,
        0,
        0,
        -1,
        -1,
        {{0.0f, 2.0f, 0.0f}},
        {{0.0f, 0.0f, 0.0f}},
        identity_matrix());
    const NnModelData model = make_model({root, child}, 1u);
    MatrixBackend backend;

    const std::vector<CameraMatrix> palette = nn_static_matrix_palette(
        model, identity_matrix(), CameraPrecision::Single, backend);

    return check(palette.size() == 1u && same_matrix_bits(palette[0u], translation_matrix(1.0f, 2.0f, 0.0f)),
                 "traversal-only node did not carry its world into its child") &&
           check(backend.left_inputs.empty(), "traversal-only node decoded its unused inverse bind");
}

bool test_child_and_sibling_inheritance() {
    constexpr std::uint32_t kUnitRotationScaleInverse = 0x0000000eu;
    const CameraMatrix inverse = identity_matrix();
    NnModelData model = make_model({
        make_node(kUnitRotationScaleInverse, 0, -1, 1, -1, {{10.0f, 0.0f, 0.0f}}, {{0.0f, 0.0f, 0.0f}}, inverse),
        make_node(kUnitRotationScaleInverse, 1, 0, 2, 3, {{0.0f, 5.0f, 0.0f}}, {{0.0f, 0.0f, 0.0f}}, inverse),
        make_node(kUnitRotationScaleInverse, 2, 1, -1, -1, {{0.0f, 0.0f, 7.0f}}, {{0.0f, 0.0f, 0.0f}}, inverse),
        make_node(kUnitRotationScaleInverse, 3, 0, -1, -1, {{0.0f, -5.0f, 0.0f}}, {{0.0f, 0.0f, 0.0f}}, inverse),
    }, 4u);
    MatrixBackend backend;
    const std::vector<CameraMatrix> palette = nn_static_matrix_palette(
        model, identity_matrix(), CameraPrecision::Double, backend);

    return check(palette.size() == 4u, "hierarchy palette slot count mismatch") &&
           check(same_matrix_bits(palette[0u], translation_matrix(10.0f, 0.0f, 0.0f)), "root world mismatch") &&
           check(same_matrix_bits(palette[1u], translation_matrix(10.0f, 5.0f, 0.0f)), "child world mismatch") &&
           check(same_matrix_bits(palette[2u], translation_matrix(10.0f, 5.0f, 7.0f)), "grandchild world mismatch") &&
           check(same_matrix_bits(palette[3u], translation_matrix(10.0f, -5.0f, 0.0f)), "sibling inherited the wrong parent world") &&
           check(backend.left_inputs.empty() && backend.right_inputs.empty() && backend.precisions.empty(),
                 "unit inverse-bind nodes invoked the matrix backend");
}

bool test_root_sibling_palette_slot_permutation() {
    constexpr std::uint32_t kUnitRotationScaleInverse = 0x0000000eu;
    const CameraMatrix base = translation_matrix(4.0f, 5.0f, 6.0f);
    const NnModelData model = make_model({
        make_node(
            kUnitRotationScaleInverse,
            2,
            -1,
            -1,
            1,
            {{1.0f, 0.0f, 0.0f}},
            {{0.0f, 0.0f, 0.0f}},
            identity_matrix()),
        make_node(
            kUnitRotationScaleInverse,
            0,
            -1,
            -1,
            2,
            {{0.0f, 2.0f, 0.0f}},
            {{0.0f, 0.0f, 0.0f}},
            identity_matrix()),
        make_node(
            kUnitRotationScaleInverse,
            1,
            -1,
            -1,
            -1,
            {{0.0f, 0.0f, 3.0f}},
            {{0.0f, 0.0f, 0.0f}},
            identity_matrix()),
    }, 3u);
    MatrixBackend backend;

    const std::vector<CameraMatrix> palette = nn_static_matrix_palette(
        model, base, CameraPrecision::Single, backend);

    return check(palette.size() == 3u, "root-sibling palette slot count mismatch") &&
           check(
               same_matrix_bits(palette[0u], translation_matrix(4.0f, 7.0f, 6.0f)),
               "root sibling did not write its declared palette slot") &&
           check(
               same_matrix_bits(palette[1u], translation_matrix(4.0f, 5.0f, 9.0f)),
               "second root sibling did not write its declared palette slot") &&
           check(
               same_matrix_bits(palette[2u], translation_matrix(5.0f, 5.0f, 6.0f)),
               "root did not write its declared palette slot") &&
           check(backend.left_inputs.empty() && backend.right_inputs.empty() && backend.precisions.empty(),
                 "unit root siblings invoked the matrix backend");
}

bool test_translation_precision_uses_declared_significand() {
    constexpr std::uint32_t kUnitRotationScaleInverse = 0x0000000eu;
    CameraMatrix base = identity_matrix();
    base[4u] = 1.0f;
    base[8u] = 1.0f;
    const NnModelData model = make_model(
        {make_node(
            kUnitRotationScaleInverse,
            0,
            -1,
            -1,
            -1,
            {{16777216.0f, 1.0f, -16777216.0f}},
            {{0.0f, 0.0f, 0.0f}},
            identity_matrix())},
        1u);
    MatrixBackend single_backend;
    MatrixBackend double_backend;

    const std::vector<CameraMatrix> single_palette = nn_static_matrix_palette(
        model, base, CameraPrecision::Single, single_backend);
    const std::vector<CameraMatrix> double_palette = nn_static_matrix_palette(
        model, base, CameraPrecision::Double, double_backend);

    return check(
               float_bits(single_palette[0u][12u]) == 0x00000000u &&
                   float_bits(single_palette[0u][13u]) == 0x3f800000u &&
                   float_bits(single_palette[0u][14u]) == 0xcb800000u,
               "single-precision static translation words mismatch") &&
           check(
               float_bits(double_palette[0u][12u]) == 0x3f800000u &&
                   float_bits(double_palette[0u][13u]) == 0x3f800000u &&
                   float_bits(double_palette[0u][14u]) == 0xcb800000u,
               "double-precision static translation words mismatch") &&
           check(
               float_bits(single_palette[0u][12u]) != float_bits(double_palette[0u][12u]),
               "static translation did not preserve its declared precision distinction") &&
           check(
               single_backend.left_inputs.empty() && single_backend.right_inputs.empty() &&
                   single_backend.precisions.empty() && double_backend.left_inputs.empty() &&
                   double_backend.right_inputs.empty() && double_backend.precisions.empty(),
                 "unit inverse-bind precision fixture invoked the matrix backend");
}

NnModelData make_translation_chain(std::size_t depth) {
    constexpr std::uint32_t kUnitRotationScaleInverse = 0x0000000eu;
    std::vector<NnNodeData> nodes;
    nodes.reserve(depth);
    for (std::size_t index = 0u; index < depth; ++index) {
        const std::int16_t node_index = static_cast<std::int16_t>(index);
        const std::int16_t parent_index = index == 0u
            ? static_cast<std::int16_t>(-1)
            : static_cast<std::int16_t>(index - 1u);
        const std::int16_t child_index = index + 1u == depth
            ? static_cast<std::int16_t>(-1)
            : static_cast<std::int16_t>(index + 1u);
        nodes.push_back(make_node(
            kUnitRotationScaleInverse,
            node_index,
            parent_index,
            child_index,
            -1,
            {{1.0f, 0.0f, 0.0f}},
            {{0.0f, 0.0f, 0.0f}},
            identity_matrix()));
    }
    return make_model(std::move(nodes), static_cast<std::uint32_t>(depth));
}

bool test_topology_depth_limit_ignores_serialized_metadata() {
    NnModelData accepted = make_translation_chain(32u);
    accepted.max_node_depth = 0u;
    MatrixBackend accepted_backend;
    const std::vector<CameraMatrix> accepted_palette = nn_static_matrix_palette(
        accepted, identity_matrix(), CameraPrecision::Single, accepted_backend);

    if (!check(accepted_palette.size() == 32u, "depth-32 hierarchy palette slot count mismatch") ||
        !check(
            same_matrix_bits(accepted_palette.back(), translation_matrix(32.0f, 0.0f, 0.0f)),
            "depth-32 hierarchy did not produce its terminal palette") ||
        !check(
            accepted_backend.left_inputs.empty() && accepted_backend.right_inputs.empty() &&
                accepted_backend.precisions.empty(),
            "depth-32 unit hierarchy invoked the matrix backend")) {
        return false;
    }

    NnModelData rejected = make_translation_chain(33u);
    rejected.max_node_depth = std::numeric_limits<std::uint32_t>::max();
    MatrixBackend rejected_backend;
    return expects_invalid_argument(
        [&]() {
            nn_static_matrix_palette(
                rejected, identity_matrix(), CameraPrecision::Double, rejected_backend);
        },
        "depth-33 hierarchy was accepted despite its serialized depth metadata");
}

bool test_unit_flags_leave_poisoned_fields_unread() {
    NnNodeData node = make_node(
        0x0000000fu,
        0,
        -1,
        -1,
        -1,
        {{0.0f, 0.0f, 0.0f}},
        {{1.0f, 1.0f, 1.0f}},
        identity_matrix());
    node.translation_bits = {{0x7fc00001u, 0x7f800000u, 0xff800000u}};
    node.rotation_a16 = {{std::numeric_limits<std::int32_t>::min(), -1, 1}};
    node.scale_bits = {{0x7fc00002u, 0x7f800000u, 0xff800000u}};
    node.inverse_bind_bits.fill(0x7fc00003u);
    CameraMatrix base = translation_matrix(-7.0f, 11.0f, 13.0f);
    base[1u] = float_from_bits(0x80000000u);
    base[14u] = float_from_bits(0x80000000u);
    const NnModelData model = make_model({node}, 1u);
    MatrixBackend backend;

    const std::vector<CameraMatrix> palette = nn_static_matrix_palette(
        model, base, CameraPrecision::Double, backend);

    return check(palette.size() == 1u && same_matrix_bits(palette[0u], base),
                 "unit flags did not preserve the finite supplied base") &&
           check(backend.left_inputs.empty(), "unit inverse-bind decoded poisoned matrix data");
}

NnModelData valid_model() {
    return make_model(
        {make_node(0x00000006u, 0, -1, -1, -1, {{1.0f, 2.0f, 3.0f}}, {{1.0f, 1.0f, 1.0f}}, identity_matrix())},
        1u);
}

bool test_rejects_unsupported_or_nonfinite_inputs() {
    MatrixBackend backend;
    if (!expects_invalid_argument(
            [&]() { nn_static_matrix_palette(valid_model(), identity_matrix(), static_cast<CameraPrecision>(99), backend); },
            "unsupported static-palette precision was accepted")) {
        return false;
    }

    NnModelData unsupported = valid_model();
    unsupported.nodes[0u].flags = 0x00000010u;
    if (!expects_invalid_argument(
            [&]() { nn_static_matrix_palette(unsupported, identity_matrix(), CameraPrecision::Single, backend); },
            "unsupported static node flags were accepted")) {
        return false;
    }

    NnModelData nonunit_rotation = valid_model();
    nonunit_rotation.nodes[0u].flags = 0x00000004u;
    if (!expects_invalid_argument(
            [&]() { nn_static_matrix_palette(nonunit_rotation, identity_matrix(), CameraPrecision::Single, backend); },
            "nonunit static rotation was accepted")) {
        return false;
    }

    NnModelData nonfinite_translation = valid_model();
    nonfinite_translation.nodes[0u].translation_bits[0u] = 0x7fc00001u;
    if (!expects_invalid_argument(
            [&]() { nn_static_matrix_palette(nonfinite_translation, identity_matrix(), CameraPrecision::Single, backend); },
            "nonfinite active translation was accepted")) {
        return false;
    }

    NnModelData oversized_scale = valid_model();
    oversized_scale.nodes[0u].flags = 0x00000002u;
    oversized_scale.nodes[0u].scale_bits[1u] = float_bits(33554432.0f);
    if (!expects_invalid_argument(
            [&]() { nn_static_matrix_palette(oversized_scale, identity_matrix(), CameraPrecision::Single, backend); },
            "out-of-range active scale was accepted")) {
        return false;
    }

    NnModelData nonfinite_inverse = valid_model();
    nonfinite_inverse.nodes[0u].inverse_bind_bits[0u] = 0x7fc00001u;
    if (!expects_invalid_argument(
            [&]() { nn_static_matrix_palette(nonfinite_inverse, identity_matrix(), CameraPrecision::Single, backend); },
            "nonfinite active inverse bind was accepted")) {
        return false;
    }

    CameraMatrix nonaffine_base = identity_matrix();
    nonaffine_base[3u] = 1.0f;
    if (!expects_invalid_argument(
            [&]() { nn_static_matrix_palette(valid_model(), nonaffine_base, CameraPrecision::Single, backend); },
            "nonaffine supplied base was accepted")) {
        return false;
    }

    MatrixBackend nonfinite_backend;
    nonfinite_backend.produce_nonfinite = true;
    if (!expects_invalid_argument(
            [&]() { nn_static_matrix_palette(valid_model(), identity_matrix(), CameraPrecision::Single, nonfinite_backend); },
            "nonfinite backend palette result was accepted")) {
        return false;
    }

    MatrixBackend nonaffine_backend;
    nonaffine_backend.produce_nonaffine = true;
    return expects_invalid_argument(
        [&]() { nn_static_matrix_palette(valid_model(), identity_matrix(), CameraPrecision::Single, nonaffine_backend); },
        "nonaffine backend palette result was accepted");
}

bool test_rejects_malformed_hierarchies_and_palette_claims() {
    MatrixBackend backend;

    NnModelData count_mismatch = valid_model();
    count_mismatch.node_count = 2u;
    if (!expects_invalid_argument(
            [&]() { nn_static_matrix_palette(count_mismatch, identity_matrix(), CameraPrecision::Single, backend); },
            "node-count mismatch was accepted")) {
        return false;
    }

    NnModelData missing_slot = valid_model();
    missing_slot.nodes[0u].matrix_index = -1;
    if (!expects_invalid_argument(
            [&]() { nn_static_matrix_palette(missing_slot, identity_matrix(), CameraPrecision::Single, backend); },
            "unfilled palette slot was accepted")) {
        return false;
    }

    NnModelData duplicate_slot = make_model({
        make_node(0x00000006u, 0, -1, 1, -1, {{0.0f, 0.0f, 0.0f}}, {{1.0f, 1.0f, 1.0f}}, identity_matrix()),
        make_node(0x00000006u, 0, 0, -1, -1, {{0.0f, 0.0f, 0.0f}}, {{1.0f, 1.0f, 1.0f}}, identity_matrix()),
    }, 1u);
    if (!expects_invalid_argument(
            [&]() { nn_static_matrix_palette(duplicate_slot, identity_matrix(), CameraPrecision::Single, backend); },
            "duplicate palette slot was accepted")) {
        return false;
    }

    NnModelData out_of_range_slot = valid_model();
    out_of_range_slot.nodes[0u].matrix_index = 1;
    if (!expects_invalid_argument(
            [&]() { nn_static_matrix_palette(out_of_range_slot, identity_matrix(), CameraPrecision::Single, backend); },
            "out-of-range palette slot was accepted")) {
        return false;
    }

    NnModelData inconsistent_child = make_model({
        make_node(0x0000000eu, 0, -1, 1, -1, {{0.0f, 0.0f, 0.0f}}, {{0.0f, 0.0f, 0.0f}}, identity_matrix()),
        make_node(0x0000000eu, 1, -1, -1, -1, {{0.0f, 0.0f, 0.0f}}, {{0.0f, 0.0f, 0.0f}}, identity_matrix()),
    }, 2u);
    if (!expects_invalid_argument(
            [&]() { nn_static_matrix_palette(inconsistent_child, identity_matrix(), CameraPrecision::Single, backend); },
            "inconsistent child-parent link was accepted")) {
        return false;
    }

    NnModelData sibling_cycle = make_model({
        make_node(0x0000000eu, 0, -1, 1, -1, {{0.0f, 0.0f, 0.0f}}, {{0.0f, 0.0f, 0.0f}}, identity_matrix()),
        make_node(0x0000000eu, 1, 0, -1, 2, {{0.0f, 0.0f, 0.0f}}, {{0.0f, 0.0f, 0.0f}}, identity_matrix()),
        make_node(0x0000000eu, 2, 0, -1, 1, {{0.0f, 0.0f, 0.0f}}, {{0.0f, 0.0f, 0.0f}}, identity_matrix()),
    }, 3u);
    if (!expects_invalid_argument(
            [&]() { nn_static_matrix_palette(sibling_cycle, identity_matrix(), CameraPrecision::Single, backend); },
            "sibling cycle was accepted")) {
        return false;
    }

    NnModelData out_of_range_link = valid_model();
    out_of_range_link.nodes[0u].child_index = 2;
    if (!expects_invalid_argument(
            [&]() { nn_static_matrix_palette(out_of_range_link, identity_matrix(), CameraPrecision::Single, backend); },
            "out-of-range hierarchy link was accepted")) {
        return false;
    }

    NnModelData orphan = make_model({
        make_node(0x0000000eu, 0, -1, -1, -1, {{0.0f, 0.0f, 0.0f}}, {{0.0f, 0.0f, 0.0f}}, identity_matrix()),
        make_node(0x0000000eu, 1, -1, -1, -1, {{0.0f, 0.0f, 0.0f}}, {{0.0f, 0.0f, 0.0f}}, identity_matrix()),
    }, 2u);
    return expects_invalid_argument(
        [&]() { nn_static_matrix_palette(orphan, identity_matrix(), CameraPrecision::Single, backend); },
        "orphaned static node was accepted");
}

}

int main() {
    return test_node_world_remains_separate_from_palette_slots() &&
                   test_bind_cancellation_keeps_the_supplied_base() &&
                   test_selected_static_flag_family_keeps_the_supplied_base() &&
                   test_traversal_only_node_leaves_inverse_bind_unread() &&
                   test_child_and_sibling_inheritance() &&
                   test_root_sibling_palette_slot_permutation() &&
                   test_translation_precision_uses_declared_significand() &&
                   test_topology_depth_limit_ignores_serialized_metadata() &&
                   test_unit_flags_leave_poisoned_fields_unread() &&
                   test_rejects_unsupported_or_nonfinite_inputs() &&
                   test_rejects_malformed_hierarchies_and_palette_claims()
               ? 0
               : 1;
}
