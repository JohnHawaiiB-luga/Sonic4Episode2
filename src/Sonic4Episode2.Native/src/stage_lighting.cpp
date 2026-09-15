#include "stage_lighting.h"

#include "stage_data.h"

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <stdexcept>

namespace {

constexpr std::size_t kWordCount = 148u;
constexpr std::size_t kPresetSize = kWordCount * sizeof(std::uint32_t);
constexpr std::size_t kLightCount = 8u;
constexpr std::size_t kWordsPerLight = 18u;
constexpr std::size_t kLightFirstWord = 4u;

[[noreturn]] void fail_parse(const char* message) {
    throw StageDataError(message);
}

[[noreturn]] void fail_normalization(const char* message) {
    throw std::invalid_argument(message);
}

std::uint32_t read_be32(const std::uint8_t* data, std::size_t size, std::size_t word) {
    if (word > std::numeric_limits<std::size_t>::max() / sizeof(std::uint32_t)) {
        fail_parse("stage lighting word index is out of range");
    }
    const std::size_t offset = word * sizeof(std::uint32_t);
    if (offset > size || size - offset < sizeof(std::uint32_t)) {
        fail_parse("stage lighting word is out of range");
    }
    return (static_cast<std::uint32_t>(data[offset]) << 24u) |
           (static_cast<std::uint32_t>(data[offset + 1u]) << 16u) |
           (static_cast<std::uint32_t>(data[offset + 2u]) << 8u) |
           static_cast<std::uint32_t>(data[offset + 3u]);
}

bool is_finite_float_bits(std::uint32_t bits) {
    return (bits & 0x7F800000u) != 0x7F800000u;
}

bool has_only_finite_consumed_values(const StageLighting& lighting) {
    for (std::uint32_t bits : lighting.ambient_bits) {
        if (!is_finite_float_bits(bits)) {
            return false;
        }
    }
    for (const StageParallelLight& light : lighting.lights) {
        for (std::uint32_t bits : light.color_bits) {
            if (!is_finite_float_bits(bits)) {
                return false;
            }
        }
        if (!is_finite_float_bits(light.intensity_bits)) {
            return false;
        }
        for (std::uint32_t bits : light.direction_bits) {
            if (!is_finite_float_bits(bits)) {
                return false;
            }
        }
    }
    return true;
}

float decode_float(std::uint32_t bits) {
    static_assert(sizeof(float) == sizeof(bits), "stage lighting requires 32-bit floats");
    float value = 0.0f;
    std::memcpy(&value, &bits, sizeof(value));
    return value;
}

std::uint32_t encode_float(float value) {
    std::uint32_t bits = 0u;
    std::memcpy(&bits, &value, sizeof(bits));
    return bits;
}

void require_precision(CameraPrecision precision) {
    if (precision != CameraPrecision::Single && precision != CameraPrecision::Double) {
        fail_normalization("stage lighting precision is unsupported");
    }
}

struct Arithmetic {
    CameraPrecision precision;

    double rounded(double value) const {
        if (!std::isfinite(value)) {
            fail_normalization("stage lighting arithmetic is nonfinite");
        }
        if (precision == CameraPrecision::Double) {
            return value;
        }
        int exponent = 0;
        const float mantissa = static_cast<float>(std::frexp(value, &exponent));
        const double result = std::ldexp(static_cast<double>(mantissa), exponent);
        if (!std::isfinite(result)) {
            fail_normalization("stage lighting arithmetic is nonfinite");
        }
        return result;
    }

    double add(double left, double right) const {
        return rounded(left + right);
    }

    double multiply(double left, double right) const {
        return rounded(left * right);
    }
};

float store_finite(double value) {
    if (!std::isfinite(value)) {
        fail_normalization("stage lighting arithmetic is nonfinite");
    }
    const float stored = static_cast<float>(value);
    if (!std::isfinite(stored)) {
        fail_normalization("stage lighting arithmetic is nonfinite");
    }
    return stored;
}

std::array<std::uint32_t, 3u> normalize_direction(
    const std::array<std::uint32_t, 3u>& direction_bits,
    const Arithmetic& arithmetic) {
    const std::array<float, 3u> direction = {{
        decode_float(direction_bits[0u]),
        decode_float(direction_bits[1u]),
        decode_float(direction_bits[2u]),
    }};
    const double xy = arithmetic.add(
        arithmetic.multiply(direction[0u], direction[0u]),
        arithmetic.multiply(direction[1u], direction[1u]));
    const float squared_length = store_finite(
        arithmetic.add(xy, arithmetic.multiply(direction[2u], direction[2u])));
    if (squared_length == 0.0f) {
        return {{0u, 0u, 0u}};
    }

    const double length = arithmetic.rounded(std::sqrt(static_cast<double>(squared_length)));
    if (!(length > 0.0)) {
        fail_normalization("stage lighting direction length is invalid");
    }
    const float reciprocal = store_finite(arithmetic.rounded(1.0 / length));
    return {{
        encode_float(store_finite(arithmetic.multiply(direction[0u], reciprocal))),
        encode_float(store_finite(arithmetic.multiply(direction[1u], reciprocal))),
        encode_float(store_finite(arithmetic.multiply(direction[2u], reciprocal))),
    }};
}

}

StageLighting parse_pc_stage_lighting(const std::uint8_t* data, std::size_t size) {
    if (data == nullptr) {
        fail_parse("stage lighting data is null");
    }
    if (size != kPresetSize) {
        fail_parse("stage lighting length is unsupported");
    }

    std::array<std::uint32_t, kWordCount> words{};
    for (std::size_t index = 0u; index < words.size(); ++index) {
        words[index] = read_be32(data, size, index);
    }

    StageLighting result{};
    result.header_word = words[0u];
    for (std::size_t index = 0u; index < result.ambient_bits.size(); ++index) {
        result.ambient_bits[index] = words[1u + index];
    }
    for (std::size_t index = 0u; index < kLightCount; ++index) {
        const std::size_t base = kLightFirstWord + index * kWordsPerLight;
        if (words[base] != 1u) {
            fail_parse("stage lighting entry type is unsupported");
        }
        StageParallelLight& light = result.lights[index];
        for (std::size_t channel = 0u; channel < light.color_bits.size(); ++channel) {
            light.color_bits[channel] = words[base + 2u + channel];
        }
        light.intensity_bits = words[base + 6u];
        for (std::size_t axis = 0u; axis < light.direction_bits.size(); ++axis) {
            light.direction_bits[axis] = words[base + 7u + axis];
        }
    }
    if (!has_only_finite_consumed_values(result)) {
        fail_parse("stage lighting contains a nonfinite consumed value");
    }
    return result;
}

StageLighting normalize_stage_lighting(const StageLighting& lighting, CameraPrecision precision) {
    require_precision(precision);
    if (!has_only_finite_consumed_values(lighting)) {
        fail_normalization("stage lighting contains a nonfinite consumed value");
    }

    const Arithmetic arithmetic{precision};
    StageLighting result = lighting;
    for (std::size_t index = 0u; index < result.lights.size(); ++index) {
        result.lights[index].direction_bits = normalize_direction(lighting.lights[index].direction_bits, arithmetic);
    }
    return result;
}
