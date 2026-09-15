#include "dds_texture.h"

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <exception>
#include <limits>
#include <stdexcept>
#include <vector>

namespace {

constexpr std::size_t kDdsHeaderBytes = 128u;
constexpr std::uint32_t kDdsHeaderFlags = 0x00001007u;
constexpr std::uint32_t kDdsMipMapCountFlag = 0x00020000u;
constexpr std::uint32_t kDdsDepthFlag = 0x00800000u;
constexpr std::uint32_t kDdsPixelFormatFourCc = 0x00000004u;
constexpr std::uint32_t kDdsCapsComplex = 0x00000008u;
constexpr std::uint32_t kDdsCapsTexture = 0x00001000u;
constexpr std::uint32_t kDdsCapsMipMap = 0x00400000u;
constexpr std::uint32_t kDxt1 = 0x31545844u;
constexpr std::uint32_t kDxt3 = 0x33545844u;
constexpr std::uint32_t kDxt5 = 0x35545844u;
constexpr std::uint32_t kDx10 = 0x30315844u;

struct DdsFixtureSpec {
    std::uint32_t width;
    std::uint32_t height;
    std::uint32_t mip_count;
    std::uint32_t header_flags;
    std::uint32_t depth;
    std::uint32_t pixel_format_size;
    std::uint32_t pixel_flags;
    std::uint32_t four_cc;
    std::uint32_t caps;
    std::uint32_t caps2;
};

bool check(bool condition, const char* message) {
    if (condition) {
        return true;
    }
    std::fprintf(stderr, "%s\n", message);
    return false;
}

void write_le32(std::vector<std::uint8_t>& data, std::size_t offset, std::uint32_t value) {
    data[offset] = static_cast<std::uint8_t>(value & 0xFFu);
    data[offset + 1u] = static_cast<std::uint8_t>((value >> 8u) & 0xFFu);
    data[offset + 2u] = static_cast<std::uint8_t>((value >> 16u) & 0xFFu);
    data[offset + 3u] = static_cast<std::uint8_t>((value >> 24u) & 0xFFu);
}

DdsFixtureSpec make_standard_spec(
    std::uint32_t width,
    std::uint32_t height,
    std::uint32_t mip_count,
    std::uint32_t four_cc) {
    const bool has_multiple_mips = mip_count > 1u;
    return {
        width,
        height,
        mip_count,
        kDdsHeaderFlags | (has_multiple_mips ? kDdsMipMapCountFlag : 0u),
        0u,
        32u,
        kDdsPixelFormatFourCc,
        four_cc,
        kDdsCapsTexture | (has_multiple_mips ? kDdsCapsComplex | kDdsCapsMipMap : 0u),
        0u,
    };
}

std::vector<std::uint8_t> make_dds(
    const DdsFixtureSpec& spec,
    const std::vector<std::vector<std::uint8_t>>& payloads) {
    std::vector<std::uint8_t> data(kDdsHeaderBytes, 0u);
    data[0u] = 0x44u;
    data[1u] = 0x44u;
    data[2u] = 0x53u;
    data[3u] = 0x20u;
    write_le32(data, 4u, 124u);
    write_le32(data, 8u, spec.header_flags);
    write_le32(data, 12u, spec.height);
    write_le32(data, 16u, spec.width);
    write_le32(data, 24u, spec.depth);
    write_le32(data, 28u, spec.mip_count);
    write_le32(data, 76u, spec.pixel_format_size);
    write_le32(data, 80u, spec.pixel_flags);
    write_le32(data, 84u, spec.four_cc);
    write_le32(data, 108u, spec.caps);
    write_le32(data, 112u, spec.caps2);
    for (const std::vector<std::uint8_t>& payload : payloads) {
        data.insert(data.end(), payload.begin(), payload.end());
    }
    return data;
}

std::vector<std::uint8_t> make_bytes(std::size_t length, std::uint8_t seed) {
    std::vector<std::uint8_t> bytes(length, 0u);
    for (std::size_t index = 0u; index < bytes.size(); ++index) {
        bytes[index] = static_cast<std::uint8_t>(seed + static_cast<std::uint8_t>(index));
    }
    return bytes;
}

bool check_mip(
    const DdsTextureMip& mip,
    std::uint32_t width,
    std::uint32_t height,
    std::uint32_t row_pitch,
    const std::vector<std::uint8_t>& bytes,
    const char* message) {
    return check(
        mip.width == width && mip.height == height && mip.row_pitch == row_pitch && mip.bytes == bytes,
        message);
}

bool rejects(const std::vector<std::uint8_t>& bytes, const char* message) {
    try {
        static_cast<void>(parse_dds_texture(bytes.empty() ? nullptr : bytes.data(), bytes.size()));
    } catch (const std::invalid_argument&) {
        return true;
    } catch (const std::exception&) {
        return check(false, message);
    }
    return check(false, message);
}

bool test_parses_actual_like_zero_mip_dxt1_and_owns_bytes() {
    const std::vector<std::uint8_t> expected_payload = make_bytes(4096u, 0x11u);
    DdsTextureData parsed{};
    {
        std::vector<std::uint8_t> input = make_dds(
            make_standard_spec(128u, 64u, 0u, kDxt1),
            {expected_payload});
        parsed = parse_dds_texture(input.data(), input.size());
        input.assign(input.size(), 0u);
    }

    return check(parsed.width == 128u && parsed.height == 64u, "128x64 DXT1 dimensions mismatch") &&
           check(parsed.format == DdsTextureFormat::Dxt1, "128x64 DXT1 format mismatch") &&
           check(parsed.mips.size() == 1u, "zero DDS mip count did not preserve one stored level") &&
           check_mip(
               parsed.mips[0u],
               128u,
               64u,
               256u,
               expected_payload,
               "128x64 DXT1 mip pitch or owned payload mismatch");
}

bool test_preserves_partial_dxt5_chain() {
    const std::vector<std::uint8_t> level0 = make_bytes(64u, 0x20u);
    const std::vector<std::uint8_t> level1 = make_bytes(16u, 0x50u);
    const DdsFixtureSpec spec = make_standard_spec(7u, 5u, 2u, kDxt5);
    const std::vector<std::uint8_t> input = make_dds(spec, {level0, level1});
    const DdsTextureData parsed = parse_dds_texture(input.data(), input.size());

    return check(parsed.width == 7u && parsed.height == 5u, "partial DXT5 dimensions mismatch") &&
           check(parsed.format == DdsTextureFormat::Dxt5, "partial DXT5 format mismatch") &&
           check(parsed.mips.size() == 2u, "legal partial DXT5 mip chain mismatch") &&
           check_mip(parsed.mips[0u], 7u, 5u, 32u, level0, "partial DXT5 level zero mismatch") &&
           check_mip(parsed.mips[1u], 3u, 2u, 16u, level1, "partial DXT5 level one mismatch");
}

bool test_accepts_omitted_presence_flags() {
    const std::vector<std::uint8_t> level0 = make_bytes(8u, 0x90u);
    const std::vector<std::uint8_t> level1 = make_bytes(8u, 0xB0u);
    DdsFixtureSpec missing_mip_flag = make_standard_spec(4u, 4u, 2u, kDxt1);
    DdsFixtureSpec zero_header_flags = missing_mip_flag;
    DdsFixtureSpec zero_caps = missing_mip_flag;
    missing_mip_flag.header_flags &= ~kDdsMipMapCountFlag;
    zero_header_flags.header_flags = 0u;
    zero_caps.caps = 0u;

    const std::vector<std::uint8_t> without_mip_flag_input = make_dds(
        missing_mip_flag,
        {level0, level1});
    const std::vector<std::uint8_t> without_header_flags_input = make_dds(
        zero_header_flags,
        {level0, level1});
    const std::vector<std::uint8_t> without_caps_input = make_dds(zero_caps, {level0, level1});
    const DdsTextureData without_mip_flag = parse_dds_texture(
        without_mip_flag_input.data(),
        without_mip_flag_input.size());
    const DdsTextureData without_header_flags = parse_dds_texture(
        without_header_flags_input.data(),
        without_header_flags_input.size());
    const DdsTextureData without_caps = parse_dds_texture(
        without_caps_input.data(),
        without_caps_input.size());

    return check(without_mip_flag.format == DdsTextureFormat::Dxt1, "DDS without mip flag format mismatch") &&
           check(without_header_flags.format == DdsTextureFormat::Dxt1, "DDS without header flags format mismatch") &&
           check(without_caps.format == DdsTextureFormat::Dxt1, "DDS without caps format mismatch") &&
           check(without_mip_flag.mips.size() == 2u, "DDS without mip flag mip count mismatch") &&
           check(without_header_flags.mips.size() == 2u, "DDS without header flags mip count mismatch") &&
           check(without_caps.mips.size() == 2u, "DDS without caps mip count mismatch") &&
           check_mip(without_mip_flag.mips[0u], 4u, 4u, 8u, level0, "DDS without mip flag level zero mismatch") &&
           check_mip(without_mip_flag.mips[1u], 2u, 2u, 8u, level1, "DDS without mip flag level one mismatch") &&
           check_mip(without_header_flags.mips[0u], 4u, 4u, 8u, level0, "DDS without header flags level zero mismatch") &&
           check_mip(without_header_flags.mips[1u], 2u, 2u, 8u, level1, "DDS without header flags level one mismatch") &&
           check_mip(without_caps.mips[0u], 4u, 4u, 8u, level0, "DDS without caps level zero mismatch") &&
           check_mip(without_caps.mips[1u], 2u, 2u, 8u, level1, "DDS without caps level one mismatch");
}

bool test_parses_nonmultiple_and_single_block_formats() {
    DdsFixtureSpec dxt3_spec = make_standard_spec(5u, 3u, 1u, kDxt3);
    dxt3_spec.pixel_flags = kDdsPixelFormatFourCc | 0x00000001u;
    dxt3_spec.depth = 1u;
    const std::vector<std::uint8_t> dxt3_payload = make_bytes(32u, 0x30u);
    const std::vector<std::uint8_t> dxt3_input = make_dds(dxt3_spec, {dxt3_payload});
    const DdsTextureData dxt3 = parse_dds_texture(dxt3_input.data(), dxt3_input.size());

    const std::vector<std::uint8_t> dxt1_payload = make_bytes(8u, 0x70u);
    const std::vector<std::uint8_t> dxt1_input = make_dds(
        make_standard_spec(1u, 1u, 1u, kDxt1),
        {dxt1_payload});
    const DdsTextureData dxt1 = parse_dds_texture(dxt1_input.data(), dxt1_input.size());

    return check(dxt3.format == DdsTextureFormat::Dxt3, "DXT3 format mismatch") &&
           check(dxt3.mips.size() == 1u, "DXT3 mip count mismatch") &&
           check_mip(dxt3.mips[0u], 5u, 3u, 32u, dxt3_payload, "nonmultiple DXT3 mip mismatch") &&
           check(dxt1.mips.size() == 1u, "1x1 DXT1 mip count mismatch") &&
           check_mip(dxt1.mips[0u], 1u, 1u, 8u, dxt1_payload, "1x1 DXT1 mip mismatch");
}

bool test_rejects_truncated_and_trailing_data() {
    const std::vector<std::uint8_t> payload = make_bytes(8u, 0x10u);
    const std::vector<std::uint8_t> complete = make_dds(
        make_standard_spec(4u, 4u, 1u, kDxt1),
        {payload});
    std::vector<std::uint8_t> truncated_payload = complete;
    truncated_payload.pop_back();
    std::vector<std::uint8_t> trailing_payload = complete;
    trailing_payload.push_back(0xFFu);

    const std::vector<std::uint8_t> level0 = make_bytes(64u, 0x20u);
    const std::vector<std::uint8_t> level1 = make_bytes(16u, 0x50u);
    const std::vector<std::uint8_t> level2 = make_bytes(16u, 0x80u);
    std::vector<std::uint8_t> truncated_last_mip = make_dds(
        make_standard_spec(7u, 5u, 3u, kDxt5),
        {level0, level1, level2});
    truncated_last_mip.pop_back();

    return rejects(std::vector<std::uint8_t>(127u, 0u), "truncated DDS header was accepted") &&
           rejects(truncated_payload, "truncated DDS payload was accepted") &&
           rejects(truncated_last_mip, "truncated final DDS mip was accepted") &&
           rejects(trailing_payload, "DDS trailing payload byte was accepted");
}

bool test_rejects_header_and_pixel_format_domains() {
    const std::vector<std::uint8_t> payload = make_bytes(8u, 0x40u);
    DdsFixtureSpec bad_header_size = make_standard_spec(4u, 4u, 1u, kDxt1);
    DdsFixtureSpec bad_pixel_format_size = bad_header_size;
    DdsFixtureSpec contradictory_pixel_flags = bad_header_size;
    DdsFixtureSpec rgb_pixel_flags = bad_header_size;
    DdsFixtureSpec unknown_four_cc = bad_header_size;
    DdsFixtureSpec dx10_four_cc = bad_header_size;
    bad_pixel_format_size.pixel_format_size = 31u;
    contradictory_pixel_flags.pixel_flags = kDdsPixelFormatFourCc | 0x00000040u;
    rgb_pixel_flags.pixel_flags = 0x00000040u;
    unknown_four_cc.four_cc = 0x12345678u;
    dx10_four_cc.four_cc = kDx10;

    std::vector<std::uint8_t> wrong_header = make_dds(bad_header_size, {payload});
    write_le32(wrong_header, 4u, 123u);
    std::vector<std::uint8_t> wrong_magic = make_dds(bad_header_size, {payload});
    wrong_magic[0u] = 0u;
    return rejects(std::vector<std::uint8_t>{}, "null DDS input was accepted") &&
           rejects(wrong_magic, "invalid DDS magic was accepted") &&
           rejects(wrong_header, "invalid DDS header size was accepted") &&
           rejects(
               make_dds(bad_pixel_format_size, {payload}),
               "invalid DDS pixel format size was accepted") &&
           rejects(
               make_dds(contradictory_pixel_flags, {payload}),
               "contradictory DDS pixel flags were accepted") &&
           rejects(make_dds(rgb_pixel_flags, {payload}), "RGB DDS pixel flags were accepted") &&
           rejects(make_dds(unknown_four_cc, {payload}), "unknown DDS fourCC was accepted") &&
           rejects(make_dds(dx10_four_cc, {payload}), "DX10 DDS header was accepted");
}

bool test_rejects_dimensions_mips_caps_and_depth() {
    const std::vector<std::uint8_t> payload = make_bytes(8u, 0x60u);
    DdsFixtureSpec zero_width = make_standard_spec(4u, 4u, 1u, kDxt1);
    DdsFixtureSpec zero_height = zero_width;
    DdsFixtureSpec huge_dimensions = zero_width;
    DdsFixtureSpec excessive_mips = make_standard_spec(1u, 1u, 33u, kDxt1);
    DdsFixtureSpec overlong_chain = make_standard_spec(1u, 1u, 2u, kDxt1);
    DdsFixtureSpec unsupported_caps = zero_width;
    DdsFixtureSpec cube_caps = zero_width;
    DdsFixtureSpec nonzero_depth = zero_width;
    DdsFixtureSpec depth_flag = zero_width;
    zero_width.width = 0u;
    zero_height.height = 0u;
    huge_dimensions.width = std::numeric_limits<std::uint32_t>::max();
    huge_dimensions.height = std::numeric_limits<std::uint32_t>::max();
    unsupported_caps.caps = kDdsCapsTexture | 0x00000002u;
    cube_caps.caps2 = 0x00000200u;
    nonzero_depth.depth = 2u;
    depth_flag.header_flags |= kDdsDepthFlag;

    return rejects(make_dds(zero_width, {payload}), "zero DDS width was accepted") &&
           rejects(make_dds(zero_height, {payload}), "zero DDS height was accepted") &&
           rejects(make_dds(huge_dimensions, {}), "huge DDS dimensions were accepted") &&
           rejects(make_dds(excessive_mips, {}), "DDS mip count above 32 was accepted") &&
           rejects(make_dds(overlong_chain, {}), "DDS mip count beyond the dimension chain was accepted") &&
           rejects(make_dds(unsupported_caps, {payload}), "unsupported DDS caps were accepted") &&
           rejects(make_dds(cube_caps, {payload}), "cube DDS caps were accepted") &&
           rejects(make_dds(nonzero_depth, {payload}), "non-2D DDS depth was accepted") &&
           rejects(make_dds(depth_flag, {payload}), "DDS depth flag was accepted");
}

}

int main() {
    return test_parses_actual_like_zero_mip_dxt1_and_owns_bytes() &&
                   test_preserves_partial_dxt5_chain() &&
                   test_accepts_omitted_presence_flags() &&
                   test_parses_nonmultiple_and_single_block_formats() &&
                   test_rejects_truncated_and_trailing_data() &&
                   test_rejects_header_and_pixel_format_domains() &&
                   test_rejects_dimensions_mips_caps_and_depth()
               ? 0
               : 1;
}
