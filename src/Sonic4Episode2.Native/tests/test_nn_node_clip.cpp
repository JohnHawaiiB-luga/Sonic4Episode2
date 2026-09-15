#include "nn_node_clip.h"

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <limits>
#include <stdexcept>
#include <utility>

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

float from_bits(std::uint32_t bits) {
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

NnPerspectiveClipContext perspective_context() {
    return {
        1.0f,
        10.0f,
        {{8.0f, 1.0f}},
        {{-4.0f, 1.0f}},
        {{4.0f, 1.0f}},
        {{-2.0f, 1.0f}},
    };
}

NnNodeData make_leaf(
    std::uint32_t node_flags,
    std::int16_t matrix_index,
    const std::array<float, 3u>& center,
    const std::array<float, 3u>& half_extents) {
    NnNodeData result{};
    result.flags = node_flags;
    result.matrix_index = matrix_index;
    result.parent_index = -1;
    result.child_index = -1;
    result.sibling_index = -1;
    for (std::size_t index = 0u; index < center.size(); ++index) {
        result.opaque_bits[index] = float_bits(center[index]);
        result.opaque_bits[5u + index] = float_bits(half_extents[index]);
    }
    return result;
}

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

bool test_box_masks_cover_all_perspective_faces() {
    const NnPerspectiveClipContext context = perspective_context();
    const CameraMatrix world = identity_matrix();
    const std::array<float, 3u> half_extents = {{0.5f, 0.5f, 0.5f}};
    bool passed = true;

    passed = check(
                 nn_clip_box({{0.0f, 0.0f, -5.0f}}, half_extents, world, context, CameraPrecision::Single) == 2u,
                 "fully inside perspective box did not return 2") &&
             passed;
    passed = check(
                 nn_clip_box({{1.75f, 0.0f, -5.0f}}, half_extents, world, context, CameraPrecision::Single) == 0u,
                 "right-side intersection did not return its raw side mask") &&
             passed;
    passed = check(
                 nn_clip_box({{-3.0f, 0.0f, -5.0f}}, half_extents, world, context, CameraPrecision::Single) == 0u,
                 "left-side intersection did not return its raw side mask") &&
             passed;
    passed = check(
                 nn_clip_box({{0.0f, 1.0f, -5.0f}}, half_extents, world, context, CameraPrecision::Single) == 0u,
                 "top-side intersection did not return its raw side mask") &&
             passed;
    passed = check(
                 nn_clip_box({{0.0f, -1.5f, -5.0f}}, half_extents, world, context, CameraPrecision::Single) == 0u,
                 "bottom-side intersection did not return its raw side mask") &&
             passed;
    passed = check(
                 nn_clip_box({{0.0f, 0.0f, -1.2f}}, half_extents, world, context, CameraPrecision::Single) == 4u,
                 "near-plane intersection did not return mask 4") &&
             passed;
    passed = check(
                 nn_clip_box({{0.0f, 0.0f, -9.8f}}, half_extents, world, context, CameraPrecision::Single) == 8u,
                 "far-plane intersection did not return mask 8") &&
             passed;
    passed = check(
                 nn_clip_box({{2.0f, 0.0f, -5.0f}}, half_extents, world, context, CameraPrecision::Single) == 0x10u,
                 "right-side outside box was not rejected") &&
             passed;
    passed = check(
                 nn_clip_box({{-3.5f, 0.0f, -5.0f}}, half_extents, world, context, CameraPrecision::Single) == 0x10u,
                 "left-side outside box was not rejected") &&
             passed;
    passed = check(
                 nn_clip_box({{0.0f, 1.25f, -5.0f}}, half_extents, world, context, CameraPrecision::Single) == 0x10u,
                 "top-side outside box was not rejected") &&
             passed;
    passed = check(
                 nn_clip_box({{0.0f, -2.0f, -5.0f}}, half_extents, world, context, CameraPrecision::Single) == 0x10u,
                 "bottom-side outside box was not rejected") &&
             passed;
    passed = check(
                 nn_clip_box({{0.0f, 0.0f, -0.4f}}, half_extents, world, context, CameraPrecision::Single) == 0x10u,
                 "near-side outside box was not rejected") &&
             passed;
    passed = check(
                 nn_clip_box({{0.0f, 0.0f, -10.6f}}, half_extents, world, context, CameraPrecision::Single) == 0x10u,
                 "far-side outside box was not rejected") &&
             passed;
    passed = check(
                 nn_clip_box({{1.875f, 0.0f, -5.0f}}, half_extents, world, context, CameraPrecision::Double) == 0u,
                 "right tangent was treated as outside") &&
             passed;
    passed = check(
                 nn_clip_box({{-3.25f, 0.0f, -5.0f}}, half_extents, world, context, CameraPrecision::Double) == 0u,
                 "left tangent was treated as outside") &&
             passed;
    passed = check(
                 nn_clip_box({{0.0f, 1.1875f, -5.0f}}, half_extents, world, context, CameraPrecision::Double) == 0u,
                 "top tangent was treated as outside") &&
             passed;
    passed = check(
                 nn_clip_box({{0.0f, -1.875f, -5.0f}}, half_extents, world, context, CameraPrecision::Double) == 0u,
                 "bottom tangent was treated as outside") &&
             passed;
    passed = check(
                 nn_clip_box({{0.0f, 0.0f, -0.5f}}, half_extents, world, context, CameraPrecision::Double) == 4u,
                 "near tangent did not preserve the strict outside comparison") &&
             passed;
    passed = check(
                 nn_clip_box({{0.0f, 0.0f, -10.5f}}, half_extents, world, context, CameraPrecision::Double) == 8u,
                 "far tangent did not preserve the strict outside comparison") &&
             passed;
    return passed;
}

bool test_node_world_controls_oriented_visibility() {
    const NnPerspectiveClipContext context = perspective_context();
    const CameraMatrix oriented_world = {{
        0.0f, 2.0f, 0.0f, 0.0f,
        -3.0f, 0.0f, 0.0f, 0.0f,
        0.0f, 0.0f, 2.0f, 0.0f,
        0.0f, 0.0f, -8.0f, 1.0f,
    }};
    const std::array<float, 3u> center = {{0.0f, 0.0f, 0.0f}};
    const std::array<float, 3u> half_extents = {{0.25f, 0.5f, 0.25f}};
    const CameraMatrix current_world = translation_matrix(0.0f, 0.0f, -5.0f);
    const CameraMatrix canceled_palette = identity_matrix();

    return check(
               nn_clip_box(center, half_extents, oriented_world, context, CameraPrecision::Single) == 2u,
               "oriented nonuniform node-world box was not classified in row-vector space") &&
           check(
               nn_clip_box(center, half_extents, current_world, context, CameraPrecision::Double) == 2u,
               "supplied node-world matrix did not make the box visible") &&
           check(
               nn_clip_box(center, half_extents, canceled_palette, context, CameraPrecision::Double) == 0x10u,
               "canceled palette matrix was treated as the supplied node-world matrix");
}

bool test_leaf_status_overlay_preserves_initial_bits() {
    const NnPerspectiveClipContext context = perspective_context();
    NnNodeData inside = make_leaf(
        0x00200010u,
        0,
        {{0.0f, 0.0f, 0.0f}},
        {{0.25f, 0.25f, 0.25f}});
    inside.opaque_bits[3u] = 0x7fc00001u;
    inside.opaque_bits[4u] = 0x7f800000u;
    const std::uint32_t inside_status = nn_clip_box_node_status(
        inside,
        translation_matrix(0.0f, 0.0f, -5.0f),
        context,
        0x80000080u,
        1u,
        CameraPrecision::Single);

    const NnNodeData outside = make_leaf(
        0x00200000u,
        0,
        {{0.0f, 0.0f, 0.0f}},
        {{0.25f, 0.25f, 0.25f}});
    const std::uint32_t outside_status = nn_clip_box_node_status(
        outside,
        identity_matrix(),
        context,
        0x80000080u,
        0x12u,
        CameraPrecision::Double);

    return check(
               inside_status == 0x80000083u,
               "leaf box status lost initial bits, node status, or inside result") &&
           check(
               outside_status == 0x80000490u,
               "outside leaf box status did not overlay the declared flags");
}

bool test_inactive_leaf_paths_leave_poisoned_geometry_unread() {
    const float nan = std::numeric_limits<float>::quiet_NaN();
    NnPerspectiveClipContext poisoned_context = perspective_context();
    poisoned_context.near_plane = nan;
    poisoned_context.far_plane = nan;
    poisoned_context.top.fill(nan);
    poisoned_context.bottom.fill(nan);
    poisoned_context.right.fill(nan);
    poisoned_context.left.fill(nan);
    CameraMatrix poisoned_world = identity_matrix();
    poisoned_world.fill(nan);

    NnNodeData skipped = make_leaf(
        0u,
        0,
        {{0.0f, 0.0f, 0.0f}},
        {{0.0f, 0.0f, 0.0f}});
    skipped.child_index = 7;
    skipped.opaque_bits.fill(0x7fc00001u);

    NnNodeData disabled = make_leaf(
        0x10u,
        0,
        {{0.0f, 0.0f, 0.0f}},
        {{0.0f, 0.0f, 0.0f}});
    disabled.opaque_bits.fill(0x7fc00001u);

    NnNodeData no_matrix = disabled;
    no_matrix.matrix_index = -1;

    NnNodeData hidden = make_leaf(
        0x20u,
        0,
        {{0.0f, 0.0f, 0.0f}},
        {{0.0f, 0.0f, 0.0f}});
    hidden.opaque_bits.fill(0x7fc00001u);

    return check(
               nn_clip_box_node_status(
                   skipped,
                   poisoned_world,
                   poisoned_context,
                   0x401u,
                   1u,
                   CameraPrecision::Single) == 0x401u,
               "early status skip decoded an inactive leaf") &&
           check(
               nn_clip_box_node_status(
                   disabled,
                   poisoned_world,
                   poisoned_context,
                   0x80u,
                   0u,
                   CameraPrecision::Double) == 0x81u,
               "disabled leaf decoded inactive geometry") &&
           check(
               nn_clip_box_node_status(
                   no_matrix,
                   poisoned_world,
                   poisoned_context,
                   0x80u,
                   1u,
                   CameraPrecision::Single) == 0x81u,
               "matrix-less leaf decoded inactive geometry") &&
           check(
               nn_clip_box_node_status(
                   hidden,
                   poisoned_world,
                   poisoned_context,
                   0x80u,
                   1u,
                   CameraPrecision::Double) == 0x81u,
               "hidden leaf decoded inactive geometry");
}

bool test_precision_changes_an_asymmetric_boundary_decision() {
    const std::array<std::uint32_t, 16u> world_bits{{
        1071934022u, 3222940973u, 1063510001u, 0u,
        1058518128u, 3214405450u, 3192596681u, 0u,
        3207317117u, 1071536610u, 3207669556u, 0u,
        1122440773u, 0u, 3267887104u, 1065353216u,
    }};
    CameraMatrix world{};
    for (std::size_t index = 0u; index < world.size(); ++index) {
        world[index] = from_bits(world_bits[index]);
    }
    const NnPerspectiveClipContext context{
        1.0f, 1000.0f,
        {{from_bits(1068346670u), from_bits(1068010860u)}},
        {{from_bits(3225279599u), from_bits(1074317031u)}},
        {{from_bits(1063312734u), from_bits(1063503449u)}},
        {{from_bits(3210773621u), from_bits(1063417591u)}},
    };
    const std::array<float, 3u> center{{0.0f, 0.0f, 0.0f}};
    const std::array<float, 3u> extents{{
        from_bits(1079084480u), from_bits(1066702528u), from_bits(1080641609u),
    }};
    const auto node = make_leaf(0x0020000eu, 0, center, extents);
    return check(nn_clip_box(center, extents, world, context, CameraPrecision::Single) == 0u,
                 "single precision did not retain the intersecting boundary box") &&
           check(nn_clip_box(center, extents, world, context, CameraPrecision::Double) == 0x10u,
                 "double precision did not reject the boundary box") &&
           check(nn_clip_box_node_status(node, world, context, 0u, 0x11u, CameraPrecision::Single) == 0u,
                 "single precision boundary status differs") &&
           check(nn_clip_box_node_status(node, world, context, 0u, 0x11u, CameraPrecision::Double) == 0x11u,
                 "double precision boundary status differs");
}

bool test_sphere_tangencies_cover_all_perspective_faces() {
    const NnPerspectiveClipContext context = perspective_context();
    const CameraMatrix world = identity_matrix();
    const float radius = 0.5f;
    const float positive_infinity = std::numeric_limits<float>::infinity();
    bool passed = true;
    const auto check_both_precisions = [&](const std::array<float, 3u>& center, std::uint32_t expected, const char* message) {
        for (const CameraPrecision precision : {CameraPrecision::Single, CameraPrecision::Double}) {
            passed = check(nn_clip_sphere(center, radius, world, context, precision) == expected, message) && passed;
        }
    };

    check_both_precisions({{0.0f, 0.0f, -0.5f}}, 4u, "near external tangent differs");
    check_both_precisions(
        {{0.0f, 0.0f, std::nextafter(-0.5f, positive_infinity)}},
        0x10u,
        "near external neighbor was not outside");
    check_both_precisions(
        {{0.0f, 0.0f, std::nextafter(-0.5f, -positive_infinity)}},
        4u,
        "near internal neighbor differs");
    check_both_precisions({{0.0f, 0.0f, -1.5f}}, 2u, "near internal tangent differs");
    check_both_precisions(
        {{0.0f, 0.0f, std::nextafter(-1.5f, positive_infinity)}},
        4u,
        "near internal exterior neighbor differs");
    check_both_precisions(
        {{0.0f, 0.0f, std::nextafter(-1.5f, -positive_infinity)}},
        2u,
        "near internal interior neighbor differs");

    check_both_precisions({{0.0f, 0.0f, -10.5f}}, 8u, "far external tangent differs");
    check_both_precisions(
        {{0.0f, 0.0f, std::nextafter(-10.5f, -positive_infinity)}},
        0x10u,
        "far external neighbor was not outside");
    check_both_precisions(
        {{0.0f, 0.0f, std::nextafter(-10.5f, positive_infinity)}},
        8u,
        "far internal neighbor differs");
    check_both_precisions({{0.0f, 0.0f, -9.5f}}, 2u, "far internal tangent differs");
    check_both_precisions(
        {{0.0f, 0.0f, std::nextafter(-9.5f, -positive_infinity)}},
        8u,
        "far internal exterior neighbor differs");
    check_both_precisions(
        {{0.0f, 0.0f, std::nextafter(-9.5f, positive_infinity)}},
        2u,
        "far internal interior neighbor differs");

    check_both_precisions({{1.375f, 0.0f, -5.0f}}, 0u, "right external tangent differs");
    check_both_precisions(
        {{std::nextafter(1.375f, positive_infinity), 0.0f, -5.0f}},
        0x10u,
        "right external neighbor was not outside");
    check_both_precisions(
        {{std::nextafter(1.375f, -positive_infinity), 0.0f, -5.0f}},
        0u,
        "right internal neighbor differs");
    check_both_precisions({{1.125f, 0.0f, -5.0f}}, 2u, "right internal tangent differs");
    check_both_precisions(
        {{std::nextafter(1.125f, positive_infinity), 0.0f, -5.0f}},
        0u,
        "right internal exterior neighbor differs");
    check_both_precisions(
        {{std::nextafter(1.125f, -positive_infinity), 0.0f, -5.0f}},
        2u,
        "right internal interior neighbor differs");

    check_both_precisions({{-2.75f, 0.0f, -5.0f}}, 0u, "left external tangent differs");
    check_both_precisions(
        {{std::nextafter(-2.75f, -positive_infinity), 0.0f, -5.0f}},
        0x10u,
        "left external neighbor was not outside");
    check_both_precisions(
        {{std::nextafter(-2.75f, positive_infinity), 0.0f, -5.0f}},
        0u,
        "left internal neighbor differs");
    check_both_precisions({{-2.25f, 0.0f, -5.0f}}, 2u, "left internal tangent differs");
    check_both_precisions(
        {{std::nextafter(-2.25f, -positive_infinity), 0.0f, -5.0f}},
        0u,
        "left internal exterior neighbor differs");
    check_both_precisions(
        {{std::nextafter(-2.25f, positive_infinity), 0.0f, -5.0f}},
        2u,
        "left internal interior neighbor differs");

    check_both_precisions({{0.0f, 0.6875f, -5.0f}}, 0u, "top external tangent differs");
    check_both_precisions(
        {{0.0f, std::nextafter(0.6875f, positive_infinity), -5.0f}},
        0x10u,
        "top external neighbor was not outside");
    check_both_precisions(
        {{0.0f, std::nextafter(0.6875f, -positive_infinity), -5.0f}},
        0u,
        "top internal neighbor differs");
    check_both_precisions({{0.0f, 0.5625f, -5.0f}}, 2u, "top internal tangent differs");
    check_both_precisions(
        {{0.0f, std::nextafter(0.5625f, positive_infinity), -5.0f}},
        0u,
        "top internal exterior neighbor differs");
    check_both_precisions(
        {{0.0f, std::nextafter(0.5625f, -positive_infinity), -5.0f}},
        2u,
        "top internal interior neighbor differs");

    check_both_precisions({{0.0f, -1.375f, -5.0f}}, 0u, "bottom external tangent differs");
    check_both_precisions(
        {{0.0f, std::nextafter(-1.375f, -positive_infinity), -5.0f}},
        0x10u,
        "bottom external neighbor was not outside");
    check_both_precisions(
        {{0.0f, std::nextafter(-1.375f, positive_infinity), -5.0f}},
        0u,
        "bottom internal neighbor differs");
    check_both_precisions({{0.0f, -1.125f, -5.0f}}, 2u, "bottom internal tangent differs");
    check_both_precisions(
        {{0.0f, std::nextafter(-1.125f, -positive_infinity), -5.0f}},
        0u,
        "bottom internal exterior neighbor differs");
    check_both_precisions(
        {{0.0f, std::nextafter(-1.125f, positive_infinity), -5.0f}},
        2u,
        "bottom internal interior neighbor differs");
    return passed;
}

bool test_zero_radius_sphere_behaves_as_a_direct_point() {
    const NnPerspectiveClipContext context = perspective_context();
    const CameraMatrix world = identity_matrix();
    const float positive_infinity = std::numeric_limits<float>::infinity();
    bool passed = true;
    for (const CameraPrecision precision : {CameraPrecision::Single, CameraPrecision::Double}) {
        passed = check(
                     nn_clip_sphere({{0.0f, 0.0f, -5.0f}}, 0.0f, world, context, precision) == 2u,
                     "zero-radius inside point differs") &&
                 passed;
        passed = check(
                     nn_clip_sphere({{1.25f, 0.0f, -5.0f}}, 0.0f, world, context, precision) == 2u,
                     "zero-radius side tangent differs") &&
                 passed;
        passed = check(
                     nn_clip_sphere(
                         {{std::nextafter(1.25f, positive_infinity), 0.0f, -5.0f}},
                         0.0f,
                         world,
                         context,
                         precision) == 0x10u,
                     "zero-radius side exterior point was not outside") &&
                 passed;
        passed = check(
                     nn_clip_sphere({{0.0f, 0.0f, -1.0f}}, 0.0f, world, context, precision) == 2u,
                     "zero-radius near tangent differs") &&
                 passed;
        passed = check(
                     nn_clip_sphere(
                         {{0.0f, 0.0f, std::nextafter(-1.0f, positive_infinity)}},
                         0.0f,
                         world,
                         context,
                         precision) == 0x10u,
                     "zero-radius near exterior point was not outside") &&
                 passed;
        passed = check(
                     nn_clip_sphere({{0.0f, 0.0f, -10.0f}}, 0.0f, world, context, precision) == 2u,
                     "zero-radius far tangent differs") &&
                 passed;
        passed = check(
                     nn_clip_sphere(
                         {{0.0f, 0.0f, std::nextafter(-10.0f, -positive_infinity)}},
                         0.0f,
                         world,
                         context,
                         precision) == 0x10u,
                     "zero-radius far exterior point was not outside") &&
                 passed;
    }
    return passed;
}

bool test_sphere_transforms_center_without_scaling_radius() {
    const NnPerspectiveClipContext context = perspective_context();
    const CameraMatrix world = {{
        -2.0f, 0.0f, 0.0f, 0.0f,
        0.5f, 0.2f, 0.0f, 0.0f,
        0.0f, 0.0f, 1.0f, 0.0f,
        3.0f, 0.0f, -5.0f, 1.0f,
    }};
    const std::array<float, 3u> local_center = {{1.0f, 1.0f, 0.0f}};
    const std::array<float, 3u> transformed_center = {{1.5f, 0.2f, -5.0f}};
    bool passed = true;
    for (const CameraPrecision precision : {CameraPrecision::Single, CameraPrecision::Double}) {
        passed = check(
                     nn_clip_sphere(
                         {{0.0f, 0.0f, 0.0f}},
                         0.5f,
                         translation_matrix(0.0f, 0.0f, -5.0f),
                         context,
                         precision) == 2u,
                     "translation did not transform the sphere center") &&
                 passed;
        passed = check(
                     nn_clip_sphere(local_center, 0.5f, world, context, precision) == 0x10u,
                     "reflection/shear transform implicitly scaled the sphere radius") &&
                 passed;
        passed = check(
                     nn_clip_sphere(transformed_center, 0.5f, identity_matrix(), context, precision) == 0x10u,
                     "reflection/shear transform did not match its transformed center") &&
                 passed;
    }
    return passed;
}

bool test_sphere_precision_changes_a_near_boundary_decision() {
    const NnPerspectiveClipContext context = {
        1.0f,
        100.0f,
        {{1.0f, 1.0f}},
        {{-1.0f, 1.0f}},
        {{1.0f, 1.0f}},
        {{-1.0f, 1.0f}},
    };
    const float radius = from_bits(0x33d00000u);
    const std::array<float, 3u> center = {{0.0f, 0.0f, from_bits(0xbf7ffffeu)}};
    return check(
               nn_clip_sphere(center, radius, identity_matrix(), context, CameraPrecision::Single) == 4u,
               "single precision did not retain the near-boundary sphere") &&
           check(
               nn_clip_sphere(center, radius, identity_matrix(), context, CameraPrecision::Double) == 0x10u,
               "double precision did not reject the near-boundary sphere");
}

bool test_sphere_rejects_invalid_domains() {
    const NnPerspectiveClipContext context = perspective_context();
    const CameraMatrix identity = identity_matrix();
    const std::array<float, 3u> center = {{0.0f, 0.0f, -5.0f}};
    const CameraPrecision invalid_precision = static_cast<CameraPrecision>(99);
    const float nan = std::numeric_limits<float>::quiet_NaN();
    const float outside_limit = std::nextafter(
        16777216.0f,
        std::numeric_limits<float>::infinity());
    bool passed = true;

    passed = expects_invalid_argument(
                 [&]() {
                     nn_clip_sphere(center, 0.5f, identity, context, invalid_precision);
                 },
                 "sphere classifier accepted an unsupported precision") &&
             passed;
    passed = expects_invalid_argument(
                 [&]() {
                     nn_clip_sphere({{nan, 0.0f, -5.0f}}, 0.5f, identity, context, CameraPrecision::Single);
                 },
                 "sphere classifier accepted a nonfinite center") &&
             passed;
    passed = expects_invalid_argument(
                 [&]() {
                     nn_clip_sphere(center, nan, identity, context, CameraPrecision::Single);
                 },
                 "sphere classifier accepted a nonfinite radius") &&
             passed;
    passed = expects_invalid_argument(
                 [&]() {
                     nn_clip_sphere(center, -0.5f, identity, context, CameraPrecision::Double);
                 },
                 "sphere classifier accepted a negative radius") &&
             passed;
    passed = expects_invalid_argument(
                 [&]() {
                     nn_clip_sphere(center, outside_limit, identity, context, CameraPrecision::Double);
                 },
                 "sphere classifier accepted an out-of-range radius") &&
             passed;

    NnPerspectiveClipContext invalid_context = context;
    invalid_context.far_plane = invalid_context.near_plane;
    passed = expects_invalid_argument(
                 [&]() {
                     nn_clip_sphere(center, 0.5f, identity, invalid_context, CameraPrecision::Single);
                 },
                 "sphere classifier accepted an invalid depth range") &&
             passed;

    CameraMatrix nonaffine = identity;
    nonaffine[7u] = 1.0f;
    passed = expects_invalid_argument(
                 [&]() {
                     nn_clip_sphere(center, 0.5f, nonaffine, context, CameraPrecision::Single);
                 },
                 "sphere classifier accepted a nonaffine node-world matrix") &&
             passed;

    CameraMatrix nonfinite = identity;
    nonfinite[14u] = nan;
    return expects_invalid_argument(
               [&]() {
                   nn_clip_sphere(center, 0.5f, nonfinite, context, CameraPrecision::Double);
               },
               "sphere classifier accepted a nonfinite node-world matrix") &&
           passed;
}

bool test_rejects_invalid_active_domains() {
    const NnPerspectiveClipContext context = perspective_context();
    const CameraMatrix identity = identity_matrix();
    const std::array<float, 3u> center = {{0.0f, 0.0f, -5.0f}};
    const std::array<float, 3u> half_extents = {{0.25f, 0.25f, 0.25f}};
    const CameraPrecision invalid_precision = static_cast<CameraPrecision>(99);
    const float nan = std::numeric_limits<float>::quiet_NaN();
    const float outside_limit = std::nextafter(
        16777216.0f,
        std::numeric_limits<float>::infinity());
    bool passed = true;

    passed = expects_invalid_argument(
                 [&]() {
                     nn_clip_box(center, half_extents, identity, context, invalid_precision);
                 },
                 "box classifier accepted an unsupported precision") &&
             passed;
    passed = expects_invalid_argument(
                 [&]() {
                     nn_clip_box({{nan, 0.0f, -5.0f}}, half_extents, identity, context, CameraPrecision::Single);
                 },
                 "box classifier accepted a nonfinite center") &&
             passed;
    passed = expects_invalid_argument(
                 [&]() {
                     nn_clip_box(center, {{-0.25f, 0.25f, 0.25f}}, identity, context, CameraPrecision::Single);
                 },
                 "box classifier accepted a negative half extent") &&
             passed;
    passed = expects_invalid_argument(
                 [&]() {
                     nn_clip_box(
                         {{outside_limit, 0.0f, -5.0f}},
                         half_extents,
                         identity,
                         context,
                         CameraPrecision::Single);
                 },
                 "box classifier accepted an out-of-range center") &&
             passed;

    NnPerspectiveClipContext invalid_near = context;
    invalid_near.near_plane = 0.0f;
    passed = expects_invalid_argument(
                 [&]() {
                     nn_clip_box(center, half_extents, identity, invalid_near, CameraPrecision::Double);
                 },
                 "box classifier accepted a zero near plane") &&
             passed;

    NnPerspectiveClipContext invalid_plane = context;
    invalid_plane.left[1u] = nan;
    passed = expects_invalid_argument(
                 [&]() {
                     nn_clip_box(center, half_extents, identity, invalid_plane, CameraPrecision::Double);
                 },
                 "box classifier accepted a nonfinite side plane") &&
             passed;

    CameraMatrix nonaffine = identity;
    nonaffine[3u] = 1.0f;
    passed = expects_invalid_argument(
                 [&]() {
                     nn_clip_box(center, half_extents, nonaffine, context, CameraPrecision::Single);
                 },
                 "box classifier accepted a nonaffine node-world matrix") &&
             passed;

    CameraMatrix nonfinite_matrix = identity;
    nonfinite_matrix[0u] = nan;
    passed = expects_invalid_argument(
                 [&]() {
                     nn_clip_box(center, half_extents, nonfinite_matrix, context, CameraPrecision::Single);
                 },
                 "box classifier accepted a nonfinite node-world matrix") &&
             passed;

    CameraMatrix overflow_matrix = identity;
    overflow_matrix[0u] = std::numeric_limits<float>::max();
    passed = expects_invalid_argument(
                 [&]() {
                     nn_clip_box(
                         {{2.0f, 0.0f, -5.0f}},
                         half_extents,
                         overflow_matrix,
                         context,
                         CameraPrecision::Double);
                 },
                 "box classifier accepted a nonfinite stored transform") &&
             passed;

    NnNodeData leaf = make_leaf(0x00200000u, 0, center, half_extents);
    passed = expects_invalid_argument(
                 [&]() {
                     nn_clip_box_node_status(
                         leaf, identity, context, 0u, 4u, CameraPrecision::Single);
                 },
                 "leaf status accepted an unsupported flag") &&
             passed;
    passed = expects_invalid_argument(
                 [&]() {
                     nn_clip_box_node_status(
                         leaf, identity, context, 0u, 8u, CameraPrecision::Single);
                 },
                 "leaf status accepted descendant propagation") &&
             passed;
    passed = expects_invalid_argument(
                 [&]() {
                     nn_clip_box_node_status(
                         leaf, identity, context, 0u, 0x20u, CameraPrecision::Single);
                 },
                 "leaf status accepted forced sphere flags") &&
             passed;

    NnNodeData child = leaf;
    child.child_index = 0;
    passed = expects_invalid_argument(
                 [&]() {
                     nn_clip_box_node_status(
                         child, identity, context, 0u, 1u, CameraPrecision::Double);
                 },
                 "leaf status accepted a descendant node") &&
             passed;

    NnNodeData sphere = leaf;
    sphere.flags = 0x00400000u;
    passed = expects_invalid_argument(
                 [&]() {
                     nn_clip_box_node_status(
                         sphere, identity, context, 0u, 1u, CameraPrecision::Double);
                 },
                 "leaf status accepted a sphere route") &&
             passed;

    NnNodeData nonfinite_bounds = leaf;
    nonfinite_bounds.opaque_bits[0u] = 0x7fc00001u;
    passed = expects_invalid_argument(
                 [&]() {
                     nn_clip_box_node_status(
                         nonfinite_bounds, identity, context, 0u, 1u, CameraPrecision::Single);
                 },
                 "leaf status accepted nonfinite active bounds") &&
             passed;

    return expects_invalid_argument(
               [&]() {
                   nn_clip_box_node_status(
                       leaf, identity, context, 0x401u, 1u, invalid_precision);
               },
               "leaf status skipped precision validation") &&
           passed;
}

}

int main() {
    return test_box_masks_cover_all_perspective_faces() &&
                   test_node_world_controls_oriented_visibility() &&
                   test_leaf_status_overlay_preserves_initial_bits() &&
                   test_inactive_leaf_paths_leave_poisoned_geometry_unread() &&
                   test_precision_changes_an_asymmetric_boundary_decision() &&
                   test_sphere_tangencies_cover_all_perspective_faces() &&
                   test_zero_radius_sphere_behaves_as_a_direct_point() &&
                   test_sphere_transforms_center_without_scaling_radius() &&
                   test_sphere_precision_changes_a_near_boundary_decision() &&
                   test_sphere_rejects_invalid_domains() &&
                   test_rejects_invalid_active_domains()
               ? 0
               : 1;
}
