#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

struct AdxAudioData {
    std::uint32_t channels = 0u;
    std::uint32_t sample_rate = 0u;
    std::uint32_t sample_count = 0u;
    std::vector<std::int16_t> samples;
};

AdxAudioData decode_adx_audio(const std::uint8_t* data, std::size_t size);
