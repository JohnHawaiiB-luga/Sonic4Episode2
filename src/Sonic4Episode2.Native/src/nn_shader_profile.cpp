#include "nn_shader_profile.h"

#include <cstddef>
#include <stdexcept>

namespace {

void require_mask(std::uint32_t value, std::uint32_t mask, const char* message) {
    if ((value & ~mask) != 0u) {
        throw std::invalid_argument(message);
    }
}

void require_maximum(std::uint32_t value, std::uint32_t maximum, const char* message) {
    if (value > maximum) {
        throw std::invalid_argument(message);
    }
}

void write_bits(
    std::array<std::uint8_t, 16u>& key,
    std::size_t first_bit,
    std::size_t bit_count,
    std::uint32_t value) {
    for (std::size_t bit = 0u; bit < bit_count; ++bit) {
        if ((value & (1u << bit)) != 0u) {
            const std::size_t key_bit = first_bit + bit;
            key[key_bit / 8u] |= static_cast<std::uint8_t>(1u << (key_bit % 8u));
        }
    }
}

void validate_input(const NnShaderProfileKeyInput& input) {
    require_mask(input.vertex_features, 0x1fu, "Nn shader profile vertex features are out of range.");
    require_maximum(input.transform_mode, 3u, "Nn shader profile transform mode is out of range.");
    require_mask(input.lighting_features, 0x7u, "Nn shader profile lighting features are out of range.");
    require_maximum(input.parallel_lights, 3u, "Nn shader profile parallel light count is out of range.");
    require_maximum(input.point_lights, 3u, "Nn shader profile point light count is out of range.");
    require_maximum(input.normal_map_type, 3u, "Nn shader profile normal map type is out of range.");
    require_maximum(input.decal_maps, 3u, "Nn shader profile decal map count is out of range.");
    require_mask(input.standard_maps, 0x7fu, "Nn shader profile standard map mask is out of range.");
    require_mask(input.user_maps, 0xffu, "Nn shader profile user map mask is out of range.");
    require_maximum(input.shadow_maps, 3u, "Nn shader profile shadow map count is out of range.");
    require_mask(input.user_samplers, 0x3fu, "Nn shader profile user sampler mask is out of range.");
    for (const std::int32_t coordinate : input.texture_coordinates) {
        if (coordinate < -3 || coordinate > 3) {
            throw std::invalid_argument("Nn shader profile texture coordinate is out of range.");
        }
    }
    require_maximum(input.user_profile, 63u, "Nn shader profile user profile is out of range.");
    require_maximum(input.drawobject_profile, 255u, "Nn shader profile drawobject profile is out of range.");
}

}

std::array<std::uint8_t, 16u> nn_shader_profile_key(const NnShaderProfileKeyInput& input) {
    validate_input(input);

    std::array<std::uint8_t, 16u> key{};
    write_bits(key, 0u, 5u, input.vertex_features);
    write_bits(key, 5u, 2u, input.transform_mode);
    write_bits(key, 7u, 3u, input.lighting_features);
    write_bits(key, 10u, 2u, input.parallel_lights);
    write_bits(key, 12u, 2u, input.point_lights);
    write_bits(key, 14u, 2u, input.normal_map_type);
    write_bits(key, 16u, 1u, input.base_map ? 1u : 0u);
    write_bits(key, 17u, 2u, input.decal_maps);
    write_bits(key, 19u, 7u, input.standard_maps);
    write_bits(key, 26u, 8u, input.user_maps);
    write_bits(key, 34u, 2u, input.shadow_maps);
    write_bits(key, 36u, 6u, input.user_samplers);
    for (std::size_t index = 0u; index < input.texture_coordinates.size(); ++index) {
        const std::uint32_t coordinate = static_cast<std::uint32_t>(input.texture_coordinates[index] + 3);
        write_bits(key, 42u + 3u * index, 3u, coordinate);
    }
    write_bits(key, 66u, 6u, input.user_profile);
    write_bits(key, 72u, 8u, input.drawobject_profile);
    return key;
}
