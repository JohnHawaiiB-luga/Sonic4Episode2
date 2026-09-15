#include "render_scene.h"

#include <cstring>
#include <limits>
#include <stdexcept>

namespace {

constexpr std::uint32_t kTriangleStripMode = 0x4810u;

[[noreturn]] void fail(const char* message) {
    throw std::invalid_argument(message);
}

float decode_float(std::uint32_t bits) {
    float value = 0.0f;
    static_assert(sizeof(value) == sizeof(bits), "render vertex floats require 32-bit storage");
    std::memcpy(&value, &bits, sizeof(value));
    return value;
}

RenderVertexData make_render_vertex(const NnVertexAttributes& attributes) {
    RenderVertexData vertex{{
        decode_float(attributes.position_bits[0]),
        decode_float(attributes.position_bits[1]),
        decode_float(attributes.position_bits[2]),
    }, std::nullopt, std::nullopt, std::nullopt, std::nullopt, {}, attributes.blend_indices};
    if (attributes.normal_bits.has_value()) {
        vertex.normal = std::array<float, 3u>({{
            decode_float((*attributes.normal_bits)[0]),
            decode_float((*attributes.normal_bits)[1]),
            decode_float((*attributes.normal_bits)[2]),
        }});
    }
    if (attributes.texcoord_bits.has_value()) {
        vertex.texcoord = std::array<float, 2u>({{
            decode_float((*attributes.texcoord_bits)[0]),
            decode_float((*attributes.texcoord_bits)[1]),
        }});
    }
    if (attributes.diffuse_bgra.has_value()) {
        vertex.color_rgba = std::array<std::uint8_t, 4u>({{
            (*attributes.diffuse_bgra)[2],
            (*attributes.diffuse_bgra)[1],
            (*attributes.diffuse_bgra)[0],
            (*attributes.diffuse_bgra)[3],
        }});
    }
    if (attributes.specular_bgra.has_value()) {
        vertex.specular_rgba = std::array<std::uint8_t, 4u>({{
            (*attributes.specular_bgra)[2],
            (*attributes.specular_bgra)[1],
            (*attributes.specular_bgra)[0],
            (*attributes.specular_bgra)[3],
        }});
    }
    vertex.stored_weights.reserve(attributes.weight_bits.size());
    for (std::uint32_t bits : attributes.weight_bits) {
        vertex.stored_weights.push_back(decode_float(bits));
    }
    return vertex;
}

void validate_vertex_list(const NnVertexData& vertices, std::size_t matrix_palette_count) {
    if (vertices.count == 0u || vertices.attributes.size() != static_cast<std::size_t>(vertices.count)) {
        fail("render vertex attributes do not match the declared vertex count");
    }
    bool has_weights = false;
    bool has_blend_indices = false;
    for (const NnVertexAttributes& attributes : vertices.attributes) {
        if (!attributes.weight_bits.empty()) {
            has_weights = true;
            if (attributes.weight_bits.size() != 3u) {
                fail("render vertex has an unsupported stored-weight count");
            }
        }
        if (attributes.blend_indices.has_value()) {
            has_blend_indices = true;
        }
    }
    if (!has_weights) {
        if (!vertices.matrix_indices.empty() || has_blend_indices) {
            fail("render rigid vertex list has skin references");
        }
        return;
    }
    if (vertices.matrix_indices.empty() || vertices.matrix_indices.size() > 16u ||
        (!has_blend_indices && vertices.matrix_indices.size() > 4u)) {
        fail("render weighted vertex list has an unsupported palette subset");
    }
    for (std::uint32_t matrix_index : vertices.matrix_indices) {
        if (static_cast<std::size_t>(matrix_index) >= matrix_palette_count) {
            fail("render weighted palette subset index is out of range");
        }
    }
    for (const NnVertexAttributes& attributes : vertices.attributes) {
        if (attributes.weight_bits.empty()) {
            fail("render vertex list mixes weighted and rigid vertices");
        }
        if (has_blend_indices && !attributes.blend_indices.has_value()) {
            fail("render vertex list mixes blend-index layouts");
        }
        if (attributes.blend_indices.has_value()) {
            for (std::uint8_t blend_index : *attributes.blend_indices) {
                if (static_cast<std::size_t>(blend_index) >= vertices.matrix_indices.size()) {
                    fail("render blend index is outside its palette subset");
                }
            }
        }
    }
}

void append_strip_triangles(
    const NnPrimitiveData& primitive,
    std::size_t vertex_count,
    std::vector<std::uint32_t>& output) {
    if (primitive.mode != kTriangleStripMode || primitive.strip_lengths.empty()) {
        fail("render primitive mode or strip list is unsupported");
    }
    std::size_t total = 0u;
    for (std::uint32_t length : primitive.strip_lengths) {
        const std::size_t item_count = static_cast<std::size_t>(length);
        if (item_count > std::numeric_limits<std::size_t>::max() - total) {
            fail("render strip lengths overflow");
        }
        total += item_count;
    }
    if (total != primitive.indices.size()) {
        fail("render strip lengths do not match index data");
    }
    std::size_t offset = 0u;
    for (std::uint32_t length : primitive.strip_lengths) {
        const std::size_t strip_length = static_cast<std::size_t>(length);
        for (std::size_t item = 0u; item < strip_length; ++item) {
            if (static_cast<std::size_t>(primitive.indices[offset + item]) >= vertex_count) {
                fail("render index is outside its source vertex list");
            }
        }
        for (std::size_t triangle = 0u; triangle + 2u < strip_length; ++triangle) {
            const std::uint32_t first = primitive.indices[offset + triangle];
            const std::uint32_t second = primitive.indices[offset + triangle + 1u];
            const std::uint32_t third = primitive.indices[offset + triangle + 2u];
            if (first == second || second == third || first == third) {
                continue;
            }
            output.push_back(first);
            if ((triangle & 1u) == 0u) {
                output.push_back(second);
                output.push_back(third);
            } else {
                output.push_back(third);
                output.push_back(second);
            }
        }
        offset += strip_length;
    }
}

}

