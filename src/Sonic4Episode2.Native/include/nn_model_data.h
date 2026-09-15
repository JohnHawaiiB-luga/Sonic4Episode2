#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

struct NnVertexAttributes {
    std::array<std::uint32_t, 3u> position_bits;
    std::optional<std::array<std::uint32_t, 3u>> normal_bits;
    std::optional<std::array<std::uint32_t, 2u>> texcoord_bits;
    std::optional<std::array<std::uint8_t, 4u>> diffuse_bgra;
    std::optional<std::array<std::uint8_t, 4u>> specular_bgra;
    std::vector<std::uint32_t> weight_bits;
    std::optional<std::array<std::uint8_t, 4u>> blend_indices;
};

struct NnVertexData {
    std::uint32_t flags;
    std::uint32_t format;
    std::uint32_t fvf;
    std::uint32_t stride;
    std::uint32_t count;
    std::vector<std::uint32_t> matrix_indices;
    std::vector<std::uint8_t> bytes;
    std::vector<NnVertexAttributes> attributes;
};

struct NnPrimitiveData {
    std::uint32_t flags;
    std::uint32_t mode;
    std::vector<std::uint32_t> strip_lengths;
    std::vector<std::uint16_t> indices;
};

struct NnMeshData {
    std::array<std::uint32_t, 4u> bounds_bits;
    std::int32_t node_index;
    std::int32_t matrix_index;
    std::uint32_t material_index;
    std::uint32_t vertex_index;
    std::uint32_t primitive_index;
    std::uint32_t reserved;
};

struct NnSubObjectData {
    std::uint32_t flags;
    std::array<std::uint32_t, 2u> reserved;
    std::vector<NnMeshData> meshes;
};

struct NnNodeData {
    std::uint32_t flags;
    std::int16_t matrix_index;
    std::int16_t parent_index;
    std::int16_t child_index;
    std::int16_t sibling_index;
    std::array<std::uint32_t, 3u> translation_bits;
    std::array<std::int32_t, 3u> rotation_a16;
    std::array<std::uint32_t, 3u> scale_bits;
    std::array<std::uint32_t, 16u> inverse_bind_bits;
    std::array<std::uint32_t, 8u> opaque_bits;
};

struct NnMaterialStateData {
    std::array<std::uint32_t, 7u> words;
};

struct NnMaterialStageData {
    std::vector<std::uint32_t> raw_words;
    std::uint32_t flags;
    std::int32_t texture_index;
};

struct NnMaterialData {
    std::uint32_t pointer_flags;
    std::array<std::uint32_t, 7u> descriptor_bits;
    std::vector<std::array<std::uint32_t, 4u>> color_terms_bits;
    std::optional<NnMaterialStateData> render_state;
    std::vector<NnMaterialStageData> stages;
    std::uint32_t color_flags = 0u;
    std::uint32_t shininess_bits = 0u;
    std::uint32_t specular_intensity_bits = 0u;
    std::optional<std::uint32_t> user_profile;
};

struct NnTextureData {
    std::array<std::uint32_t, 5u> raw_words;
    std::string name;
};

struct NnModelData {
    std::uint32_t flags;
    std::uint32_t version;
    std::uint32_t material_count;
    std::uint32_t node_count;
    std::uint32_t max_node_depth;
    std::uint32_t matrix_palette_count;
    std::uint32_t texture_count;
    std::array<std::uint32_t, 4u> bounds_bits;
    std::array<std::uint32_t, 3u> box_bits;
    std::vector<NnVertexData> vertices;
    std::vector<NnPrimitiveData> primitives;
    std::vector<NnSubObjectData> sub_objects;
    std::optional<std::array<std::uint32_t, 3u>> first_node_translation_bits;
    std::vector<NnNodeData> nodes;
    std::vector<NnMaterialData> materials;
    std::vector<NnTextureData> textures;
};

NnModelData parse_pc_nn_model(const std::uint8_t* data, std::size_t size);
