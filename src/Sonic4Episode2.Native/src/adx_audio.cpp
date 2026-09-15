#include "adx_audio.h"

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <stdexcept>
#include <utility>
#include <vector>

namespace {

constexpr std::size_t kMinimumHeaderSize = 24u;
constexpr std::size_t kBlockBytes = 18u;
constexpr std::size_t kSamplesPerBlock = 32u;
constexpr std::int64_t kCoefficientScale = 4096;
constexpr std::uint16_t kEndMarker = 0x8000u;
constexpr double kTau = 6.2831853071795864769252867665590057683943387987502;
constexpr std::int64_t kMaximumCoefficient =
    std::numeric_limits<std::int64_t>::max() / 65536;

[[noreturn]] void fail(const char* message) {
    throw std::invalid_argument(message);
}

void require(bool condition, const char* message) {
    if (!condition) {
        fail(message);
    }
}

std::uint16_t read_be16(const std::uint8_t* data, std::size_t offset) {
    return static_cast<std::uint16_t>(
        (static_cast<std::uint16_t>(data[offset]) << 8u) |
        static_cast<std::uint16_t>(data[offset + 1u]));
}

std::uint32_t read_be32(const std::uint8_t* data, std::size_t offset) {
    return (static_cast<std::uint32_t>(data[offset]) << 24u) |
           (static_cast<std::uint32_t>(data[offset + 1u]) << 16u) |
           (static_cast<std::uint32_t>(data[offset + 2u]) << 8u) |
           static_cast<std::uint32_t>(data[offset + 3u]);
}

std::size_t checked_size(std::uint32_t value, const char* message) {
    const std::size_t result = static_cast<std::size_t>(value);
    if (static_cast<std::uint32_t>(result) != value) {
        fail(message);
    }
    return result;
}

std::size_t checked_sum(std::size_t left, std::size_t right, const char* message) {
    if (right > std::numeric_limits<std::size_t>::max() - left) {
        fail(message);
    }
    return left + right;
}

std::size_t checked_product(std::size_t left, std::size_t right, const char* message) {
    if (left != 0u && right > std::numeric_limits<std::size_t>::max() / left) {
        fail(message);
    }
    return left * right;
}

std::int64_t round_nearest_even(double value) {
    require(std::isfinite(value), "ADX predictor coefficient is nonfinite.");
    const double lower = std::floor(value);
    require(
        lower >= -static_cast<double>(kMaximumCoefficient) - 1.0 &&
            lower <= static_cast<double>(kMaximumCoefficient),
        "ADX predictor coefficient is out of range.");
    const std::int64_t lower_integer = static_cast<std::int64_t>(lower);
    const double fraction = value - lower;
    std::int64_t result = lower_integer;
    if (fraction > 0.5 || (fraction == 0.5 && lower_integer % 2 != 0)) {
        ++result;
    }
    require(
        result >= -kMaximumCoefficient && result <= kMaximumCoefficient,
        "ADX predictor coefficient is out of range.");
    return result;
}

std::int64_t floor_divide_scale(std::int64_t value) {
    if (value >= 0) {
        return value / kCoefficientScale;
    }
    return -1 - (-(value + 1) / kCoefficientScale);
}

std::size_t block_offset(
    std::size_t header_size,
    std::size_t block_index,
    std::size_t channel_count,
    std::size_t channel) {
    const std::size_t interleaved_block = checked_sum(
        checked_product(block_index, channel_count, "ADX block offset is too large."),
        channel,
        "ADX block offset is too large.");
    return checked_sum(
        header_size,
        checked_product(interleaved_block, kBlockBytes, "ADX block offset is too large."),
        "ADX block offset is too large.");
}

}

