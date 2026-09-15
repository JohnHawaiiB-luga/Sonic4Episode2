#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

enum class DdsTextureFormat : std::uint8_t {
    Dxt1,
    Dxt3,
    Dxt5,
};

struct DdsTextureMip {
    std::uint32_t width = 0u;
    std::uint32_t height = 0u;
    std::uint32_t row_pitch = 0u;
    std::vector<std::uint8_t> bytes;
};

struct DdsTextureData {
    std::uint32_t width = 0u;
    std::uint32_t height = 0u;
    DdsTextureFormat format = DdsTextureFormat::Dxt1;
    std::vector<DdsTextureMip> mips;
};

DdsTextureData parse_dds_texture(const std::uint8_t* data, std::size_t size);
