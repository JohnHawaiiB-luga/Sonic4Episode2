#include "dds_texture.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <stdexcept>
#include <utility>

namespace {

constexpr std::size_t kDdsHeaderBytes = 128u;
constexpr std::uint32_t kDdsHeaderSize = 124u;
constexpr std::uint32_t kDdsPixelFormatSize = 32u;
constexpr std::uint32_t kDdsDepthFlag = 0x00800000u;
constexpr std::uint32_t kDdsPixelFormatFourCc = 0x00000004u;
constexpr std::uint32_t kDdsPixelFormatAlphaPixels = 0x00000001u;
constexpr std::uint32_t kDdsCapsComplex = 0x00000008u;
constexpr std::uint32_t kDdsCapsTexture = 0x00001000u;
constexpr std::uint32_t kDdsCapsMipMap = 0x00400000u;
constexpr std::uint32_t kDdsAllowedCaps = kDdsCapsComplex | kDdsCapsTexture | kDdsCapsMipMap;
constexpr std::uint32_t kDxt1 = 0x31545844u;
constexpr std::uint32_t kDxt3 = 0x33545844u;
constexpr std::uint32_t kDxt5 = 0x35545844u;
constexpr std::size_t kMaximumMipCount = 32u;

struct MipPlan {
    std::uint32_t width;
    std::uint32_t height;
    std::uint32_t row_pitch;
    std::size_t offset;
    std::size_t byte_count;
};

void require(bool condition, const char* message) {
    if (!condition) {
        throw std::invalid_argument(message);
    }
}

std::uint32_t read_le32(const std::uint8_t* data, std::size_t offset) {
    return static_cast<std::uint32_t>(
        static_cast<std::uint32_t>(data[offset]) |
        (static_cast<std::uint32_t>(data[offset + 1u]) << 8u) |
        (static_cast<std::uint32_t>(data[offset + 2u]) << 16u) |
        (static_cast<std::uint32_t>(data[offset + 3u]) << 24u));
}

DdsTextureFormat parse_format(std::uint32_t four_cc) {
    switch (four_cc) {
    case kDxt1:
        return DdsTextureFormat::Dxt1;
    case kDxt3:
        return DdsTextureFormat::Dxt3;
    case kDxt5:
        return DdsTextureFormat::Dxt5;
    default:
        throw std::invalid_argument("DDS texture uses an unsupported fourCC.");
    }
}

std::uint32_t block_byte_count(DdsTextureFormat format) {
    return format == DdsTextureFormat::Dxt1 ? 8u : 16u;
}

std::size_t maximum_mip_count(std::uint32_t width, std::uint32_t height) {
    std::size_t count = 0u;
    for (;;) {
        ++count;
        if (width == 1u && height == 1u) {
            return count;
        }
        width = width > 1u ? width >> 1u : 1u;
        height = height > 1u ? height >> 1u : 1u;
    }
}

}

DdsTextureData parse_dds_texture(const std::uint8_t* data, std::size_t size) {
    require(data != nullptr && size >= kDdsHeaderBytes, "DDS texture header is truncated.");
    require(
        data[0u] == 0x44u && data[1u] == 0x44u && data[2u] == 0x53u && data[3u] == 0x20u,
        "DDS texture magic is invalid.");
    require(read_le32(data, 4u) == kDdsHeaderSize, "DDS texture header size is invalid.");

    const std::uint32_t header_flags = read_le32(data, 8u);
    const std::uint32_t height = read_le32(data, 12u);
    const std::uint32_t width = read_le32(data, 16u);
    const std::uint32_t depth = read_le32(data, 24u);
    const std::uint32_t requested_mip_count = read_le32(data, 28u);
    const std::uint32_t pixel_format_size = read_le32(data, 76u);
    const std::uint32_t pixel_flags = read_le32(data, 80u);
    const std::uint32_t four_cc = read_le32(data, 84u);
    const std::uint32_t caps = read_le32(data, 108u);
    const std::uint32_t caps2 = read_le32(data, 112u);

    require(width != 0u && height != 0u, "DDS texture dimensions must be nonzero.");
    require(depth <= 1u, "DDS texture depth is unsupported.");
    require((header_flags & kDdsDepthFlag) == 0u, "DDS texture depth flag is unsupported.");
    require(pixel_format_size == kDdsPixelFormatSize, "DDS texture pixel format size is invalid.");
    require(
        pixel_flags == kDdsPixelFormatFourCc ||
            pixel_flags == (kDdsPixelFormatFourCc | kDdsPixelFormatAlphaPixels),
        "DDS texture pixel format flags are unsupported.");
    require((caps & ~kDdsAllowedCaps) == 0u, "DDS texture caps are unsupported.");
    require(caps2 == 0u, "DDS texture caps2 is unsupported.");

    const DdsTextureFormat format = parse_format(four_cc);
    const std::size_t mip_count = requested_mip_count == 0u
                                      ? 1u
                                      : static_cast<std::size_t>(requested_mip_count);
    require(mip_count <= kMaximumMipCount, "DDS texture mip count exceeds the supported maximum.");
    require(
        mip_count <= maximum_mip_count(width, height),
        "DDS texture mip count exceeds the dimension chain.");
    std::array<MipPlan, kMaximumMipCount> plans{};
    std::size_t payload_offset = kDdsHeaderBytes;
    std::uint32_t mip_width = width;
    std::uint32_t mip_height = height;
    const std::uint32_t bytes_per_block = block_byte_count(format);
    for (std::size_t mip_index = 0u; mip_index < mip_count; ++mip_index) {
        const std::uint64_t blocks_wide = (static_cast<std::uint64_t>(mip_width) + 3u) / 4u;
        const std::uint64_t blocks_high = (static_cast<std::uint64_t>(mip_height) + 3u) / 4u;
        const std::uint64_t row_pitch = blocks_wide * bytes_per_block;
        require(
            row_pitch <= std::numeric_limits<std::uint32_t>::max(),
            "DDS texture row pitch overflows.");
        const std::uint64_t byte_count = row_pitch * blocks_high;
        require(
            byte_count <= std::numeric_limits<std::size_t>::max(),
            "DDS texture mip payload exceeds addressable memory.");
        const std::size_t mip_bytes = static_cast<std::size_t>(byte_count);
        require(
            payload_offset <= size && mip_bytes <= size - payload_offset,
            "DDS texture payload is truncated.");

        plans[mip_index] = {
            mip_width,
            mip_height,
            static_cast<std::uint32_t>(row_pitch),
            payload_offset,
            mip_bytes,
        };
        payload_offset += mip_bytes;
        mip_width = mip_width > 1u ? mip_width >> 1u : 1u;
        mip_height = mip_height > 1u ? mip_height >> 1u : 1u;
    }
    require(payload_offset == size, "DDS texture has trailing payload bytes.");

    DdsTextureData texture{};
    texture.width = width;
    texture.height = height;
    texture.format = format;
    texture.mips.reserve(mip_count);
    for (std::size_t mip_index = 0u; mip_index < mip_count; ++mip_index) {
        const MipPlan& plan = plans[mip_index];
        DdsTextureMip mip{};
        mip.width = plan.width;
        mip.height = plan.height;
        mip.row_pitch = plan.row_pitch;
        mip.bytes.assign(data + plan.offset, data + plan.offset + plan.byte_count);
        texture.mips.push_back(std::move(mip));
    }
    return texture;
}