AdxAudioData decode_adx_audio(const std::uint8_t* data, std::size_t size) {
    require(data != nullptr, "ADX data is null.");
    require(size >= kMinimumHeaderSize, "ADX header is truncated.");
    require(read_be16(data, 0u) == 0x8000u, "ADX magic is unsupported.");

    const std::size_t header_size = static_cast<std::size_t>(read_be16(data, 2u)) + 4u;
    require(header_size >= kMinimumHeaderSize && header_size <= size, "ADX header size is invalid.");
    require(
        data[header_size - 6u] == static_cast<std::uint8_t>('(') &&
            data[header_size - 5u] == static_cast<std::uint8_t>('c') &&
            data[header_size - 4u] == static_cast<std::uint8_t>(')') &&
            data[header_size - 3u] == static_cast<std::uint8_t>('C') &&
            data[header_size - 2u] == static_cast<std::uint8_t>('R') &&
            data[header_size - 1u] == static_cast<std::uint8_t>('I'),
        "ADX copyright marker is invalid.");
    require(
        data[4u] == 3u && data[5u] == kBlockBytes && data[6u] == 4u,
        "ADX parameters are unsupported.");

    const std::size_t channel_count = static_cast<std::size_t>(data[7u]);
    const std::uint32_t sample_rate = read_be32(data, 8u);
    const std::uint32_t sample_count_field = read_be32(data, 12u);
    const std::size_t sample_count = checked_size(sample_count_field, "ADX sample count is too large.");
    require(channel_count != 0u && sample_rate != 0u, "ADX channels or sample rate is invalid.");

    const std::size_t block_count = sample_count / kSamplesPerBlock +
        (sample_count % kSamplesPerBlock == 0u ? 0u : 1u);
    const std::size_t block_data_size = checked_product(
        checked_product(block_count, channel_count, "ADX block count is too large."),
        kBlockBytes,
        "ADX block data is too large.");
    const std::size_t required_size = checked_sum(header_size, block_data_size, "ADX layout is too large.");
    require(size >= required_size, "ADX sample data is truncated.");

    const std::size_t output_samples = checked_product(
        sample_count,
        channel_count,
        "ADX output sample count is too large.");
    std::vector<std::int16_t> samples;
    require(output_samples <= samples.max_size(), "ADX output sample count is too large.");

    for (std::size_t block_index = 0u; block_index < block_count; ++block_index) {
        for (std::size_t channel = 0u; channel < channel_count; ++channel) {
            const std::size_t offset = block_offset(header_size, block_index, channel_count, channel);
            require((read_be16(data, offset) & kEndMarker) == 0u,
                "ADX stream ends before declared sample count.");
        }
    }

    const std::uint16_t cutoff = read_be16(data, 16u);
    const double root_two = std::sqrt(2.0);
    const double a = root_two - std::cos(
        kTau * static_cast<double>(cutoff) / static_cast<double>(sample_rate));
    const double b = root_two - 1.0;
    const double discriminant = (a + b) * (a - b);
    require(std::isfinite(a) && std::isfinite(discriminant) && discriminant >= 0.0,
        "ADX predictor coefficients are invalid.");
    const double c = (a - std::sqrt(discriminant)) / b;
    const std::int64_t coefficient0 = round_nearest_even(c * 2.0 * kCoefficientScale);
    const std::int64_t coefficient1 = round_nearest_even(-(c * c) * kCoefficientScale);

    samples.resize(output_samples);
    std::array<std::int64_t, 256u> previous1{};
    std::array<std::int64_t, 256u> previous2{};
    for (std::size_t block_index = 0u; block_index < block_count; ++block_index) {
        for (std::size_t channel = 0u; channel < channel_count; ++channel) {
            const std::size_t offset = block_offset(header_size, block_index, channel_count, channel);
            const std::int64_t scale = static_cast<std::int64_t>(read_be16(data, offset));
            for (std::size_t sample_in_block = 0u;
                 sample_in_block < kSamplesPerBlock;
                 ++sample_in_block) {
                const std::uint8_t packed = data[offset + 2u + sample_in_block / 2u];
                const std::int64_t residual = sample_in_block % 2u == 0u
                    ? static_cast<std::int64_t>(packed >> 4u)
                    : static_cast<std::int64_t>(packed & 0x0fu);
                const std::int64_t signed_residual = residual >= 8 ? residual - 16 : residual;
                const std::int64_t predictor = floor_divide_scale(
                    coefficient0 * previous1[channel] + coefficient1 * previous2[channel]);
                const std::int64_t raw_sample = signed_residual * scale + predictor;
                const std::int64_t sample = raw_sample < -32768
                    ? -32768
                    : raw_sample > 32767 ? 32767 : raw_sample;
                previous2[channel] = previous1[channel];
                previous1[channel] = sample;

                const std::size_t sample_index = block_index * kSamplesPerBlock + sample_in_block;
                if (sample_index < sample_count) {
                    samples[sample_index * channel_count + channel] = static_cast<std::int16_t>(sample);
                }
            }
        }
    }

    AdxAudioData result{};
    result.channels = static_cast<std::uint32_t>(channel_count);
    result.sample_rate = sample_rate;
    result.sample_count = sample_count_field;
    result.samples = std::move(samples);
    return result;
}
