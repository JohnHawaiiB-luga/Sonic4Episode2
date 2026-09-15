#pragma once

#include "nn_motion_scalar.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

struct NnMotionChannel {
    std::uint32_t flags;
    std::uint32_t interpolation_flags;
    std::int32_t target;
    std::uint32_t start_bits;
    std::uint32_t end_bits;
    std::uint32_t start_key_bits;
    std::uint32_t end_key_bits;
    std::uint32_t key_count;
    std::uint32_t key_stride;
    std::size_t key_data_offset;
};

struct NnMotionData {
    std::uint32_t flags;
    std::uint32_t start_bits;
    std::uint32_t end_bits;
    std::uint32_t frame_rate_bits;
    std::array<std::uint32_t, 2u> reserved_bits;
    std::uint32_t channel_count;
    std::vector<NnMotionChannel> channels;
    std::vector<std::uint8_t> key_data;
};

NnMotionData parse_pc_nn_motion(const std::uint8_t* data, std::size_t size);

std::vector<NnMotionA16Key> nn_motion_a16_keys(
    const NnMotionData& motion,
    std::size_t channel_index);

std::vector<NnMotionFloatKey> nn_motion_float_keys(
    const NnMotionData& motion,
    std::size_t channel_index);