RenderSceneData build_render_scene(const NnModelData& model) {
    if (model.node_count != model.nodes.size() ||
        model.material_count != model.materials.size() ||
        model.texture_count != model.textures.size()) {
        fail("render model record counts do not match their declared header values");
    }
    for (const NnVertexData& vertices : model.vertices) {
        validate_vertex_list(vertices, static_cast<std::size_t>(model.matrix_palette_count));
    }

    RenderSceneData scene{};
    for (std::size_t sub_object_index = 0u; sub_object_index < model.sub_objects.size(); ++sub_object_index) {
        const NnSubObjectData& sub_object = model.sub_objects[sub_object_index];
        for (std::size_t mesh_index = 0u; mesh_index < sub_object.meshes.size(); ++mesh_index) {
            const NnMeshData& mesh = sub_object.meshes[mesh_index];
            if (mesh.node_index < 0 || static_cast<std::size_t>(mesh.node_index) >= model.nodes.size() ||
                mesh.matrix_index < -1 ||
                (mesh.matrix_index >= 0 &&
                 static_cast<std::size_t>(mesh.matrix_index) >= model.matrix_palette_count) ||
                static_cast<std::size_t>(mesh.material_index) >= model.materials.size() ||
                static_cast<std::size_t>(mesh.vertex_index) >= model.vertices.size() ||
                static_cast<std::size_t>(mesh.primitive_index) >= model.primitives.size()) {
                fail("render mesh reference is out of range");
            }
            const NnVertexData& vertices = model.vertices[mesh.vertex_index];
            const NnPrimitiveData& primitive = model.primitives[mesh.primitive_index];
            RenderDrawPacket packet{
                static_cast<std::uint32_t>(sub_object_index),
                static_cast<std::uint32_t>(mesh_index),
                mesh.vertex_index,
                mesh.primitive_index,
                mesh.material_index,
                mesh.node_index,
                mesh.matrix_index,
                vertices.matrix_indices,
                {},
                {},
            };
            packet.vertices.reserve(vertices.attributes.size());
            for (const NnVertexAttributes& attributes : vertices.attributes) {
                packet.vertices.push_back(make_render_vertex(attributes));
            }
            append_strip_triangles(
                primitive,
                static_cast<std::size_t>(vertices.count),
                packet.indices);
            scene.draw_packets.push_back(std::move(packet));
        }
    }
    return scene;
}
