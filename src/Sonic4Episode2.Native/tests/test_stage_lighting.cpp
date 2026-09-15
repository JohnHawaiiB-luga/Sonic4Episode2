#include "stage_lighting.h"

#include "stage_data.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <stdexcept>
#include <utility>
#include <vector>

namespace {

constexpr std::size_t kWordCount = 148u;
constexpr std::size_t kPresetSize = kWordCount * sizeof(std::uint32_t);

constexpr std::array<std::array<std::uint32_t, 3u>, 8u> kDirections = {{
    {{0x40000000u, 0xC0000000u, 0x3F800000u}},
    {{0xBF800000u, 0x00000000u, 0x00000000u}},
    {{0x00000000u, 0x3F800000u, 0x00000000u}},
    {{0x00000000u, 0x00000000u, 0xBF800000u}},
    {{0x3F800000u, 0x3F800000u, 0x00000000u}},
    {{0xBF800000u, 0x3F800000u, 0x3F800000u}},
    {{0x40000000u, 0xBF800000u, 0x3F000000u}},
    {{0x3F000000u, 0xC0000000u, 0x40000000u}},
}};

bool check(bool condition, const char* message) {
    if (!condition) {
        std::fprintf(stderr, "%s\n", message);
    }
    return condition;
}

template <typename Callable>
bool expects_stage_data_error(Callable&& callable, const char* message) {
    try {
        std::forward<Callable>(callable)();
    } catch (const StageDataError&) {
        return true;
    } catch (...) {
        return check(false, "wrong stage lighting parser exception type");
    }
    return check(false, message);
}

template <typename Callable>
bool expects_invalid_argument(Callable&& callable, const char* message) {
    try {
        std::forward<Callable>(callable)();
    } catch (const std::invalid_argument&) {
        return true;
    } catch (...) {
        return check(false, "wrong stage lighting normalization exception type");
    }
    return check(false, message);
}

std::uint32_t color_bits(std::size_t light, std::size_t channel) {
    return 0x3E800000u + static_cast<std::uint32_t>(light) * 0x00200000u +
           static_cast<std::uint32_t>(channel) * 0x00010000u;
}

std::uint32_t intensity_bits(std::size_t light) {
    return 0x3F000000u + static_cast<std::uint32_t>(light) * 0x00100000u;
}

void write_be32(std::vector<std::uint8_t>& bytes, std::size_t word, std::uint32_t value) {
    const std::size_t offset = word * sizeof(value);
    bytes[offset] = static_cast<std::uint8_t>(value >> 24u);
    bytes[offset + 1u] = static_cast<std::uint8_t>(value >> 16u);
    bytes[offset + 2u] = static_cast<std::uint8_t>(value >> 8u);
    bytes[offset + 3u] = static_cast<std::uint8_t>(value);
}

std::vector<std::uint8_t> make_preset() {
    std::vector<std::uint8_t> result(kPresetSize, 0u);
    write_be32(result, 0u, 0xD0C0B0A0u);
    write_be32(result, 1u, 0x3F000000u);
    write_be32(result, 2u, 0xBF000000u);
    write_be32(result, 3u, 0x3E800000u);

    for (std::size_t index = 0u; index < 8u; ++index) {
        const std::size_t base = 4u + index * 18u;
        write_be32(result, base, 1u);
        write_be32(result, base + 1u, 0x7FC01234u);
        for (std::size_t channel = 0u; channel < 4u; ++channel) {
            write_be32(result, base + 2u + channel, color_bits(index, channel));
        }
        write_be32(result, base + 6u, intensity_bits(index));
        for (std::size_t axis = 0u; axis < 3u; ++axis) {
            write_be32(result, base + 7u + axis, kDirections[index][axis]);
        }
        for (std::size_t unused = 10u; unused < 18u; ++unused) {
            write_be32(result, base + unused, unused % 2u == 0u ? 0x7F800000u : 0x7FC01234u);
        }
    }
    return result;
}

StageLighting parse_valid_preset() {
    const std::vector<std::uint8_t> bytes = make_preset();
    return parse_pc_stage_lighting(bytes.data(), bytes.size());
}

bool same_lighting(const StageLighting& left, const StageLighting& right) {
    if (left.header_word != right.header_word || left.ambient_bits != right.ambient_bits) {
        return false;
    }
    for (std::size_t index = 0u; index < left.lights.size(); ++index) {
        if (left.lights[index].color_bits != right.lights[index].color_bits ||
            left.lights[index].intensity_bits != right.lights[index].intensity_bits ||
            left.lights[index].direction_bits != right.lights[index].direction_bits) {
            return false;
        }
    }
    return true;
}

bool same_non_direction_fields(const StageLighting& left, const StageLighting& right) {
    if (left.header_word != right.header_word || left.ambient_bits != right.ambient_bits) {
        return false;
    }
    for (std::size_t index = 0u; index < left.lights.size(); ++index) {
        if (left.lights[index].color_bits != right.lights[index].color_bits ||
            left.lights[index].intensity_bits != right.lights[index].intensity_bits) {
            return false;
        }
    }
    return true;
}

bool test_parser_retains_semantic_bits_and_ignores_unused_words() {
    std::vector<std::uint8_t> bytes = make_preset();
    const std::vector<std::uint8_t> original_bytes = bytes;
    const StageLighting lighting = parse_pc_stage_lighting(bytes.data(), bytes.size());
    if (!check(lighting.header_word == 0xD0C0B0A0u, "stage lighting header word mismatch") ||
        !check(
            lighting.ambient_bits == std::array<std::uint32_t, 3u>{{0x3F000000u, 0xBF000000u, 0x3E800000u}},
            "stage lighting ambient bits mismatch") ||
        !check(bytes == original_bytes, "stage lighting parser mutated input data")) {
        return false;
    }
    for (std::size_t index = 0u; index < lighting.lights.size(); ++index) {
        for (std::size_t channel = 0u; channel < lighting.lights[index].color_bits.size(); ++channel) {
            if (!check(
                    lighting.lights[index].color_bits[channel] == color_bits(index, channel),
                    "stage lighting color bits mismatch")) {
                return false;
            }
        }
        if (!check(
                lighting.lights[index].intensity_bits == intensity_bits(index),
                "stage lighting intensity bits mismatch") ||
            !check(
                lighting.lights[index].direction_bits == kDirections[index],
                "stage lighting direction bits mismatch")) {
            return false;
        }
    }
    return true;
}

bool test_parser_rejects_malformed_and_nonfinite_consumed_words() {
    const std::vector<std::uint8_t> valid = make_preset();
    const std::vector<std::uint8_t> truncated(kPresetSize - 1u, 0u);
    const std::vector<std::uint8_t> oversized(kPresetSize + 1u, 0u);
    if (!expects_stage_data_error(
            []() { parse_pc_stage_lighting(nullptr, kPresetSize); },
            "null stage lighting data was accepted") ||
        !expects_stage_data_error(
            [&]() { parse_pc_stage_lighting(truncated.data(), truncated.size()); },
            "truncated stage lighting data was accepted") ||
        !expects_stage_data_error(
            [&]() { parse_pc_stage_lighting(oversized.data(), oversized.size()); },
            "oversized stage lighting data was accepted")) {
        return false;
    }

    for (std::size_t index = 0u; index < 8u; ++index) {
        std::vector<std::uint8_t> wrong_type = valid;
        write_be32(wrong_type, 4u + index * 18u, 2u);
        if (!expects_stage_data_error(
                [&]() { parse_pc_stage_lighting(wrong_type.data(), wrong_type.size()); },
                "unsupported stage lighting entry type was accepted")) {
            return false;
        }
    }

    const auto rejects_nonfinite_word = [&](std::size_t word) {
        for (const std::uint32_t nonfinite : {0x7F800000u, 0x7FC01234u}) {
            std::vector<std::uint8_t> malformed = valid;
            write_be32(malformed, word, nonfinite);
            if (!expects_stage_data_error(
                    [&]() { parse_pc_stage_lighting(malformed.data(), malformed.size()); },
                    "nonfinite consumed stage lighting word was accepted")) {
                return false;
            }
        }
        return true;
    };

    for (std::size_t word = 1u; word <= 3u; ++word) {
        if (!rejects_nonfinite_word(word)) {
            return false;
        }
    }
    for (std::size_t index = 0u; index < 8u; ++index) {
        const std::size_t base = 4u + index * 18u;
        for (std::size_t channel = 0u; channel < 4u; ++channel) {
            if (!rejects_nonfinite_word(base + 2u + channel)) {
                return false;
            }
        }
        if (!rejects_nonfinite_word(base + 6u)) {
            return false;
        }
        for (std::size_t axis = 0u; axis < 3u; ++axis) {
            if (!rejects_nonfinite_word(base + 7u + axis)) {
                return false;
            }
        }
    }
    return true;
}

bool test_normalization_preserves_input_and_non_direction_fields() {
    StageLighting lighting = parse_valid_preset();
    lighting.lights[0u].direction_bits = {{0x80000000u, 0x00000000u, 0x80000000u}};
    lighting.lights[1u].direction_bits = {{0xC0A00000u, 0x80000000u, 0x00000000u}};
    lighting.lights[2u].direction_bits = {{0x40000000u, 0xC0000000u, 0x3F800000u}};
    lighting.lights[3u].direction_bits = {{0x00000001u, 0x00000000u, 0x00000000u}};
    const StageLighting original = lighting;

    for (const CameraPrecision precision : {CameraPrecision::Single, CameraPrecision::Double}) {
        const StageLighting normalized = normalize_stage_lighting(lighting, precision);
        if (!check(same_lighting(lighting, original), "stage lighting normalization mutated its input") ||
            !check(
                same_non_direction_fields(normalized, original),
                "stage lighting normalization changed non-direction fields") ||
            !check(
                normalized.lights[0u].direction_bits == std::array<std::uint32_t, 3u>{{0u, 0u, 0u}},
                "zero stage lighting direction did not produce positive zero") ||
            !check(
                normalized.lights[1u].direction_bits ==
                    std::array<std::uint32_t, 3u>{{0xBF800000u, 0x80000000u, 0x00000000u}},
                "axis stage lighting direction lost signed zero") ||
            !check(
                normalized.lights[2u].direction_bits ==
                    std::array<std::uint32_t, 3u>{{0x3F2AAAABu, 0xBF2AAAABu, 0x3EAAAAABu}},
                "nonunit mixed-sign stage lighting direction mismatch") ||
            !check(
                normalized.lights[3u].direction_bits == std::array<std::uint32_t, 3u>{{0u, 0u, 0u}},
                "underflowed stage lighting direction did not produce positive zero")) {
            return false;
        }
    }
    return true;
}

bool test_normalization_rejects_nonfinite_fields_and_invalid_precision() {
    const StageLighting valid = parse_valid_preset();
    const auto rejects_nonfinite = [&](const StageLighting& malformed) {
        return expects_invalid_argument(
            [&]() { normalize_stage_lighting(malformed, CameraPrecision::Single); },
            "nonfinite stage lighting field was accepted for normalization");
    };

    for (std::size_t index = 0u; index < valid.ambient_bits.size(); ++index) {
        StageLighting malformed = valid;
        malformed.ambient_bits[index] = 0x7FC01234u;
        if (!rejects_nonfinite(malformed)) {
            return false;
        }
    }
    for (std::size_t light = 0u; light < valid.lights.size(); ++light) {
        for (std::size_t channel = 0u; channel < valid.lights[light].color_bits.size(); ++channel) {
            StageLighting malformed = valid;
            malformed.lights[light].color_bits[channel] = 0x7F800000u;
            if (!rejects_nonfinite(malformed)) {
                return false;
            }
        }
        {
            StageLighting malformed = valid;
            malformed.lights[light].intensity_bits = 0x7FC01234u;
            if (!rejects_nonfinite(malformed)) {
                return false;
            }
        }
        for (std::size_t axis = 0u; axis < valid.lights[light].direction_bits.size(); ++axis) {
            StageLighting malformed = valid;
            malformed.lights[light].direction_bits[axis] = 0x7F800000u;
            if (!rejects_nonfinite(malformed)) {
                return false;
            }
        }
    }
    for (const CameraPrecision precision : {CameraPrecision::Single, CameraPrecision::Double}) {
        StageLighting overflowing = valid;
        overflowing.lights[0u].direction_bits = {{0x7F7FFFFFu, 0x00000000u, 0x00000000u}};
        if (!expects_invalid_argument(
                [&]() { normalize_stage_lighting(overflowing, precision); },
                "overflowing stage lighting direction was accepted")) {
            return false;
        }
    }
    return expects_invalid_argument(
        [&]() { normalize_stage_lighting(valid, static_cast<CameraPrecision>(99)); },
        "unsupported stage lighting precision was accepted");
}

}

int main() {
    return test_parser_retains_semantic_bits_and_ignores_unused_words() &&
                   test_parser_rejects_malformed_and_nonfinite_consumed_words() &&
                   test_normalization_preserves_input_and_non_direction_fields() &&
                   test_normalization_rejects_nonfinite_fields_and_invalid_precision()
               ? 0
               : 1;
}
