#include "nn_shader_profile.h"

#include <array>
#include <cstdint>
#include <cstdio>
#include <exception>
#include <stdexcept>

namespace {

bool check(bool condition, const char* message) {
    if (condition) {
        return true;
    }
    std::fprintf(stderr, "%s\n", message);
    return false;
}

bool same_input(const NnShaderProfileKeyInput& left, const NnShaderProfileKeyInput& right) {
    return left.vertex_features == right.vertex_features &&
           left.transform_mode == right.transform_mode &&
           left.lighting_features == right.lighting_features &&
           left.parallel_lights == right.parallel_lights &&
           left.point_lights == right.point_lights &&
           left.normal_map_type == right.normal_map_type &&
           left.base_map == right.base_map &&
           left.decal_maps == right.decal_maps &&
           left.standard_maps == right.standard_maps &&
           left.user_maps == right.user_maps &&
           left.shadow_maps == right.shadow_maps &&
           left.user_samplers == right.user_samplers &&
           left.texture_coordinates == right.texture_coordinates &&
           left.user_profile == right.user_profile &&
           left.drawobject_profile == right.drawobject_profile;
}

bool check_key(
    const NnShaderProfileKeyInput& input,
    const std::array<std::uint8_t, 16u>& expected,
    const char* message) {
    const NnShaderProfileKeyInput original = input;
    const std::array<std::uint8_t, 16u> actual = nn_shader_profile_key(input);
    return check(actual == expected, message) &&
           check(same_input(input, original), "shader profile key packing modified its input");
}

bool test_known_keys() {
    const std::array<std::uint8_t, 16u> zero_expected = {{
        0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x6cu, 0xdbu, 0xb6u,
        0x01u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u,
    }};
    const std::array<std::uint8_t, 16u> vertex_and_base_expected = {{
        0x03u, 0x00u, 0x01u, 0x00u, 0x00u, 0x6cu, 0xdbu, 0xb6u,
        0x01u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u,
    }};

    NnShaderProfileKeyInput vertex_and_base{};
    vertex_and_base.vertex_features = 3u;
    vertex_and_base.base_map = true;
    return check_key(NnShaderProfileKeyInput{}, zero_expected, "zero shader profile key mismatch") &&
           check_key(vertex_and_base, vertex_and_base_expected, "vertex/base shader profile key mismatch");
}

bool test_coordinate_bias_and_cross_byte_packing() {
    const std::array<std::uint8_t, 16u> expected = {{
        0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x20u, 0x1au, 0x6bu,
        0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u,
    }};
    NnShaderProfileKeyInput input{};
    input.texture_coordinates = {{-3, -2, -1, 0, 1, 2, 3, -3}};
    return check_key(input, expected, "coordinate bias or cross-byte shader profile packing mismatch");
}

bool test_all_semantic_groups() {
    const std::array<std::uint8_t, 16u> expected = {{
        0xffu, 0xffu, 0xffu, 0xffu, 0xffu, 0x23u, 0x1au, 0x6bu,
        0xfcu, 0xffu, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u,
    }};
    NnShaderProfileKeyInput input{};
    input.vertex_features = 0x1fu;
    input.transform_mode = 3u;
    input.lighting_features = 0x7u;
    input.parallel_lights = 3u;
    input.point_lights = 3u;
    input.normal_map_type = 3u;
    input.base_map = true;
    input.decal_maps = 3u;
    input.standard_maps = 0x7fu;
    input.user_maps = 0xffu;
    input.shadow_maps = 3u;
    input.user_samplers = 0x3fu;
    input.texture_coordinates = {{-3, -2, -1, 0, 1, 2, 3, -3}};
    input.user_profile = 0x3fu;
    input.drawobject_profile = 0xffu;
    return check_key(input, expected, "full semantic shader profile key mismatch");
}

template <typename Mutate>
bool rejects_invalid_input(Mutate&& mutate, const char* message) {
    NnShaderProfileKeyInput input{};
    mutate(input);
    const NnShaderProfileKeyInput original = input;
    try {
        static_cast<void>(nn_shader_profile_key(input));
    } catch (const std::invalid_argument&) {
        return check(same_input(input, original), "rejected shader profile input was modified");
    } catch (const std::exception&) {
        return check(false, message);
    }
    return check(false, message);
}

bool test_rejects_out_of_domain_inputs() {
    return rejects_invalid_input(
               [](NnShaderProfileKeyInput& input) { input.vertex_features = 0x20u; },
               "out-of-domain vertex feature mask was accepted") &&
           rejects_invalid_input(
               [](NnShaderProfileKeyInput& input) { input.transform_mode = 4u; },
               "out-of-domain transform mode was accepted") &&
           rejects_invalid_input(
               [](NnShaderProfileKeyInput& input) { input.lighting_features = 0x8u; },
               "out-of-domain lighting feature mask was accepted") &&
           rejects_invalid_input(
               [](NnShaderProfileKeyInput& input) { input.parallel_lights = 4u; },
               "out-of-domain parallel light count was accepted") &&
           rejects_invalid_input(
               [](NnShaderProfileKeyInput& input) { input.point_lights = 4u; },
               "out-of-domain point light count was accepted") &&
           rejects_invalid_input(
               [](NnShaderProfileKeyInput& input) { input.normal_map_type = 4u; },
               "out-of-domain normal map type was accepted") &&
           rejects_invalid_input(
               [](NnShaderProfileKeyInput& input) { input.decal_maps = 4u; },
               "out-of-domain decal map count was accepted") &&
           rejects_invalid_input(
               [](NnShaderProfileKeyInput& input) { input.standard_maps = 0x80u; },
               "out-of-domain standard map mask was accepted") &&
           rejects_invalid_input(
               [](NnShaderProfileKeyInput& input) { input.user_maps = 0x100u; },
               "out-of-domain user map mask was accepted") &&
           rejects_invalid_input(
               [](NnShaderProfileKeyInput& input) { input.shadow_maps = 4u; },
               "out-of-domain shadow map count was accepted") &&
           rejects_invalid_input(
               [](NnShaderProfileKeyInput& input) { input.user_samplers = 0x40u; },
               "out-of-domain user sampler mask was accepted") &&
           rejects_invalid_input(
               [](NnShaderProfileKeyInput& input) { input.texture_coordinates[0u] = -4; },
               "low out-of-domain texture coordinate was accepted") &&
           rejects_invalid_input(
               [](NnShaderProfileKeyInput& input) { input.texture_coordinates[7u] = 4; },
               "high out-of-domain texture coordinate was accepted") &&
           rejects_invalid_input(
               [](NnShaderProfileKeyInput& input) { input.user_profile = 64u; },
               "out-of-domain user profile was accepted") &&
           rejects_invalid_input(
               [](NnShaderProfileKeyInput& input) { input.drawobject_profile = 256u; },
               "out-of-domain drawobject profile was accepted");
}

} // namespace

int main() {
    return test_known_keys() &&
                   test_coordinate_bias_and_cross_byte_packing() &&
                   test_all_semantic_groups() &&
                   test_rejects_out_of_domain_inputs()
               ? 0
               : 1;
}
