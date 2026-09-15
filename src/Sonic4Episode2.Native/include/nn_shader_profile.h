#pragma once

#include <array>
#include <cstdint>

struct NnShaderProfileKeyInput {
    std::uint32_t vertex_features = 0u;
    std::uint32_t transform_mode = 0u;
    std::uint32_t lighting_features = 0u;
    std::uint32_t parallel_lights = 0u;
    std::uint32_t point_lights = 0u;
    std::uint32_t normal_map_type = 0u;
    bool base_map = false;
    std::uint32_t decal_maps = 0u;
    std::uint32_t standard_maps = 0u;
    std::uint32_t user_maps = 0u;
    std::uint32_t shadow_maps = 0u;
    std::uint32_t user_samplers = 0u;
    std::array<std::int32_t, 8u> texture_coordinates{};
    std::uint32_t user_profile = 0u;
    std::uint32_t drawobject_profile = 0u;
};

std::array<std::uint8_t, 16u> nn_shader_profile_key(const NnShaderProfileKeyInput& input);
