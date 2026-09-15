#include "nn_motion_data.h"
#include "stage_data.h"

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <initializer_list>
#include <stdexcept>
#include <utility>
#include <vector>

namespace {

constexpr std::size_t kDataBase = 0x20u;
constexpr std::size_t kChunkHeaderSize = 8u;
constexpr std::size_t kMotionHeaderSize = 32u;
constexpr std::size_t kChannelSize = 40u;
constexpr std::uint32_t kVersion2Flag = 0x10000000u;
constexpr std::uint32_t kMotionRootRelative = 0x40u;
constexpr std::uint32_t kChannelRelative = 0x80u;
constexpr std::uint32_t kSharedKeyRelative = 0x140u;
constexpr std::uint32_t kSecondKeyRelative = 0x14Cu;
constexpr std::uint32_t kRawKeyRelative = 0x154u;

struct ChannelSpec {
    std::uint32_t flags;
    std::uint32_t interpolation_flags;
    std::uint32_t target_bits;
    std::uint32_t start_bits;
    std::uint32_t end_bits;
    std::uint32_t start_key_bits;
    std::uint32_t end_key_bits;
    std::uint32_t key_count;
    std::uint32_t key_stride;
    std::uint32_t key_relative;
    std::vector<std::uint8_t> key_bytes;
};

struct Fixture {
    std::vector<std::uint8_t> data;
    std::size_t nzmo_offset;
    std::size_t nof0_offset;
    std::size_t nfn0_offset;
    std::size_t root_offset;
    std::vector<std::size_t> channel_offsets;
    std::vector<std::size_t> relocation_entries;
};

bool check(bool condition, const char* message) {
    if (condition) {
        return true;
    }
    std::fprintf(stderr, "%s\n", message);
    return false;
}

void write_le16(std::vector<std::uint8_t>& data, std::size_t offset, std::uint16_t value) {
    data[offset] = static_cast<std::uint8_t>(value & 0xffu);
    data[offset + 1u] = static_cast<std::uint8_t>((value >> 8u) & 0xffu);
}

void write_le32(std::vector<std::uint8_t>& data, std::size_t offset, std::uint32_t value) {
    data[offset] = static_cast<std::uint8_t>(value & 0xffu);
    data[offset + 1u] = static_cast<std::uint8_t>((value >> 8u) & 0xffu);
    data[offset + 2u] = static_cast<std::uint8_t>((value >> 16u) & 0xffu);
    data[offset + 3u] = static_cast<std::uint8_t>((value >> 24u) & 0xffu);
}

std::uint32_t read_le32(const std::vector<std::uint8_t>& data, std::size_t offset) {
    return static_cast<std::uint32_t>(
        static_cast<std::uint32_t>(data[offset]) |
        (static_cast<std::uint32_t>(data[offset + 1u]) << 8u) |
        (static_cast<std::uint32_t>(data[offset + 2u]) << 16u) |
        (static_cast<std::uint32_t>(data[offset + 3u]) << 24u));
}

std::uint32_t float_bits(float value) {
    std::uint32_t bits = 0u;
    std::memcpy(&bits, &value, sizeof(bits));
    return bits;
}

void write_tag(
    std::vector<std::uint8_t>& data,
    std::size_t offset,
    const char (&tag)[5]) {
    for (std::size_t index = 0u; index < 4u; ++index) {
        data[offset + index] = static_cast<std::uint8_t>(tag[index]);
    }
}

std::size_t align_four(std::size_t value) {
    return (value + 3u) & ~std::size_t(3u);
}

std::vector<std::uint8_t> packed_a16(std::initializer_list<NnMotionA16Key> keys) {
    std::vector<std::uint8_t> bytes;
    bytes.reserve(keys.size() * 4u);
    for (const NnMotionA16Key& key : keys) {
        const std::uint32_t packed =
            static_cast<std::uint32_t>(static_cast<std::uint16_t>(key.frame)) |
            (static_cast<std::uint32_t>(static_cast<std::uint16_t>(key.value)) << 16u);
        bytes.push_back(static_cast<std::uint8_t>(packed & 0xffu));
        bytes.push_back(static_cast<std::uint8_t>((packed >> 8u) & 0xffu));
        bytes.push_back(static_cast<std::uint8_t>((packed >> 16u) & 0xffu));
        bytes.push_back(static_cast<std::uint8_t>((packed >> 24u) & 0xffu));
    }
    return bytes;
}

std::vector<std::uint8_t> packed_float(
    std::initializer_list<std::pair<std::uint32_t, std::uint32_t>> keys) {
    std::vector<std::uint8_t> bytes;
    bytes.reserve(keys.size() * 8u);
    for (const std::pair<std::uint32_t, std::uint32_t>& key : keys) {
        const std::size_t offset = bytes.size();
        bytes.resize(offset + 8u);
        write_le32(bytes, offset, key.first);
        write_le32(bytes, offset + 4u, key.second);
    }
    return bytes;
}

void copy_bytes(
    std::vector<std::uint8_t>& destination,
    std::size_t offset,
    const std::vector<std::uint8_t>& source) {
    for (std::size_t index = 0u; index < source.size(); ++index) {
        destination[offset + index] = source[index];
    }
}

void append_bytes(
    std::vector<std::uint8_t>& destination,
    const std::vector<std::uint8_t>& source,
    std::size_t offset,
    std::size_t length) {
    for (std::size_t index = 0u; index < length; ++index) {
        destination.push_back(source[offset + index]);
    }
}

Fixture make_motion_fixture(const std::vector<ChannelSpec>& channels) {
    std::size_t maximum_absolute = kDataBase + kMotionRootRelative + kMotionHeaderSize;
    if (!channels.empty()) {
        maximum_absolute = kDataBase + kChannelRelative + channels.size() * kChannelSize;
    }
    for (const ChannelSpec& channel : channels) {
        if (channel.key_count == 0u) {
            if (channel.key_relative != 0u || !channel.key_bytes.empty()) {
                throw std::runtime_error("zero-count synthetic channel has key data");
            }
            continue;
        }
        if (channel.key_stride == 0u ||
            channel.key_bytes.size() !=
                static_cast<std::size_t>(channel.key_count) * channel.key_stride) {
            throw std::runtime_error("synthetic channel key span is inconsistent");
        }
        const std::size_t key_end =
            kDataBase + static_cast<std::size_t>(channel.key_relative) + channel.key_bytes.size();
        if (key_end > maximum_absolute) {
            maximum_absolute = key_end;
        }
    }

    const std::size_t nzmo_offset = kDataBase;
    const std::size_t nzmo_payload_size =
        align_four(maximum_absolute - (nzmo_offset + kChunkHeaderSize));
    const std::size_t nof0_offset = nzmo_offset + kChunkHeaderSize + nzmo_payload_size;
    std::vector<std::uint32_t> relocations;
    if (!channels.empty()) {
        relocations.push_back(kMotionRootRelative + 0x10u);
    }
    for (std::size_t index = 0u; index < channels.size(); ++index) {
        if (channels[index].key_count != 0u) {
            relocations.push_back(
                kChannelRelative + static_cast<std::uint32_t>(index * kChannelSize) + 36u);
        }
    }

    const std::size_t nof0_payload_size = 8u + relocations.size() * 4u;
    const std::size_t nfn0_offset = nof0_offset + kChunkHeaderSize + nof0_payload_size;
    const std::size_t nend_offset = nfn0_offset + kChunkHeaderSize + 24u;
    Fixture fixture{
        std::vector<std::uint8_t>(nend_offset + 16u, 0u),
        nzmo_offset,
        nof0_offset,
        nfn0_offset,
        kDataBase + kMotionRootRelative,
        {},
        {},
    };

    write_tag(fixture.data, 0u, "NZIF");
    write_le32(fixture.data, 4u, 24u);
    write_le32(fixture.data, 8u, 1u);
    write_le32(fixture.data, 12u, static_cast<std::uint32_t>(kDataBase));
    write_le32(
        fixture.data,
        16u,
        static_cast<std::uint32_t>(nof0_offset - kDataBase));
    write_le32(fixture.data, 20u, static_cast<std::uint32_t>(nof0_offset));
    write_le32(
        fixture.data,
        24u,
        static_cast<std::uint32_t>(kChunkHeaderSize + nof0_payload_size));
    write_le32(fixture.data, 28u, 1u);

    write_tag(fixture.data, nzmo_offset, "NZMO");
    write_le32(
        fixture.data,
        nzmo_offset + 4u,
        static_cast<std::uint32_t>(nzmo_payload_size));
    write_le32(fixture.data, nzmo_offset + 8u, kMotionRootRelative);
    write_le32(fixture.data, nzmo_offset + 12u, 0u);

    write_le32(fixture.data, fixture.root_offset, kVersion2Flag | 0x00010001u);
    write_le32(fixture.data, fixture.root_offset + 4u, 0x80000000u);
    write_le32(fixture.data, fixture.root_offset + 8u, 0x42700000u);
    write_le32(fixture.data, fixture.root_offset + 12u, static_cast<std::uint32_t>(channels.size()));
    write_le32(
        fixture.data,
        fixture.root_offset + 16u,
        channels.empty() ? 0u : kChannelRelative);
    write_le32(fixture.data, fixture.root_offset + 20u, 0x42700000u);
    write_le32(fixture.data, fixture.root_offset + 24u, 0x7fc01234u);
    write_le32(fixture.data, fixture.root_offset + 28u, 0x80000000u);

    for (std::size_t index = 0u; index < channels.size(); ++index) {
        const ChannelSpec& spec = channels[index];
        const std::size_t channel_offset =
            kDataBase + kChannelRelative + index * kChannelSize;
        fixture.channel_offsets.push_back(channel_offset);
        write_le32(fixture.data, channel_offset, spec.flags);
        write_le32(fixture.data, channel_offset + 4u, spec.interpolation_flags);
        write_le32(fixture.data, channel_offset + 8u, spec.target_bits);
        write_le32(fixture.data, channel_offset + 12u, spec.start_bits);
        write_le32(fixture.data, channel_offset + 16u, spec.end_bits);
        write_le32(fixture.data, channel_offset + 20u, spec.start_key_bits);
        write_le32(fixture.data, channel_offset + 24u, spec.end_key_bits);
        write_le32(fixture.data, channel_offset + 28u, spec.key_count);
        write_le32(fixture.data, channel_offset + 32u, spec.key_stride);
        write_le32(fixture.data, channel_offset + 36u, spec.key_relative);
        if (!spec.key_bytes.empty()) {
            copy_bytes(
                fixture.data,
                kDataBase + static_cast<std::size_t>(spec.key_relative),
                spec.key_bytes);
        }
    }

    write_tag(fixture.data, nof0_offset, "NOF0");
    write_le32(
        fixture.data,
        nof0_offset + 4u,
        static_cast<std::uint32_t>(nof0_payload_size));
    write_le32(fixture.data, nof0_offset + 8u, static_cast<std::uint32_t>(relocations.size()));
    write_le32(fixture.data, nof0_offset + 12u, 0u);
    for (std::size_t index = 0u; index < relocations.size(); ++index) {
        const std::size_t entry = nof0_offset + 16u + index * 4u;
        write_le32(fixture.data, entry, relocations[index]);
        fixture.relocation_entries.push_back(entry);
    }

    write_tag(fixture.data, nfn0_offset, "NFN0");
    write_le32(fixture.data, nfn0_offset + 4u, 24u);
    const char name[] = "fixture.znm";
    for (std::size_t index = 0u; index < sizeof(name); ++index) {
        fixture.data[nfn0_offset + 16u + index] = static_cast<std::uint8_t>(name[index]);
    }
    write_tag(fixture.data, nend_offset, "NEND");
    write_le32(fixture.data, nend_offset + 4u, 8u);
    return fixture;
}

Fixture make_valid_fixture() {
    const std::vector<std::uint8_t> shared_keys = packed_a16({
        {-10, -300},
        {0, 7},
        {8, -2},
    });
    const std::vector<std::uint8_t> second_keys = packed_a16({
        {-4, 10},
        {6, -20},
    });
    const std::vector<std::uint8_t> raw_keys = packed_float({
        {0xbf800000u, 0x80000000u},
        {0x40000000u, 0x00000001u},
    });
    return make_motion_fixture({
        {
            0x812u,
            0x20002u,
            0xfffffff9u,
            0x80000000u,
            0x42700000u,
            0xc0000000u,
            0x3f800000u,
            3u,
            4u,
            kSharedKeyRelative,
            shared_keys,
        },
        {
            0x1012u,
            0x20004u,
            42u,
            0x00000000u,
            0x42700000u,
            0xbf800000u,
            0x3f000000u,
            3u,
            4u,
            kSharedKeyRelative,
            shared_keys,
        },
        {
            0x2012u,
            0x20002u,
            0xfffffffeu,
            0x80000000u,
            0x3f800000u,
            0x00000000u,
            0x3f800000u,
            2u,
            4u,
            kSecondKeyRelative,
            second_keys,
        },
        {
            0x101u,
            0x20002u,
            99u,
            0x3f800000u,
            0x40000000u,
            0x3f800000u,
            0x40000000u,
            2u,
            8u,
            kRawKeyRelative,
            raw_keys,
        },
    });
}

template <typename Callable>
bool expects_stage_data_error(Callable&& callable, const char* message) {
    try {
        std::forward<Callable>(callable)();
    } catch (const StageDataError&) {
        return true;
    } catch (...) {
        std::fprintf(stderr, "%s (wrong exception type)\n", message);
        return false;
    }
    std::fprintf(stderr, "%s\n", message);
    return false;
}

bool test_parses_raw_motion_and_coalesces_key_storage() {
    Fixture fixture = make_valid_fixture();
    const std::vector<std::uint8_t> input_before = fixture.data;
    const NnMotionData motion = parse_pc_nn_motion(fixture.data.data(), fixture.data.size());

    std::vector<std::uint8_t> expected_key_data;
    append_bytes(
        expected_key_data,
        fixture.data,
        kDataBase + kSharedKeyRelative,
        kSecondKeyRelative - kSharedKeyRelative + 8u);
    append_bytes(expected_key_data, fixture.data, kDataBase + kRawKeyRelative, 16u);

    const std::vector<NnMotionA16Key> first = nn_motion_a16_keys(motion, 0u);
    const std::vector<NnMotionA16Key> constant = nn_motion_a16_keys(motion, 1u);
    const std::vector<NnMotionA16Key> third = nn_motion_a16_keys(motion, 2u);
    const std::vector<NnMotionFloatKey> scalar = nn_motion_float_keys(motion, 3u);
    return check(fixture.data == input_before, "motion parser modified its input") &&
           check(
               motion.flags == (kVersion2Flag | 0x00010001u) &&
                   motion.start_bits == 0x80000000u &&
                   motion.end_bits == 0x42700000u &&
                   motion.frame_rate_bits == 0x42700000u &&
                   motion.reserved_bits[0u] == 0x7fc01234u &&
                   motion.reserved_bits[1u] == 0x80000000u &&
                   motion.channel_count == 4u,
               "motion header raw bits were not preserved") &&
           check(motion.channels.size() == 4u, "motion channel count mismatch") &&
           check(
               motion.channels[0u].flags == 0x812u &&
                   motion.channels[0u].interpolation_flags == 0x20002u &&
                   motion.channels[0u].target == -7 &&
                   motion.channels[0u].start_bits == 0x80000000u &&
                   motion.channels[0u].end_bits == 0x42700000u &&
                   motion.channels[0u].start_key_bits == 0xc0000000u &&
                   motion.channels[0u].end_key_bits == 0x3f800000u &&
                   motion.channels[0u].key_count == 3u &&
                   motion.channels[0u].key_stride == 4u &&
                   motion.channels[0u].key_data_offset == 0u,
               "first motion channel raw fields mismatch") &&
           check(
               motion.channels[1u].flags == 0x1012u &&
                   motion.channels[1u].interpolation_flags == 0x20004u &&
                   motion.channels[1u].target == 42 &&
                   motion.channels[1u].key_data_offset == 0u &&
                   motion.channels[2u].flags == 0x2012u &&
                   motion.channels[2u].target == -2 &&
                   motion.channels[2u].key_data_offset == 12u &&
                   motion.channels[3u].flags == 0x101u &&
                   motion.channels[3u].key_stride == 8u &&
                   motion.channels[3u].key_data_offset == 20u,
               "motion channel sharing or raw records changed") &&
           check(
               motion.key_data == expected_key_data && motion.key_data.size() == 36u,
               "shared motion key spans multiplied owned storage") &&
           check(
               first.size() == 3u &&
                   first[0u].frame == -10 &&
                   first[0u].value == -300 &&
                   first[1u].frame == 0 &&
                   first[1u].value == 7 &&
                   first[2u].frame == 8 &&
                   first[2u].value == -2,
               "first packed A16 channel decoded incorrectly") &&
           check(
               constant.size() == first.size() &&
                   constant[0u].frame == first[0u].frame &&
                   constant[2u].value == first[2u].value,
               "constant interpolation A16 channel did not remain decodable") &&
           check(
               third.size() == 2u &&
                   third[0u].frame == -4 &&
                   third[0u].value == 10 &&
                   third[1u].frame == 6 &&
                   third[1u].value == -20,
               "third packed A16 channel decoded incorrectly") &&
           check(
               scalar.size() == 2u &&
                   float_bits(scalar[0u].frame) == 0xbf800000u &&
                   float_bits(scalar[0u].value) == 0x80000000u &&
                   float_bits(scalar[1u].frame) == 0x40000000u &&
                   float_bits(scalar[1u].value) == 0x00000001u,
               "scalar float channel did not preserve raw float bits");
}

bool test_accepts_explicit_empty_counts() {
    Fixture empty_motion = make_motion_fixture({});
    const std::vector<std::uint8_t> empty_input = empty_motion.data;
    const NnMotionData parsed_empty = parse_pc_nn_motion(
        empty_motion.data.data(),
        empty_motion.data.size());

    const ChannelSpec empty_channel{
        0x812u,
        0x20002u,
        0u,
        0u,
        0u,
        0u,
        0u,
        0u,
        0u,
        0u,
        {},
    };
    Fixture zero_keys = make_motion_fixture({empty_channel});
    const NnMotionData parsed_zero_keys = parse_pc_nn_motion(
        zero_keys.data.data(),
        zero_keys.data.size());

    return check(
               empty_motion.data == empty_input &&
                   parsed_empty.channel_count == 0u &&
                   parsed_empty.channels.empty() &&
                   parsed_empty.key_data.empty(),
               "zero-channel motion was not accepted explicitly") &&
           check(
               parsed_zero_keys.channels.size() == 1u &&
                   parsed_zero_keys.channel_count == 1u &&
                   parsed_zero_keys.channels[0u].key_count == 0u &&
                   parsed_zero_keys.channels[0u].key_stride == 0u &&
                   parsed_zero_keys.key_data.empty(),
               "zero-key channel was not retained explicitly") &&
           expects_stage_data_error(
               [&]() { static_cast<void>(nn_motion_a16_keys(parsed_zero_keys, 0u)); },
               "zero-key A16 channel was decoded");
}

bool test_rejects_bad_envelopes_and_relocations() {
    Fixture valid = make_valid_fixture();
    if (!expects_stage_data_error(
            []() { static_cast<void>(parse_pc_nn_motion(nullptr, 1u)); },
            "null motion input was accepted") ||
        !expects_stage_data_error(
            [&]() {
                static_cast<void>(parse_pc_nn_motion(valid.data.data(), valid.data.size() - 1u));
            },
            "truncated motion container was accepted")) {
        return false;
    }

    Fixture bad_chunk = make_valid_fixture();
    bad_chunk.data[bad_chunk.nzmo_offset] = static_cast<std::uint8_t>('X');
    if (!expects_stage_data_error(
            [&]() { static_cast<void>(parse_pc_nn_motion(bad_chunk.data.data(), bad_chunk.data.size())); },
            "unsupported motion chunk was accepted")) {
        return false;
    }

    Fixture bad_version = make_valid_fixture();
    write_le32(bad_version.data, 28u, 2u);
    if (!expects_stage_data_error(
            [&]() { static_cast<void>(parse_pc_nn_motion(bad_version.data.data(), bad_version.data.size())); },
            "unsupported NZIF version was accepted")) {
        return false;
    }

    Fixture bad_root_version = make_valid_fixture();
    write_le32(bad_root_version.data, bad_root_version.nzmo_offset + 12u, 1u);
    if (!expects_stage_data_error(
            [&]() {
                static_cast<void>(parse_pc_nn_motion(
                    bad_root_version.data.data(),
                    bad_root_version.data.size()));
            },
            "unsupported NZMO root version was accepted")) {
        return false;
    }

    Fixture trailing = make_valid_fixture();
    trailing.data.push_back(0u);
    if (!expects_stage_data_error(
            [&]() { static_cast<void>(parse_pc_nn_motion(trailing.data.data(), trailing.data.size())); },
            "motion data after NEND was accepted")) {
        return false;
    }

    Fixture short_name = make_valid_fixture();
    write_le32(short_name.data, short_name.nfn0_offset + 4u, 8u);
    if (!expects_stage_data_error(
            [&]() { static_cast<void>(parse_pc_nn_motion(short_name.data.data(), short_name.data.size())); },
            "truncated source-name envelope was accepted")) {
        return false;
    }

    Fixture huge_relocation_count = make_valid_fixture();
    write_le32(huge_relocation_count.data, huge_relocation_count.nof0_offset + 8u, 0x7fffffffu);
    if (!expects_stage_data_error(
            [&]() {
                static_cast<void>(parse_pc_nn_motion(
                    huge_relocation_count.data.data(),
                    huge_relocation_count.data.size()));
            },
            "overflowing relocation count was accepted")) {
        return false;
    }

    Fixture out_of_range_location = make_valid_fixture();
    write_le32(
        out_of_range_location.data,
        out_of_range_location.relocation_entries[0u],
        0xfffffffcu);
    if (!expects_stage_data_error(
            [&]() {
                static_cast<void>(parse_pc_nn_motion(
                    out_of_range_location.data.data(),
                    out_of_range_location.data.size()));
            },
            "out-of-range relocation location was accepted")) {
        return false;
    }

    Fixture unaligned_location = make_valid_fixture();
    write_le32(
        unaligned_location.data,
        unaligned_location.relocation_entries[0u],
        read_le32(unaligned_location.data, unaligned_location.relocation_entries[0u]) + 1u);
    if (!expects_stage_data_error(
            [&]() {
                static_cast<void>(parse_pc_nn_motion(
                    unaligned_location.data.data(),
                    unaligned_location.data.size()));
            },
            "unaligned relocation location was accepted")) {
        return false;
    }

    Fixture duplicate_location = make_valid_fixture();
    write_le32(
        duplicate_location.data,
        duplicate_location.relocation_entries[1u],
        read_le32(duplicate_location.data, duplicate_location.relocation_entries[0u]));
    if (!expects_stage_data_error(
            [&]() {
                static_cast<void>(parse_pc_nn_motion(
                    duplicate_location.data.data(),
                    duplicate_location.data.size()));
            },
            "duplicate relocation location was accepted")) {
        return false;
    }

    Fixture zero_target = make_valid_fixture();
    write_le32(zero_target.data, zero_target.channel_offsets[0u] + 36u, 0u);
    if (!expects_stage_data_error(
            [&]() { static_cast<void>(parse_pc_nn_motion(zero_target.data.data(), zero_target.data.size())); },
            "zero relocation target was accepted")) {
        return false;
    }

    Fixture missing_channel_location = make_valid_fixture();
    write_le32(
        missing_channel_location.data,
        missing_channel_location.relocation_entries[0u],
        kMotionRootRelative + 28u);
    write_le32(
        missing_channel_location.data,
        missing_channel_location.root_offset + 28u,
        kMotionRootRelative);
    if (!expects_stage_data_error(
            [&]() {
                static_cast<void>(parse_pc_nn_motion(
                    missing_channel_location.data.data(),
                    missing_channel_location.data.size()));
            },
            "channel pointer without relocation coverage was accepted")) {
        return false;
    }

    Fixture missing_key_location = make_valid_fixture();
    write_le32(
        missing_key_location.data,
        missing_key_location.relocation_entries[1u],
        kMotionRootRelative + 28u);
    write_le32(
        missing_key_location.data,
        missing_key_location.root_offset + 28u,
        kMotionRootRelative);
    return expects_stage_data_error(
        [&]() {
            static_cast<void>(parse_pc_nn_motion(
                missing_key_location.data.data(),
                missing_key_location.data.size()));
        },
        "key pointer without relocation coverage was accepted");
}

bool test_rejects_invalid_motion_records() {
    Fixture missing_version2 = make_valid_fixture();
    write_le32(missing_version2.data, missing_version2.root_offset, 0x00010001u);
    if (!expects_stage_data_error(
            [&]() {
                static_cast<void>(parse_pc_nn_motion(
                    missing_version2.data.data(),
                    missing_version2.data.size()));
            },
            "motion without VERSION2 was accepted")) {
        return false;
    }

    Fixture short_header = make_valid_fixture();
    write_le32(
        short_header.data,
        short_header.nzmo_offset + 8u,
        static_cast<std::uint32_t>(short_header.nof0_offset - kDataBase - 24u));
    if (!expects_stage_data_error(
            [&]() { static_cast<void>(parse_pc_nn_motion(short_header.data.data(), short_header.data.size())); },
            "24-byte motion header was accepted")) {
        return false;
    }

    Fixture bad_channel_count = make_valid_fixture();
    write_le32(bad_channel_count.data, bad_channel_count.root_offset + 12u, 0xffffffffu);
    if (!expects_stage_data_error(
            [&]() {
                static_cast<void>(parse_pc_nn_motion(
                    bad_channel_count.data.data(),
                    bad_channel_count.data.size()));
            },
            "negative motion channel count was accepted")) {
        return false;
    }

    Fixture empty_with_pointer = make_motion_fixture({});
    write_le32(empty_with_pointer.data, empty_with_pointer.root_offset + 16u, kChannelRelative);
    if (!expects_stage_data_error(
            [&]() {
                static_cast<void>(parse_pc_nn_motion(
                    empty_with_pointer.data.data(),
                    empty_with_pointer.data.size()));
            },
            "zero-count motion with a channel pointer was accepted")) {
        return false;
    }

    Fixture bad_key_count = make_valid_fixture();
    write_le32(bad_key_count.data, bad_key_count.channel_offsets[0u] + 28u, 0xffffffffu);
    if (!expects_stage_data_error(
            [&]() { static_cast<void>(parse_pc_nn_motion(bad_key_count.data.data(), bad_key_count.data.size())); },
            "negative key count was accepted")) {
        return false;
    }

    Fixture zero_stride = make_valid_fixture();
    write_le32(zero_stride.data, zero_stride.channel_offsets[0u] + 32u, 0u);
    if (!expects_stage_data_error(
            [&]() { static_cast<void>(parse_pc_nn_motion(zero_stride.data.data(), zero_stride.data.size())); },
            "nonempty zero-stride key span was accepted")) {
        return false;
    }

    Fixture bad_key_pointer = make_valid_fixture();
    write_le32(bad_key_pointer.data, bad_key_pointer.channel_offsets[0u] + 36u, 0xffffff00u);
    if (!expects_stage_data_error(
            [&]() {
                static_cast<void>(parse_pc_nn_motion(
                    bad_key_pointer.data.data(),
                    bad_key_pointer.data.size()));
            },
            "out-of-range key pointer was accepted")) {
        return false;
    }

    Fixture zero_count_pointer = make_valid_fixture();
    write_le32(zero_count_pointer.data, zero_count_pointer.channel_offsets[0u] + 28u, 0u);
    return expects_stage_data_error(
        [&]() {
            static_cast<void>(parse_pc_nn_motion(
                zero_count_pointer.data.data(),
                zero_count_pointer.data.size()));
        },
        "zero-count channel with a key pointer was accepted");
}

bool test_rejects_malformed_or_unsupported_a16_channels() {
    Fixture fixture = make_valid_fixture();
    const NnMotionData parsed = parse_pc_nn_motion(fixture.data.data(), fixture.data.size());
    return expects_stage_data_error(
               [&]() { static_cast<void>(nn_motion_a16_keys(parsed, parsed.channels.size())); },
               "out-of-range A16 channel index was accepted") &&
           expects_stage_data_error(
               [&]() { static_cast<void>(nn_motion_a16_keys(parsed, 3u)); },
               "unsupported A16 channel flags were accepted") &&
           expects_stage_data_error(
               [&]() {
                   NnMotionData bad_stride = parsed;
                   bad_stride.channels[0u].key_stride = 8u;
                   static_cast<void>(nn_motion_a16_keys(bad_stride, 0u));
               },
               "unsupported A16 key stride was accepted") &&
           expects_stage_data_error(
               [&]() {
                   NnMotionData short_storage = parsed;
                   short_storage.key_data.clear();
                   static_cast<void>(nn_motion_a16_keys(short_storage, 0u));
               },
               "A16 decoder accepted short public key storage") &&
           expects_stage_data_error(
               [&]() {
                   NnMotionData inconsistent_count = parsed;
                   inconsistent_count.channel_count = 0u;
                   static_cast<void>(nn_motion_a16_keys(inconsistent_count, 0u));
               },
               "A16 decoder accepted inconsistent channel storage") &&
           expects_stage_data_error(
               [&]() {
                   NnMotionData duplicate = parsed;
                   write_le16(
                       duplicate.key_data,
                       duplicate.channels[0u].key_data_offset + 4u,
                       static_cast<std::uint16_t>(-10));
                   static_cast<void>(nn_motion_a16_keys(duplicate, 0u));
               },
               "duplicate A16 frames were accepted") &&
           expects_stage_data_error(
               [&]() {
                   NnMotionData descending = parsed;
                   write_le16(
                       descending.key_data,
                       descending.channels[0u].key_data_offset + 4u,
                       static_cast<std::uint16_t>(-11));
                   static_cast<void>(nn_motion_a16_keys(descending, 0u));
               },
               "descending A16 frames were accepted");
}

bool test_rejects_malformed_or_unsupported_float_channels() {
    Fixture fixture = make_valid_fixture();
    const NnMotionData parsed = parse_pc_nn_motion(fixture.data.data(), fixture.data.size());
    const ChannelSpec nonfinite_spec{
        0x101u,
        0x20002u,
        0u,
        0u,
        0u,
        0u,
        0u,
        2u,
        8u,
        kRawKeyRelative,
        packed_float({
            {0x7fc00000u, 0x00000000u},
            {0x3f800000u, 0x3f800000u},
        }),
    };
    Fixture raw_nonfinite = make_motion_fixture({nonfinite_spec});
    const NnMotionData parsed_nonfinite = parse_pc_nn_motion(
        raw_nonfinite.data.data(), raw_nonfinite.data.size());

    return expects_stage_data_error(
               [&]() { static_cast<void>(nn_motion_float_keys(parsed, parsed.channels.size())); },
               "out-of-range float channel index was accepted") &&
           expects_stage_data_error(
               [&]() { static_cast<void>(nn_motion_float_keys(parsed, 0u)); },
               "packed A16 channel decoded as scalar floats") &&
           expects_stage_data_error(
               [&]() {
                   NnMotionData bad_flags = parsed;
                   bad_flags.channels[3u].flags = 0x801u;
                   static_cast<void>(nn_motion_float_keys(bad_flags, 3u));
               },
               "unsupported scalar float flags were accepted") &&
           expects_stage_data_error(
               [&]() {
                   NnMotionData bad_stride = parsed;
                   bad_stride.channels[3u].key_stride = 4u;
                   static_cast<void>(nn_motion_float_keys(bad_stride, 3u));
               },
               "unsupported scalar float stride was accepted") &&
           expects_stage_data_error(
               [&]() {
                   NnMotionData zero_count = parsed;
                   zero_count.channels[3u].key_count = 0u;
                   static_cast<void>(nn_motion_float_keys(zero_count, 3u));
               },
               "zero-count scalar float channel was decoded") &&
           expects_stage_data_error(
               [&]() {
                   NnMotionData too_many = parsed;
                   too_many.channels[3u].key_count = 65537u;
                   static_cast<void>(nn_motion_float_keys(too_many, 3u));
               },
               "scalar float decoder accepted too many keys") &&
           expects_stage_data_error(
               [&]() {
                   NnMotionData short_storage = parsed;
                   short_storage.key_data.clear();
                   static_cast<void>(nn_motion_float_keys(short_storage, 3u));
               },
               "scalar float decoder accepted short public key storage") &&
           expects_stage_data_error(
               [&]() {
                   NnMotionData inconsistent_count = parsed;
                   inconsistent_count.channel_count = 0u;
                   static_cast<void>(nn_motion_float_keys(inconsistent_count, 3u));
               },
               "scalar float decoder accepted inconsistent channel storage") &&
           expects_stage_data_error(
               [&]() {
                   NnMotionData duplicate = parsed;
                   write_le32(
                       duplicate.key_data,
                       duplicate.channels[3u].key_data_offset + 8u,
                       0xbf800000u);
                   static_cast<void>(nn_motion_float_keys(duplicate, 3u));
               },
               "duplicate scalar float frames were accepted") &&
           expects_stage_data_error(
               [&]() {
                   NnMotionData descending = parsed;
                   write_le32(
                       descending.key_data,
                       descending.channels[3u].key_data_offset + 8u,
                       0xc0000000u);
                   static_cast<void>(nn_motion_float_keys(descending, 3u));
               },
               "descending scalar float frames were accepted") &&
           expects_stage_data_error(
               [&]() {
                   NnMotionData nonfinite_value = parsed;
                   write_le32(
                       nonfinite_value.key_data,
                       nonfinite_value.channels[3u].key_data_offset + 4u,
                       0x7f800000u);
                   static_cast<void>(nn_motion_float_keys(nonfinite_value, 3u));
               },
               "scalar float decoder accepted a nonfinite key value") &&
           expects_stage_data_error(
               [&]() { static_cast<void>(nn_motion_float_keys(parsed_nonfinite, 0u)); },
               "scalar float decoder accepted a nonfinite key frame");
}

bool test_decodes_all_supported_scalar_float_axes() {
    Fixture fixture = make_valid_fixture();
    const NnMotionData parsed = parse_pc_nn_motion(fixture.data.data(), fixture.data.size());
    for (std::uint32_t flags : {0x101u, 0x201u, 0x401u, 0x8001u, 0x10001u, 0x20001u}) {
        NnMotionData candidate = parsed;
        candidate.channels[3u].flags = flags;
        const std::vector<NnMotionFloatKey> keys = nn_motion_float_keys(candidate, 3u);
        if (!check(
                keys.size() == 2u &&
                    float_bits(keys[0u].frame) == 0xbf800000u &&
                    float_bits(keys[0u].value) == 0x80000000u &&
                    float_bits(keys[1u].frame) == 0x40000000u &&
                    float_bits(keys[1u].value) == 0x00000001u,
                "supported scalar float axis decoded incorrectly")) {
            return false;
        }
    }
    return true;
}

}

int main() {
    return test_parses_raw_motion_and_coalesces_key_storage() &&
                   test_accepts_explicit_empty_counts() &&
                   test_rejects_bad_envelopes_and_relocations() &&
                   test_rejects_invalid_motion_records() &&
                   test_rejects_malformed_or_unsupported_a16_channels() &&
                   test_rejects_malformed_or_unsupported_float_channels() &&
                   test_decodes_all_supported_scalar_float_axes()
        ? 0
        : 1;
}
