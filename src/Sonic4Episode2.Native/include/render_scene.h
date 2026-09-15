#pragma once

#include "nn_model_data.h"

#include <array>
#include <cstdint>
#include <optional>
#include <vector>

struct RenderVertexData {
    std::array<float, 3u> position;
    std::optional<std::array<float, 3u>> normal;
    std::optional<std::array<float, 2u>> texcoord;
    std::optional<std::array<std::uint8_t, 4u>> color_rgba;
    std::optional<std::array<std::uint8_t, 4u>> specular_rgba;
    std::vector<float> stored_weights;
    std::optional<std::array<std::uint8_t, 4u>> blend_indices;
};

struct RenderDrawPacket {
    std::uint32_t source_sub_object_index;
    std::uint32_t source_mesh_index;
    std::uint32_t source_vertex_list_index;
    std::uint32_t source_primitive_list_index;
    std::uint32_t material_index;
    std::int32_t node_index;
    std::int32_t matrix_index;
    std::vector<std::uint32_t> matrix_subset;
    std::vector<RenderVertexData> vertices;
    std::vector<std::uint32_t> indices;
};

struct RenderSceneData {
    std::vector<RenderDrawPacket> draw_packets;
};

RenderSceneData build_render_scene(const NnModelData& model);
