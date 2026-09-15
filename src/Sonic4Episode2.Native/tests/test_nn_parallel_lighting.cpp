#include "nn_parallel_lighting.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <stdexcept>
#include <utility>

namespace {

using MatrixBits = std::array<std::uint32_t, 16u>;
using RawLightBits = std::array<std::uint32_t, 8u>;
using LightInputs = std::array<RawLightBits, 8u>;
using LightOutputBits = std::array<std::uint32_t, 8u>;
using OutputBits = std::array<LightOutputBits, 4u>;

const MatrixBits kIdentityMatrix = {{
    0x3F800000u, 0x00000000u, 0x00000000u, 0x00000000u,
    0x00000000u, 0x3F800000u, 0x00000000u, 0x00000000u,
    0x00000000u, 0x00000000u, 0x3F800000u, 0x00000000u,
    0x00000000u, 0x00000000u, 0x00000000u, 0x3F800000u,
}};

const MatrixBits kQuarterTurnMatrix = {{
    0x00000000u, 0x3F800000u, 0x00000000u, 0x00000000u,
    0xBF800000u, 0x00000000u, 0x00000000u, 0x00000000u,
    0x00000000u, 0x00000000u, 0x3F800000u, 0x00000000u,
    0x42C80000u, 0xC3480000u, 0x43960000u, 0x3F800000u,
}};

const MatrixBits kMixedNonunitMatrix = {{
    0x402DA466u, 0x3E616AB1u, 0xBFC34ED2u, 0x3F8431DFu,
    0x403B4C2Au, 0xBECA292Au, 0xC0357EE7u, 0xC00EBE46u,
    0xC03B90FCu, 0x3FAA4958u, 0x3F10C0E5u, 0x3EC2E966u,
    0xC0356F1Fu, 0x400815E0u, 0xBF9B3290u, 0x3FEFCA89u,
}};

const LightInputs kPreset0SingleLights = {{
    {{0x3F800000u, 0x3F800000u, 0x3F800000u, 0x3F800000u, 0x3F333334u, 0x3EF85B43u, 0xBEF85B43u, 0xBF3A4472u}},
    {{0x3F800000u, 0x3F800000u, 0x3F800000u, 0x3F800000u, 0x3ECCCCCDu, 0xBDC7DD45u, 0xBE47DD45u, 0xBF79D496u}},
    {{0x3F4CCCCCu, 0x3F4CCCCCu, 0x3F800000u, 0x3F800000u, 0x3F19999Au, 0x3F11DE9Au, 0xBF2F0B20u, 0xBEE9642Au}},
    {{0x3F666666u, 0x3FCCCCCEu, 0x3FB33334u, 0x3F800001u, 0x3E4CCCCEu, 0xBE90781Bu, 0x80000000u, 0xBF7598FBu}},
    {{0x3F800000u, 0x3F800000u, 0x3F800000u, 0x3F800000u, 0x3F7FFFFEu, 0x3F17A42Du, 0xBF26CE32u, 0x3EF2A048u}},
    {{0x3F800000u, 0x3F800000u, 0x3F800000u, 0x3F800000u, 0x3DCCCCCFu, 0x3DC7DD45u, 0xBE47DD45u, 0xBF79D496u}},
    {{0x3F800000u, 0x3F800000u, 0x3F800000u, 0x3F800000u, 0x3FC00000u, 0x3F3504F4u, 0xBF3504F4u, 0x00000000u}},
    {{0x3F800000u, 0x3F800000u, 0x3F800000u, 0x3F800000u, 0x3F800000u, 0xBF13CD3Bu, 0xBF13CD3Bu, 0xBF13CD3Bu}},
}};

const LightInputs kPreset0DoubleLights = {{
    {{0x3F800000u, 0x3F800000u, 0x3F800000u, 0x3F800000u, 0x3F333334u, 0x3EF85B42u, 0xBEF85B42u, 0xBF3A4472u}},
    {{0x3F800000u, 0x3F800000u, 0x3F800000u, 0x3F800000u, 0x3ECCCCCDu, 0xBDC7DD45u, 0xBE47DD45u, 0xBF79D496u}},
    {{0x3F4CCCCCu, 0x3F4CCCCCu, 0x3F800000u, 0x3F800000u, 0x3F19999Au, 0x3F11DE9Au, 0xBF2F0B20u, 0xBEE9642Au}},
    {{0x3F666666u, 0x3FCCCCCEu, 0x3FB33334u, 0x3F800001u, 0x3E4CCCCEu, 0xBE90781Bu, 0x80000000u, 0xBF7598FBu}},
    {{0x3F800000u, 0x3F800000u, 0x3F800000u, 0x3F800000u, 0x3F7FFFFEu, 0x3F17A42Du, 0xBF26CE32u, 0x3EF2A048u}},
    {{0x3F800000u, 0x3F800000u, 0x3F800000u, 0x3F800000u, 0x3DCCCCCFu, 0x3DC7DD45u, 0xBE47DD45u, 0xBF79D496u}},
    {{0x3F800000u, 0x3F800000u, 0x3F800000u, 0x3F800000u, 0x3FC00000u, 0x3F3504F3u, 0xBF3504F3u, 0x00000000u}},
    {{0x3F800000u, 0x3F800000u, 0x3F800000u, 0x3F800000u, 0x3F800000u, 0xBF13CD3Au, 0xBF13CD3Au, 0xBF13CD3Au}},
}};

const LightInputs kMixedNonunitLights = {{
    {{0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u}},
    {{0x3FFABE4Eu, 0xBE4A932Du, 0xBBE8761Du, 0x3FCA2B1Du, 0x00000000u, 0x3EC2E6CAu, 0x3E90E9CCu, 0x3AE027EBu}},
    {{0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u}},
    {{0x3F045110u, 0x3FA403CBu, 0x3F89A076u, 0x3F604DCEu, 0x00000000u, 0x41A66787u, 0xC2664971u, 0xBB5521F9u}},
    {{0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u}},
    {{0x3F1D698Au, 0x3F02775Fu, 0x3FF133A1u, 0x3EBE27E4u, 0x00000000u, 0x42A3C04Du, 0xBD1AF28Cu, 0xB8311AAAu}},
    {{0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u}},
    {{0xBF5AE6F6u, 0x3E98CF64u, 0x3F98C0F4u, 0x3F5443EEu, 0x3F800000u, 0x3E971C64u, 0xBFCD03A2u, 0xBBFBB63Fu}},
}};

const LightOutputBits kIdentitySingleLight0 = {{
    0x3F333334u, 0x3F333334u, 0x3F333334u, 0x3F800000u,
    0xBEF85B43u, 0x3EF85B43u, 0x3F3A4472u, 0x00000000u,
}};

const LightOutputBits kIdentitySingleLight1 = {{
    0x3ECCCCCDu, 0x3ECCCCCDu, 0x3ECCCCCDu, 0x3F800000u,
    0x3DC7DD47u, 0x3E47DD47u, 0x3F79D498u, 0x00000000u,
}};

const LightOutputBits kIdentityDoubleLight0 = {{
    0x3F333334u, 0x3F333334u, 0x3F333334u, 0x3F800000u,
    0xBEF85B42u, 0x3EF85B42u, 0x3F3A4472u, 0x00000000u,
}};

const LightOutputBits kIdentityDoubleLight1 = {{
    0x3ECCCCCDu, 0x3ECCCCCDu, 0x3ECCCCCDu, 0x3F800000u,
    0x3DC7DD45u, 0x3E47DD45u, 0x3F79D496u, 0x00000000u,
}};

const LightOutputBits kQuarterTurnSingleLight0 = {{
    0x3F333334u, 0x3F333334u, 0x3F333334u, 0x3F800000u,
    0xBEF85B43u, 0xBEF85B43u, 0x3F3A4472u, 0x00000000u,
}};

const LightOutputBits kQuarterTurnSingleLight1 = {{
    0x3ECCCCCDu, 0x3ECCCCCDu, 0x3ECCCCCDu, 0x3F800000u,
    0xBE47DD47u, 0x3DC7DD47u, 0x3F79D498u, 0x00000000u,
}};

const LightOutputBits kQuarterTurnDoubleLight0 = {{
    0x3F333334u, 0x3F333334u, 0x3F333334u, 0x3F800000u,
    0xBEF85B42u, 0xBEF85B42u, 0x3F3A4472u, 0x00000000u,
}};

const LightOutputBits kQuarterTurnDoubleLight1 = {{
    0x3ECCCCCDu, 0x3ECCCCCDu, 0x3ECCCCCDu, 0x3F800000u,
    0xBE47DD45u, 0x3DC7DD45u, 0x3F79D496u, 0x00000000u,
}};

const LightOutputBits kHighSlotLight = {{
    0x3F800000u, 0x3F800000u, 0x3F800000u, 0x3F800000u,
    0x3F13CD3Au, 0x3F13CD3Au, 0x3F13CD3Au, 0x00000000u,
}};

const LightOutputBits kCapLight2 = {{
    0x3EF5C28Fu, 0x3EF5C28Fu, 0x3F19999Au, 0x3F800000u,
    0xBF11DE9Au, 0x3F2F0B20u, 0x3EE9642Au, 0x00000000u,
}};

const LightOutputBits kCapLight3 = {{
    0x3E3851ECu, 0x3EA3D70Cu, 0x3E8F5C2Au, 0x3F800001u,
    0x3E90781Bu, 0x00000000u, 0x3F7598FBu, 0x00000000u,
}};

const OutputBits kMixedNonunitSingle = {{
    {{0x00000000u, 0x80000000u, 0x80000000u, 0x3FCA2B1Du, 0xBF4D4B35u, 0x3C35CB36u, 0x3F18E9BDu, 0x00000000u}},
    {{0x00000000u, 0x00000000u, 0x00000000u, 0x3F604DCEu, 0x3F23F8D9u, 0xBE1FDA74u, 0xBF407D3Eu, 0x00000000u}},
    {{0x00000000u, 0x00000000u, 0x00000000u, 0x3EBE27E4u, 0xBF5E9898u, 0xBD90ACC5u, 0x3EFA478Du, 0x00000000u}},
    {{0xBF5AE6F6u, 0x3E98CF64u, 0x3F98C0F4u, 0x3F5443EEu, 0x3F2E9102u, 0xBDF859F3u, 0xBF38A8A2u, 0x00000000u}},
}};

const OutputBits kMixedNonunitDouble = {{
    {{0x00000000u, 0x80000000u, 0x80000000u, 0x3FCA2B1Du, 0xBF4D4B35u, 0x3C35CB38u, 0x3F18E9BDu, 0x00000000u}},
    {{0x00000000u, 0x00000000u, 0x00000000u, 0x3F604DCEu, 0x3F23F8D9u, 0xBE1FDA73u, 0xBF407D3Eu, 0x00000000u}},
    {{0x00000000u, 0x00000000u, 0x00000000u, 0x3EBE27E4u, 0xBF5E9896u, 0xBD90ACC3u, 0x3EFA478Bu, 0x00000000u}},
    {{0xBF5AE6F6u, 0x3E98CF64u, 0x3F98C0F4u, 0x3F5443EEu, 0x3F2E9102u, 0xBDF859F3u, 0xBF38A8A2u, 0x00000000u}},
}};

bool check(bool condition, const char* message) {
    if (!condition) {
        std::fprintf(stderr, "%s\n", message);
    }
    return condition;
}

template <typename Callable>
bool expects_invalid_argument(Callable&& callable, const char* message) {
    try {
        std::forward<Callable>(callable)();
    } catch (const std::invalid_argument&) {
        return true;
    } catch (...) {
        return check(false, "wrong parallel lighting exception type");
    }
    return check(false, message);
}

float float_from_bits(std::uint32_t bits) {
    static_assert(sizeof(float) == sizeof(bits), "parallel lighting requires 32-bit floats");
    float value = 0.0f;
    std::memcpy(&value, &bits, sizeof(value));
    return value;
}

std::uint32_t float_bits(float value) {
    std::uint32_t bits = 0u;
    std::memcpy(&bits, &value, sizeof(bits));
    return bits;
}

StageParallelLight light_from_bits(const RawLightBits& bits) {
    StageParallelLight result{};
    for (std::size_t index = 0u; index < result.color_bits.size(); ++index) {
        result.color_bits[index] = bits[index];
    }
    result.intensity_bits = bits[4u];
    for (std::size_t index = 0u; index < result.direction_bits.size(); ++index) {
        result.direction_bits[index] = bits[5u + index];
    }
    return result;
}

std::array<StageParallelLight, 8u> lights_from_bits(const LightInputs& bits) {
    std::array<StageParallelLight, 8u> result{};
    for (std::size_t index = 0u; index < result.size(); ++index) {
        result[index] = light_from_bits(bits[index]);
    }
    return result;
}

LightInputs light_bits(const std::array<StageParallelLight, 8u>& lights) {
    LightInputs result{};
    for (std::size_t light = 0u; light < lights.size(); ++light) {
        for (std::size_t channel = 0u; channel < lights[light].color_bits.size(); ++channel) {
            result[light][channel] = lights[light].color_bits[channel];
        }
        result[light][4u] = lights[light].intensity_bits;
        for (std::size_t axis = 0u; axis < lights[light].direction_bits.size(); ++axis) {
            result[light][5u + axis] = lights[light].direction_bits[axis];
        }
    }
    return result;
}

NnShaderMatrix matrix_from_bits(const MatrixBits& bits) {
    NnShaderMatrix result{};
    for (std::size_t index = 0u; index < result.size(); ++index) {
        result[index] = float_from_bits(bits[index]);
    }
    return result;
}

MatrixBits matrix_bits(const NnShaderMatrix& matrix) {
    MatrixBits result{};
    for (std::size_t index = 0u; index < matrix.size(); ++index) {
        result[index] = float_bits(matrix[index]);
    }
    return result;
}

OutputBits output_bits(const NnParallelLightingConstants& constants) {
    OutputBits result{};
    for (std::size_t light = 0u; light < constants.lights.size(); ++light) {
        for (std::size_t channel = 0u; channel < constants.lights[light].diffuse.size(); ++channel) {
            result[light][channel] = float_bits(constants.lights[light].diffuse[channel]);
            result[light][4u + channel] = float_bits(constants.lights[light].position[channel]);
        }
    }
    return result;
}

OutputBits one_output(const LightOutputBits& first) {
    OutputBits result{};
    result[0u] = first;
    return result;
}

OutputBits two_outputs(const LightOutputBits& first, const LightOutputBits& second) {
    OutputBits result{};
    result[0u] = first;
    result[1u] = second;
    return result;
}

OutputBits four_outputs(
    const LightOutputBits& first,
    const LightOutputBits& second,
    const LightOutputBits& third,
    const LightOutputBits& fourth) {
    return {{first, second, third, fourth}};
}

bool check_constants(
    const NnParallelLightingConstants& actual,
    std::uint32_t expected_count,
    const OutputBits& expected,
    const char* message) {
    for (std::size_t light = 0u; light < actual.lights.size(); ++light) {
        for (std::size_t channel = 0u; channel < 4u; ++channel) {
            if (!check(float_bits(actual.lights[light].specular[channel]) == expected[light][channel],
                       "parallel-light specular constant mismatch")) {
                return false;
            }
        }
    }
    return check(actual.count == expected_count, message) && check(output_bits(actual) == expected, message);
}

bool inputs_are_unchanged(
    const std::array<StageParallelLight, 8u>& lights,
    const LightInputs& original_lights,
    const NnShaderMatrix& matrix,
    const MatrixBits& original_matrix) {
    return light_bits(lights) == original_lights && matrix_bits(matrix) == original_matrix;
}

bool rejects_without_mutating(
    std::array<StageParallelLight, 8u>& lights,
    std::uint32_t mask,
    NnShaderMatrix& matrix,
    CameraPrecision precision,
    const char* message) {
    const LightInputs original_lights = light_bits(lights);
    const MatrixBits original_matrix = matrix_bits(matrix);
    return expects_invalid_argument(
               [&]() { nn_parallel_lighting_constants(lights, mask, matrix, precision); },
               message) &&
           check(inputs_are_unchanged(lights, original_lights, matrix, original_matrix), message);
}

bool test_actual_preset_identity_retains_precision_specific_original_bits() {
    std::array<StageParallelLight, 8u> single_lights = lights_from_bits(kPreset0SingleLights);
    std::array<StageParallelLight, 8u> double_lights = lights_from_bits(kPreset0DoubleLights);
    NnShaderMatrix single_matrix = matrix_from_bits(kIdentityMatrix);
    NnShaderMatrix double_matrix = matrix_from_bits(kIdentityMatrix);
    const LightInputs original_single_lights = light_bits(single_lights);
    const LightInputs original_double_lights = light_bits(double_lights);
    const MatrixBits original_single_matrix = matrix_bits(single_matrix);
    const MatrixBits original_double_matrix = matrix_bits(double_matrix);

    const NnParallelLightingConstants single = nn_parallel_lighting_constants(
        single_lights, 0x03u, single_matrix, CameraPrecision::Single);
    const NnParallelLightingConstants double_precision = nn_parallel_lighting_constants(
        double_lights, 0x03u, double_matrix, CameraPrecision::Double);
    return check_constants(
               single,
               2u,
               two_outputs(kIdentitySingleLight0, kIdentitySingleLight1),
               "preset 0 identity single-precision light constants mismatch") &&
           check_constants(
               double_precision,
               2u,
               two_outputs(kIdentityDoubleLight0, kIdentityDoubleLight1),
               "preset 0 identity double-precision light constants mismatch") &&
           check(
               output_bits(single)[0u][4u] != output_bits(double_precision)[0u][4u],
               "precision-sensitive preset output collapsed") &&
           check(
               inputs_are_unchanged(single_lights, original_single_lights, single_matrix, original_single_matrix) &&
                   inputs_are_unchanged(double_lights, original_double_lights, double_matrix, original_double_matrix),
               "preset 0 identity call modified an input");
}

bool test_actual_preset_quarter_turn_retains_transformed_original_bits() {
    std::array<StageParallelLight, 8u> single_lights = lights_from_bits(kPreset0SingleLights);
    std::array<StageParallelLight, 8u> double_lights = lights_from_bits(kPreset0DoubleLights);
    NnShaderMatrix single_matrix = matrix_from_bits(kQuarterTurnMatrix);
    NnShaderMatrix double_matrix = matrix_from_bits(kQuarterTurnMatrix);
    const LightInputs original_single_lights = light_bits(single_lights);
    const LightInputs original_double_lights = light_bits(double_lights);
    const MatrixBits original_single_matrix = matrix_bits(single_matrix);
    const MatrixBits original_double_matrix = matrix_bits(double_matrix);

    const NnParallelLightingConstants single = nn_parallel_lighting_constants(
        single_lights, 0x03u, single_matrix, CameraPrecision::Single);
    const NnParallelLightingConstants double_precision = nn_parallel_lighting_constants(
        double_lights, 0x03u, double_matrix, CameraPrecision::Double);
    return check_constants(
               single,
               2u,
               two_outputs(kQuarterTurnSingleLight0, kQuarterTurnSingleLight1),
               "preset 0 quarter-turn single-precision light constants mismatch") &&
           check_constants(
               double_precision,
               2u,
               two_outputs(kQuarterTurnDoubleLight0, kQuarterTurnDoubleLight1),
               "preset 0 quarter-turn double-precision light constants mismatch") &&
           check(
               inputs_are_unchanged(single_lights, original_single_lights, single_matrix, original_single_matrix) &&
                   inputs_are_unchanged(double_lights, original_double_lights, double_matrix, original_double_matrix),
               "preset 0 quarter-turn call modified an input");
}

bool test_original_nonunit_mixed_case_retains_packed_bits() {
    std::array<StageParallelLight, 8u> lights = lights_from_bits(kMixedNonunitLights);
    NnShaderMatrix matrix = matrix_from_bits(kMixedNonunitMatrix);
    const LightInputs original_lights = light_bits(lights);
    const MatrixBits original_matrix = matrix_bits(matrix);
    const NnParallelLightingConstants single = nn_parallel_lighting_constants(
        lights, 0xAAu, matrix, CameraPrecision::Single);
    const NnParallelLightingConstants double_precision = nn_parallel_lighting_constants(
        lights, 0xAAu, matrix, CameraPrecision::Double);
    return check_constants(
               single,
               4u,
               kMixedNonunitSingle,
               "nonunit mixed single-precision light constants mismatch") &&
           check_constants(
               double_precision,
               4u,
               kMixedNonunitDouble,
               "nonunit mixed double-precision light constants mismatch") &&
           check(
               inputs_are_unchanged(lights, original_lights, matrix, original_matrix),
               "nonunit mixed call modified an input");
}

bool test_selects_first_enabled_slots_and_caps_at_four() {
    std::array<StageParallelLight, 8u> lights = lights_from_bits(kPreset0SingleLights);
    NnShaderMatrix matrix = matrix_from_bits(kIdentityMatrix);
    const LightInputs original_lights = light_bits(lights);
    const MatrixBits original_matrix = matrix_bits(matrix);
    const NnParallelLightingConstants empty = nn_parallel_lighting_constants(
        lights, 0u, matrix, CameraPrecision::Single);
    const NnParallelLightingConstants high_slot = nn_parallel_lighting_constants(
        lights, 0x80u, matrix, CameraPrecision::Single);
    const NnParallelLightingConstants capped = nn_parallel_lighting_constants(
        lights, 0xFFu, matrix, CameraPrecision::Single);
    return check_constants(empty, 0u, OutputBits{}, "empty parallel-light mask mismatch") &&
           check_constants(high_slot, 1u, one_output(kHighSlotLight), "high parallel-light slot mismatch") &&
           check_constants(
               capped,
               4u,
               four_outputs(kIdentitySingleLight0, kIdentitySingleLight1, kCapLight2, kCapLight3),
               "parallel-light cap or ordering mismatch") &&
           check(
               inputs_are_unchanged(lights, original_lights, matrix, original_matrix),
               "parallel-light selection modified an input");
}

bool test_ignores_unselected_lights_and_unused_matrix_words() {
    std::array<StageParallelLight, 8u> ignored_lights = lights_from_bits(kPreset0SingleLights);
    for (std::size_t index = 1u; index < ignored_lights.size(); ++index) {
        ignored_lights[index].color_bits = {{0x7FC01234u, 0x7F800000u, 0xFFC01234u, 0xFF800000u}};
        ignored_lights[index].intensity_bits = 0x7FC01234u;
        ignored_lights[index].direction_bits = {{0x7FC01234u, 0x7F800000u, 0xFFC01234u}};
    }
    NnShaderMatrix matrix = matrix_from_bits(kIdentityMatrix);
    for (const std::size_t index : std::array<std::size_t, 7u>{{3u, 7u, 11u, 12u, 13u, 14u, 15u}}) {
        matrix[index] = float_from_bits(0x7FC12345u);
    }
    const LightInputs original_lights = light_bits(ignored_lights);
    const MatrixBits original_matrix = matrix_bits(matrix);
    const NnParallelLightingConstants selected = nn_parallel_lighting_constants(
        ignored_lights, 0x01u, matrix, CameraPrecision::Single);

    std::array<StageParallelLight, 8u> beyond_cap_lights = lights_from_bits(kPreset0SingleLights);
    for (std::size_t index = 4u; index < beyond_cap_lights.size(); ++index) {
        beyond_cap_lights[index].color_bits = {{0x7FC01234u, 0x7F800000u, 0xFFC01234u, 0xFF800000u}};
        beyond_cap_lights[index].intensity_bits = 0x7FC01234u;
        beyond_cap_lights[index].direction_bits = {{0x7FC01234u, 0x7F800000u, 0xFFC01234u}};
    }
    NnShaderMatrix cap_matrix = matrix_from_bits(kIdentityMatrix);
    const LightInputs original_cap_lights = light_bits(beyond_cap_lights);
    const MatrixBits original_cap_matrix = matrix_bits(cap_matrix);
    const NnParallelLightingConstants capped = nn_parallel_lighting_constants(
        beyond_cap_lights, 0xFFu, cap_matrix, CameraPrecision::Single);

    return check_constants(
               selected,
               1u,
               one_output(kIdentitySingleLight0),
               "unselected nonfinite light or matrix words affected constants") &&
           check_constants(
               capped,
               4u,
               four_outputs(kIdentitySingleLight0, kIdentitySingleLight1, kCapLight2, kCapLight3),
               "beyond-cap nonfinite lights affected constants") &&
           check(
               inputs_are_unchanged(ignored_lights, original_lights, matrix, original_matrix) &&
                   inputs_are_unchanged(
                       beyond_cap_lights, original_cap_lights, cap_matrix, original_cap_matrix),
               "ignored parallel-light inputs were modified");
}

bool test_transforms_then_normalizes_nonunit_directions() {
    std::array<StageParallelLight, 8u> lights{};
    lights[0u].color_bits = {{0x3FC00000u, 0xC0000000u, 0x00000000u, 0xBF800000u}};
    lights[0u].intensity_bits = 0x3F800000u;
    lights[0u].direction_bits = {{0x40000000u, 0xC0000000u, 0x3F800000u}};
    NnShaderMatrix matrix = matrix_from_bits(kIdentityMatrix);
    const LightInputs original_lights = light_bits(lights);
    const MatrixBits original_matrix = matrix_bits(matrix);
    const LightOutputBits expected = {{
        0x3FC00000u, 0xC0000000u, 0x00000000u, 0xBF800000u,
        0xBF2AAAABu, 0x3F2AAAABu, 0xBEAAAAABu, 0x00000000u,
    }};

    for (const CameraPrecision precision : {CameraPrecision::Single, CameraPrecision::Double}) {
        const NnParallelLightingConstants actual = nn_parallel_lighting_constants(lights, 0x01u, matrix, precision);
        if (!check_constants(actual, 1u, one_output(expected), "nonunit parallel-light direction mismatch") ||
            !check(
                inputs_are_unchanged(lights, original_lights, matrix, original_matrix),
                "nonunit parallel-light call modified an input")) {
            return false;
        }
    }
    return true;
}

bool test_diffuse_scaling_keeps_alpha_unscaled() {
    std::array<StageParallelLight, 8u> lights{};
    for (std::size_t index = 0u; index < 3u; ++index) {
        lights[index].direction_bits = {{0x3F800000u, 0x00000000u, 0x00000000u}};
    }
    lights[0u].color_bits = {{0x3FC00000u, 0xC0000000u, 0x80000000u, 0x3F400000u}};
    lights[0u].intensity_bits = 0x00000000u;
    lights[1u].color_bits = {{0x00000000u, 0x80000000u, 0x40400000u, 0x80000000u}};
    lights[1u].intensity_bits = 0x3F800000u;
    lights[2u].color_bits = {{0x3FC00000u, 0xC0000000u, 0x00000000u, 0x3F800001u}};
    lights[2u].intensity_bits = 0xC0000000u;
    NnShaderMatrix matrix = matrix_from_bits(kIdentityMatrix);
    const LightInputs original_lights = light_bits(lights);
    const MatrixBits original_matrix = matrix_bits(matrix);
    const NnParallelLightingConstants actual = nn_parallel_lighting_constants(
        lights, 0x07u, matrix, CameraPrecision::Single);

    const std::array<LightOutputBits, 3u> expected = {{
        {{0x00000000u, 0x80000000u, 0x80000000u, 0x3F400000u, 0u, 0u, 0u, 0u}},
        {{0x00000000u, 0x80000000u, 0x40400000u, 0x80000000u, 0u, 0u, 0u, 0u}},
        {{0xC0400000u, 0x40800000u, 0x80000000u, 0x3F800001u, 0u, 0u, 0u, 0u}},
    }};
    const OutputBits bits = output_bits(actual);
    return check(actual.count == 3u, "diffuse scaling light count mismatch") &&
           check(
               bits[0u][0u] == expected[0u][0u] && bits[0u][1u] == expected[0u][1u] &&
                   bits[0u][2u] == expected[0u][2u] && bits[0u][3u] == expected[0u][3u] &&
                   bits[1u][0u] == expected[1u][0u] && bits[1u][1u] == expected[1u][1u] &&
                   bits[1u][2u] == expected[1u][2u] && bits[1u][3u] == expected[1u][3u] &&
                   bits[2u][0u] == expected[2u][0u] && bits[2u][1u] == expected[2u][1u] &&
                   bits[2u][2u] == expected[2u][2u] && bits[2u][3u] == expected[2u][3u],
               "diffuse intensity scaling or alpha preservation mismatch") &&
           check(
               inputs_are_unchanged(lights, original_lights, matrix, original_matrix),
               "diffuse scaling modified an input");
}

bool test_zero_and_signed_zero_directions_keep_original_position_signs() {
    std::array<StageParallelLight, 8u> lights = lights_from_bits(kPreset0SingleLights);
    lights[0u].direction_bits = {{0x80000000u, 0x00000000u, 0x80000000u}};
    NnShaderMatrix matrix = matrix_from_bits(kIdentityMatrix);
    const LightInputs original_lights = light_bits(lights);
    const MatrixBits original_matrix = matrix_bits(matrix);
    const LightOutputBits expected = {{
        0x3F333334u, 0x3F333334u, 0x3F333334u, 0x3F800000u,
        0x80000000u, 0x80000000u, 0x80000000u, 0x00000000u,
    }};
    const NnParallelLightingConstants actual = nn_parallel_lighting_constants(
        lights, 0x01u, matrix, CameraPrecision::Single);
    return check_constants(actual, 1u, one_output(expected), "zero parallel-light position signs mismatch") &&
           check(
               inputs_are_unchanged(lights, original_lights, matrix, original_matrix),
               "zero parallel-light call modified an input");
}

bool test_rejects_invalid_domains_and_nonfinite_or_overflowing_values() {
    const auto valid_lights = lights_from_bits(kPreset0SingleLights);
    const auto valid_matrix = matrix_from_bits(kIdentityMatrix);
    {
        auto lights = valid_lights;
        auto matrix = valid_matrix;
        if (!rejects_without_mutating(
                lights,
                0x100u,
                matrix,
                CameraPrecision::Single,
                "out-of-domain parallel-light mask was accepted")) {
            return false;
        }
    }
    {
        auto lights = valid_lights;
        auto matrix = valid_matrix;
        if (!rejects_without_mutating(
                lights,
                0x01u,
                matrix,
                static_cast<CameraPrecision>(99),
                "unsupported parallel-light precision was accepted")) {
            return false;
        }
    }

    for (std::size_t channel = 0u; channel < valid_lights[0u].color_bits.size(); ++channel) {
        auto lights = valid_lights;
        auto matrix = valid_matrix;
        lights[0u].color_bits[channel] = 0x7FC01234u;
        if (!rejects_without_mutating(
                lights,
                0x01u,
                matrix,
                CameraPrecision::Single,
                "nonfinite selected parallel-light color was accepted")) {
            return false;
        }
    }
    {
        auto lights = valid_lights;
        auto matrix = valid_matrix;
        lights[0u].intensity_bits = 0x7F800000u;
        if (!rejects_without_mutating(
                lights,
                0x01u,
                matrix,
                CameraPrecision::Double,
                "nonfinite selected parallel-light intensity was accepted")) {
            return false;
        }
    }
    for (std::size_t axis = 0u; axis < valid_lights[0u].direction_bits.size(); ++axis) {
        auto lights = valid_lights;
        auto matrix = valid_matrix;
        lights[0u].direction_bits[axis] = 0xFF800000u;
        if (!rejects_without_mutating(
                lights,
                0x01u,
                matrix,
                CameraPrecision::Single,
                "nonfinite selected parallel-light direction was accepted")) {
            return false;
        }
    }

    const std::array<std::size_t, 9u> consumed_matrix_indices = {{0u, 1u, 2u, 4u, 5u, 6u, 8u, 9u, 10u}};
    for (std::size_t index : consumed_matrix_indices) {
        auto lights = valid_lights;
        auto matrix = valid_matrix;
        matrix[index] = float_from_bits(0x7FC01234u);
        if (!rejects_without_mutating(
                lights,
                0x01u,
                matrix,
                CameraPrecision::Double,
                "nonfinite consumed parallel-light matrix value was accepted")) {
            return false;
        }
    }
    {
        auto lights = valid_lights;
        auto matrix = valid_matrix;
        lights[0u].color_bits[0u] = 0x7F7FFFFFu;
        lights[0u].intensity_bits = 0x7F7FFFFFu;
        if (!rejects_without_mutating(
                lights,
                0x01u,
                matrix,
                CameraPrecision::Single,
                "overflowing parallel-light diffuse component was accepted")) {
            return false;
        }
    }
    {
        auto lights = valid_lights;
        auto matrix = valid_matrix;
        lights[0u].direction_bits[0u] = 0x7F7FFFFFu;
        matrix[0u] = float_from_bits(0x7F7FFFFFu);
        if (!rejects_without_mutating(
                lights,
                0x01u,
                matrix,
                CameraPrecision::Double,
                "overflowing parallel-light transform was accepted")) {
            return false;
        }
    }
    return true;
}

}

int main() {
    return test_actual_preset_identity_retains_precision_specific_original_bits() &&
                   test_actual_preset_quarter_turn_retains_transformed_original_bits() &&
                   test_original_nonunit_mixed_case_retains_packed_bits() &&
                   test_selects_first_enabled_slots_and_caps_at_four() &&
                   test_ignores_unselected_lights_and_unused_matrix_words() &&
                   test_transforms_then_normalizes_nonunit_directions() &&
                   test_diffuse_scaling_keeps_alpha_unscaled() &&
                   test_zero_and_signed_zero_directions_keep_original_position_signs() &&
                   test_rejects_invalid_domains_and_nonfinite_or_overflowing_values()
               ? 0
               : 1;
}
