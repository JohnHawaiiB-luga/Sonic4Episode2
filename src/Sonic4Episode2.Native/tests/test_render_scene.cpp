#include "render_scene.h"

#include <array>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <stdexcept>
#include <utility>
#include <vector>

namespace {

bool check(bool condition, const char* message) {
    if (condition) {
        return true;
    }
    std::fprintf(stderr, "%s\n", message);
    return false;
}

std::uint32_t float_bits(float value) {
    std::uint32_t bits = 0u;
    static_assert(sizeof(bits) == sizeof(value), "float bits require 32-bit floats");
    std::memcpy(&bits, &value, sizeof(bits));
    return bits;
}

NnVertexAttributes make_vertex(
    float x,
    float y,
    float z,
    const std::array<std::uint8_t, 4u>& blend_indices) {
    NnVertexAttributes vertex{};
    vertex.position_bits = {{float_bits(x), float_bits(y), float_bits(z)}};
    vertex.normal_bits = std::array<std::uint32_t, 3u>({{
        float_bits(0.0f), float_bits(0.0f), float_bits(1.0f)}});
    vertex.texcoord_bits = std::array<std::uint32_t, 2u>({{
        float_bits(x * 0.25f), float_bits(y * 0.25f)}});
    vertex.diffuse_bgra = std::array<std::uint8_t, 4u>({{0x30u, 0x20u, 0x10u, 0x40u}});
    vertex.specular_bgra = std::array<std::uint8_t, 4u>({{0x70u, 0x60u, 0x50u, 0x80u}});
    vertex.weight_bits = {float_bits(0.25f), float_bits(0.50f), float_bits(0.25f)};
    vertex.blend_indices = blend_indices;
    return vertex;
}

NnModelData make_skinned_model() {
    NnModelData model{};
    model.material_count = 1u;
    model.node_count = 1u;
    model.matrix_palette_count = 4u;
    model.texture_count = 0u;

    NnNodeData node{};
    node.matrix_index = 0;
    node.parent_index = -1;
    node.child_index = -1;
    node.sibling_index = -1;
    model.nodes = {node};
    model.materials = {NnMaterialData{}};

    NnVertexData vertices{};
    vertices.format = 0x17403u;
    vertices.fvf = 0x111Au;
    vertices.stride = 48u;
    vertices.count = 4u;
    vertices.matrix_indices = {0u, 2u, 3u, 1u};
    vertices.attributes = {
        make_vertex(1.0f, -2.0f, 3.0f, {{0u, 1u, 2u, 3u}}),
        make_vertex(4.0f, 5.0f, 6.0f, {{3u, 2u, 1u, 0u}}),
        make_vertex(-1.0f, 0.0f, 2.0f, {{2u, 1u, 0u, 3u}}),
        make_vertex(8.0f, 9.0f, -7.0f, {{1u, 0u, 3u, 2u}}),
    };
    model.vertices = {std::move(vertices)};

    model.primitives = {{
        0u,
        0x4810u,
        {3u, 3u},
        {0u, 1u, 2u, 2u, 3u, 1u},
    }};

    NnMeshData mesh{{}, 0, -1, 0u, 0u, 0u, 0u};
    model.sub_objects = {{{0u, {{0u, 0u}}, {mesh}}}};
    return model;
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

bool test_expands_established_strips_without_baking_skinning() {
    const NnModelData model = make_skinned_model();
    const std::vector<std::uint32_t> source_weights = model.vertices[0].attributes[0].weight_bits;
    const RenderSceneData scene = build_render_scene(model);
    if (!check(scene.draw_packets.size() == 1u, "render scene did not emit one draw packet")) {
        return false;
    }

    const RenderDrawPacket& packet = scene.draw_packets[0];
    return check(packet.source_vertex_list_index == 0u && packet.source_primitive_list_index == 0u,
                 "render packet lost source list references") &&
           check(packet.material_index == 0u && packet.node_index == 0 && packet.matrix_index == -1,
                 "render packet lost material or matrix references") &&
           check(packet.matrix_subset == std::vector<std::uint32_t>({0u, 2u, 3u, 1u}),
                 "render packet lost palette subset references") &&
           check(packet.vertices.size() == 4u &&
                     packet.indices == std::vector<std::uint32_t>({0u, 1u, 2u, 2u, 3u, 1u}),
                 "render packet did not expand each established strip with local winding") &&
           check(packet.vertices[0].position == std::array<float, 3u>({{1.0f, -2.0f, 3.0f}}),
                 "render packet baked a transform into bind-space positions") &&
           check(packet.vertices[0].normal.has_value() && packet.vertices[0].texcoord.has_value() &&
                     packet.vertices[0].color_rgba.has_value() &&
                     packet.vertices[0].specular_rgba.has_value(),
                 "render packet omitted decoded normal, UV, diffuse color, or specular color") &&
           check(*packet.vertices[0].color_rgba == std::array<std::uint8_t, 4u>({{0x10u, 0x20u, 0x30u, 0x40u}}),
                 "render packet did not convert stored BGRA color to RGBA") &&
           check(*packet.vertices[0].specular_rgba == std::array<std::uint8_t, 4u>({{0x50u, 0x60u, 0x70u, 0x80u}}),
                 "render packet did not preserve the stored specular color") &&
           check(packet.vertices[0].stored_weights == std::vector<float>({0.25f, 0.50f, 0.25f}) &&
                     packet.vertices[0].blend_indices.has_value() &&
                     *packet.vertices[0].blend_indices == std::array<std::uint8_t, 4u>({{0u, 1u, 2u, 3u}}),
                 "render packet lost raw skin weights or UBYTE4 blend indices") &&
           check(model.vertices[0].attributes[0].weight_bits == source_weights,
                 "render scene mutated source vertex attributes");
}

bool test_rejects_invalid_draw_references() {
    NnModelData bad_blend = make_skinned_model();
    bad_blend.vertices[0].attributes[0].blend_indices = std::array<std::uint8_t, 4u>({{0u, 4u, 2u, 3u}});
    if (!expects_invalid_argument(
            [&]() { build_render_scene(bad_blend); },
            "render scene accepted a blend index outside its palette subset")) {
        return false;
    }

    NnModelData mixed_blend = make_skinned_model();
    mixed_blend.vertices[0].attributes[3].blend_indices.reset();
    if (!expects_invalid_argument(
            [&]() { build_render_scene(mixed_blend); },
            "render scene accepted mixed blend-index presence")) {
        return false;
    }

    NnModelData bad_mode = make_skinned_model();
    bad_mode.primitives[0].mode = 0u;
    if (!expects_invalid_argument(
            [&]() { build_render_scene(bad_mode); },
            "render scene accepted an unsupported primitive mode")) {
        return false;
    }

    NnModelData bad_index = make_skinned_model();
    bad_index.primitives[0].indices[5] = 4u;
    return expects_invalid_argument(
        [&]() { build_render_scene(bad_index); },
        "render scene accepted an index outside its source vertex list");
}

}

int main() {
    if (!test_expands_established_strips_without_baking_skinning() ||
        !test_rejects_invalid_draw_references()) {
        return 1;
    }
    return 0;
}
