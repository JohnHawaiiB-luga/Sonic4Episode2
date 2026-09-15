#include "nn_motion_data.h"

#include "stage_data.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <utility>
#include <vector>

namespace {

constexpr std::size_t kDataBase = 0x20u;
constexpr std::size_t kChunkHeaderSize = 8u;
constexpr std::size_t kNnIfPayloadSize = 24u;
constexpr std::size_t kMotionPrefixSize = 8u;
constexpr std::size_t kMotionHeaderSize = 32u;
constexpr std::size_t kChannelRecordSize = 40u;
constexpr std::size_t kMaximumKeyCount = 65536u;
constexpr std::uint32_t kNnIfVersion = 1u;
constexpr std::uint32_t kNnMotionRootVersion = 0u;
constexpr std::uint32_t kVersion2Flag = 0x10000000u;

struct Chunk {
    std::size_t offset;
    std::size_t payload_size;
};

class OutputBudget {
public:
    explicit OutputBudget(std::size_t available) : available_(available) {}

    void claim(std::size_t amount, const char* message) {
        if (amount > available_) {
            fail(message);
        }
        available_ -= amount;
    }

private:
    [[noreturn]] static void fail(const char* message) {
        throw StageDataError(message);
    }

    std::size_t available_;
};

[[noreturn]] void fail(const char* message) {
    throw StageDataError(message);
}

std::uint16_t read_le16(const std::uint8_t* data) {
    return static_cast<std::uint16_t>(
        static_cast<std::uint16_t>(data[0]) |
        (static_cast<std::uint16_t>(data[1]) << 8u));
}

std::uint32_t read_le32(const std::uint8_t* data) {
    return static_cast<std::uint32_t>(
        static_cast<std::uint32_t>(data[0]) |
        (static_cast<std::uint32_t>(data[1]) << 8u) |
        (static_cast<std::uint32_t>(data[2]) << 16u) |
        (static_cast<std::uint32_t>(data[3]) << 24u));
}

std::int16_t read_le_i16(const std::uint8_t* data) {
    const std::uint16_t value = read_le16(data);
    if ((value & 0x8000u) == 0u) {
        return static_cast<std::int16_t>(value);
    }
    return static_cast<std::int16_t>(
        static_cast<std::int32_t>(value) - static_cast<std::int32_t>(0x10000u));
}

std::int32_t read_le_i32(const std::uint8_t* data) {
    const std::uint32_t value = read_le32(data);
    if ((value & 0x80000000u) == 0u) {
        return static_cast<std::int32_t>(value);
    }
    return static_cast<std::int32_t>(
        static_cast<std::int64_t>(value) - static_cast<std::int64_t>(0x100000000ull));
}

float read_le_float(const std::uint8_t* data) {
    const std::uint32_t bits = read_le32(data);
    float value = 0.0f;
    std::memcpy(&value, &bits, sizeof(value));
    return value;
}

std::size_t checked_size(std::uint32_t value, const char* message) {
    const std::size_t result = static_cast<std::size_t>(value);
    if (static_cast<std::uint32_t>(result) != value) {
        fail(message);
    }
    return result;
}

std::size_t read_nonnegative_i32(const std::uint8_t* data, const char* message) {
    const std::uint32_t value = read_le32(data);
    if ((value & 0x80000000u) != 0u) {
        fail(message);
    }
    return checked_size(value, message);
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

bool fits_range(std::size_t offset, std::size_t length, std::size_t size) {
    return offset <= size && length <= size - offset;
}

void require_range(std::size_t offset, std::size_t length, std::size_t size, const char* message) {
    if (!fits_range(offset, length, size)) {
        fail(message);
    }
}

bool tag_equals(const std::uint8_t* data, std::size_t offset, const char (&tag)[5]) {
    return data[offset] == static_cast<std::uint8_t>(tag[0]) &&
           data[offset + 1u] == static_cast<std::uint8_t>(tag[1]) &&
           data[offset + 2u] == static_cast<std::uint8_t>(tag[2]) &&
           data[offset + 3u] == static_cast<std::uint8_t>(tag[3]);
}

void require_tag(
    const std::uint8_t* data,
    const Chunk& chunk,
    const char (&tag)[5],
    const char* message) {
    if (!tag_equals(data, chunk.offset, tag)) {
        fail(message);
    }
}

std::vector<Chunk> walk_chunks(const std::uint8_t* data, std::size_t size) {
    std::vector<Chunk> chunks;
    std::size_t offset = 0u;
    while (true) {
        require_range(offset, kChunkHeaderSize, size, "motion chunk header is truncated");
        const std::size_t payload_size = read_nonnegative_i32(
            data + offset + 4u,
            "motion chunk payload size is negative");
        const std::size_t next = checked_sum(
            checked_sum(offset, kChunkHeaderSize, "motion chunk layout is too large"),
            payload_size,
            "motion chunk layout is too large");
        require_range(offset, next - offset, size, "motion chunk payload is truncated");
        if (chunks.size() == 5u) {
            fail("motion chunk sequence has unsupported extra chunks");
        }
        chunks.push_back({offset, payload_size});
        if (tag_equals(data, offset, "NEND")) {
            if (next != size) {
                fail("motion data follows NEND");
            }
            return chunks;
        }
        offset = next;
    }
}

struct DataRegion {
    const std::uint8_t* data;
    std::size_t base;
    std::size_t end;

    std::size_t at(std::uint32_t relative, std::size_t length, const char* message) const {
        const std::size_t offset = checked_size(relative, message);
        const std::size_t absolute = checked_sum(base, offset, message);
        if (absolute < base || !fits_range(absolute, length, end) || absolute + length > end) {
            fail(message);
        }
        return absolute;
    }
};

using RelocationLocations = std::vector<std::uint32_t>;

bool has_relocation(const RelocationLocations& relocations, std::size_t field_relative) {
    if (field_relative > std::numeric_limits<std::uint32_t>::max()) {
        return false;
    }
    return std::binary_search(
        relocations.begin(),
        relocations.end(),
        static_cast<std::uint32_t>(field_relative));
}

void require_chunk_data_range(
    std::size_t absolute,
    std::size_t length,
    const Chunk& chunk,
    const char* message) {
    const std::size_t data_start = checked_sum(
        checked_sum(chunk.offset, kChunkHeaderSize, message),
        kMotionPrefixSize,
        message);
    const std::size_t data_end = checked_sum(
        checked_sum(chunk.offset, kChunkHeaderSize, message),
        chunk.payload_size,
        message);
    if (absolute < data_start || !fits_range(absolute, length, data_end) || absolute + length > data_end) {
        fail(message);
    }
}

std::size_t channel_record_offset(
    std::size_t channel_array,
    std::size_t index,
    const char* message) {
    return checked_sum(
        channel_array,
        checked_product(index, kChannelRecordSize, message),
        message);
}

std::size_t channel_field_relative(
    std::size_t channel_relative,
    std::size_t index,
    std::size_t field_offset,
    const char* message) {
    return checked_sum(
        checked_sum(
            channel_relative,
            checked_product(index, kChannelRecordSize, message),
            message),
        field_offset,
        message);
}

struct KeySpan {
    std::size_t start;
    std::size_t end;
    std::size_t channel_index;
    std::size_t storage_offset;
};

bool key_span_less(const KeySpan& left, const KeySpan& right) {
    if (left.start != right.start) {
        return left.start < right.start;
    }
    return left.end < right.end;
}

bool is_packed_a16_axis(std::uint32_t flags) {
    return flags == 0x812u || flags == 0x1012u || flags == 0x2012u;
}

bool is_scalar_float_axis(std::uint32_t flags) {
    return flags == 0x101u || flags == 0x201u || flags == 0x401u ||
           flags == 0x8001u || flags == 0x10001u || flags == 0x20001u;
}

}

NnMotionData parse_pc_nn_motion(const std::uint8_t* data, std::size_t size) {
    if (data == nullptr) {
        fail("motion data is null");
    }
    if (size < kDataBase) {
        fail("motion data is truncated before its data base");
    }

    const std::vector<Chunk> chunks = walk_chunks(data, size);
    if (chunks.size() != 5u) {
        fail("motion chunk sequence is unsupported");
    }
    const Chunk& nzif_chunk = chunks[0u];
    const Chunk& nzmo_chunk = chunks[1u];
    const Chunk& nof0_chunk = chunks[2u];
    const Chunk& nfn0_chunk = chunks[3u];
    const Chunk& nend_chunk = chunks[4u];
    require_tag(data, nzif_chunk, "NZIF", "motion data does not begin with NZIF");
    require_tag(data, nzmo_chunk, "NZMO", "motion data chunk is missing");
    require_tag(data, nof0_chunk, "NOF0", "motion relocation chunk is missing");
    require_tag(data, nfn0_chunk, "NFN0", "motion source-name chunk is missing");
    require_tag(data, nend_chunk, "NEND", "motion terminator chunk is missing");
    if (nzif_chunk.payload_size != kNnIfPayloadSize) {
        fail("motion NZIF payload size is unsupported");
    }
    if (nend_chunk.payload_size != 8u) {
        fail("motion NEND payload size is unsupported");
    }
    if (nfn0_chunk.payload_size < 9u) {
        fail("motion NFN0 payload is truncated");
    }

    const std::uint32_t declared_chunk_count = read_le32(data + 8u);
    const std::uint32_t declared_data_base = read_le32(data + 12u);
    const std::uint32_t declared_data_size = read_le32(data + 16u);
    const std::uint32_t declared_nof0_offset = read_le32(data + 20u);
    const std::uint32_t declared_nof0_size = read_le32(data + 24u);
    const std::uint32_t declared_version = read_le32(data + 28u);
    if (declared_chunk_count != 1u ||
        static_cast<std::size_t>(declared_data_base) != kDataBase ||
        declared_version != kNnIfVersion ||
        static_cast<std::size_t>(declared_nof0_offset) != nof0_chunk.offset ||
        static_cast<std::size_t>(declared_data_size) != nof0_chunk.offset - kDataBase ||
        static_cast<std::size_t>(declared_nof0_size) != kChunkHeaderSize + nof0_chunk.payload_size) {
        fail("motion NZIF declarations do not match the chunk layout");
    }
    if (nof0_chunk.offset < kDataBase ||
        nzif_chunk.offset + kChunkHeaderSize + nzif_chunk.payload_size != kDataBase) {
        fail("motion data base does not follow NZIF");
    }

    const DataRegion region{data, kDataBase, nof0_chunk.offset};
    if (nof0_chunk.payload_size < 8u) {
        fail("motion NOF0 payload is truncated");
    }
    const std::size_t relocation_count = read_nonnegative_i32(
        data + nof0_chunk.offset + 8u,
        "motion NOF0 relocation count is negative");
    const std::size_t relocation_bytes = checked_product(
        relocation_count,
        4u,
        "motion NOF0 relocation table is too large");
    const std::size_t required_nof0_payload = checked_sum(
        8u,
        relocation_bytes,
        "motion NOF0 relocation table is too large");
    if (required_nof0_payload > nof0_chunk.payload_size) {
        fail("motion NOF0 relocation table exceeds its chunk");
    }
    const std::size_t nof0_padding = nof0_chunk.payload_size - required_nof0_payload;
    if (nof0_padding > 12u || (nof0_padding & 3u) != 0u) {
        fail("motion NOF0 padding is unsupported");
    }
    if (read_le32(data + nof0_chunk.offset + 12u) != 0u) {
        fail("motion NOF0 reserved word is unsupported");
    }
    if (relocation_count > (region.end - region.base) / 4u) {
        fail("motion NOF0 relocation count exceeds the data region");
    }

    RelocationLocations relocations;
    relocations.reserve(relocation_count);
    for (std::size_t index = 0u; index < relocation_count; ++index) {
        const std::uint32_t location = read_le32(
            data + nof0_chunk.offset + 16u + index * 4u);
        if ((location & 3u) != 0u) {
            fail("motion NOF0 relocation location is unaligned");
        }
        region.at(location, 4u, "motion NOF0 relocation location is outside the data region");
        relocations.push_back(location);
    }
    std::sort(relocations.begin(), relocations.end());
    if (std::unique(relocations.begin(), relocations.end()) != relocations.end()) {
        fail("motion NOF0 relocation table has repeated locations");
    }
    for (std::uint32_t location : relocations) {
        const std::size_t target_word = region.at(
            location,
            4u,
            "motion NOF0 relocation location is outside the data region");
        const std::uint32_t target_relative = read_le32(data + target_word);
        if (target_relative == 0u) {
            fail("motion NOF0 relocation target is null");
        }
        region.at(target_relative, 1u, "motion NOF0 relocation target is outside the data region");
    }

    if (nzmo_chunk.payload_size < kMotionPrefixSize) {
        fail("motion NZMO prefix is truncated");
    }
    const std::uint32_t motion_relative = read_le32(data + nzmo_chunk.offset + 8u);
    if (read_le32(data + nzmo_chunk.offset + 12u) != kNnMotionRootVersion) {
        fail("motion NZMO root version is unsupported");
    }
    const std::size_t motion_relative_size = checked_size(
        motion_relative,
        "motion header relative offset is unsupported");
    const std::size_t motion = region.at(
        motion_relative,
        kMotionHeaderSize,
        "motion header is out of range");
    require_chunk_data_range(
        motion,
        kMotionHeaderSize,
        nzmo_chunk,
        "motion header is outside NZMO data");

    const std::uint32_t flags = read_le32(data + motion);
    if ((flags & kVersion2Flag) == 0u) {
        fail("motion header does not use VERSION2");
    }
    const std::uint32_t start_bits = read_le32(data + motion + 4u);
    const std::uint32_t end_bits = read_le32(data + motion + 8u);
    const std::size_t channel_count = read_nonnegative_i32(
        data + motion + 12u,
        "motion channel count is negative");
    const std::uint32_t channel_relative = read_le32(data + motion + 16u);
    const std::uint32_t frame_rate_bits = read_le32(data + motion + 20u);
    const std::uint32_t reserved0_bits = read_le32(data + motion + 24u);
    const std::uint32_t reserved1_bits = read_le32(data + motion + 28u);
    const std::size_t channel_relative_size = checked_size(
        channel_relative,
        "motion channel relative offset is unsupported");
    const std::size_t channel_field = checked_sum(
        motion_relative_size,
        16u,
        "motion channel pointer field is too large");

    std::size_t channel_array = 0u;
    if (channel_count == 0u) {
        if (channel_relative != 0u) {
            fail("empty motion has a channel pointer");
        }
    } else {
        const std::size_t channel_length = checked_product(
            channel_count,
            kChannelRecordSize,
            "motion channel array is too large");
        if (channel_relative == 0u || !has_relocation(relocations, channel_field)) {
            fail("motion channel pointer is invalid");
        }
        channel_array = region.at(
            channel_relative,
            channel_length,
            "motion channel array is out of range");
        require_chunk_data_range(
            channel_array,
            channel_length,
            nzmo_chunk,
            "motion channel array is outside NZMO data");
    }

    for (std::size_t index = 0u; index < channel_count; ++index) {
        const std::size_t channel = channel_record_offset(
            channel_array,
            index,
            "motion channel record is too large");
        const std::size_t key_count = read_nonnegative_i32(
            data + channel + 28u,
            "motion key count is negative");
        const std::size_t key_stride = read_nonnegative_i32(
            data + channel + 32u,
            "motion key stride is negative");
        const std::uint32_t key_relative = read_le32(data + channel + 36u);
        if (key_count == 0u) {
            if (key_relative != 0u) {
                fail("empty motion key span has a pointer");
            }
            continue;
        }
        if (key_stride == 0u) {
            fail("motion key stride is zero");
        }
        const std::size_t key_field = channel_field_relative(
            channel_relative_size,
            index,
            36u,
            "motion key pointer field is too large");
        if (key_relative == 0u || !has_relocation(relocations, key_field)) {
            fail("motion key pointer is invalid");
        }
        const std::size_t key_length = checked_product(
            key_count,
            key_stride,
            "motion key span is too large");
        const std::size_t key = region.at(
            key_relative,
            key_length,
            "motion key span is out of range");
        require_chunk_data_range(
            key,
            key_length,
            nzmo_chunk,
            "motion key span is outside NZMO data");
    }

    const std::size_t semantic_limit = checked_product(
        region.end - region.base,
        8u,
        "motion output budget is too large");
    OutputBudget output_budget(semantic_limit);
    output_budget.claim(
        checked_product(
            channel_count,
            sizeof(NnMotionChannel),
            "motion channel output is too large"),
        "motion channel output is too large");
    output_budget.claim(
        checked_product(
            channel_count,
            sizeof(KeySpan),
            "motion key span staging is too large"),
        "motion key span staging is too large");

    std::vector<KeySpan> spans;
    spans.reserve(channel_count);
    for (std::size_t index = 0u; index < channel_count; ++index) {
        const std::size_t channel = channel_record_offset(
            channel_array,
            index,
            "motion channel record is too large");
        const std::size_t key_count = read_nonnegative_i32(
            data + channel + 28u,
            "motion key count is negative");
        if (key_count == 0u) {
            continue;
        }
        const std::size_t key_stride = read_nonnegative_i32(
            data + channel + 32u,
            "motion key stride is negative");
        const std::size_t key_length = checked_product(
            key_count,
            key_stride,
            "motion key span is too large");
        const std::size_t key = region.at(
            read_le32(data + channel + 36u),
            key_length,
            "motion key span is out of range");
        spans.push_back({
            key,
            checked_sum(key, key_length, "motion key span is too large"),
            index,
            0u,
        });
    }

    std::sort(spans.begin(), spans.end(), key_span_less);
    std::size_t key_data_size = 0u;
    if (!spans.empty()) {
        std::size_t interval_start = spans[0u].start;
        std::size_t interval_end = spans[0u].end;
        spans[0u].storage_offset = 0u;
        for (std::size_t index = 1u; index < spans.size(); ++index) {
            KeySpan& span = spans[index];
            if (span.start <= interval_end) {
                span.storage_offset = checked_sum(
                    key_data_size,
                    span.start - interval_start,
                    "motion key storage is too large");
                if (span.end > interval_end) {
                    interval_end = span.end;
                }
                continue;
            }
            key_data_size = checked_sum(
                key_data_size,
                interval_end - interval_start,
                "motion key storage is too large");
            interval_start = span.start;
            interval_end = span.end;
            span.storage_offset = key_data_size;
        }
        key_data_size = checked_sum(
            key_data_size,
            interval_end - interval_start,
            "motion key storage is too large");
    }
    output_budget.claim(key_data_size, "motion key storage is too large");

    NnMotionData result{
        flags,
        start_bits,
        end_bits,
        frame_rate_bits,
        {reserved0_bits, reserved1_bits},
        static_cast<std::uint32_t>(channel_count),
        {},
        {},
    };
    result.channels.reserve(channel_count);
    for (std::size_t index = 0u; index < channel_count; ++index) {
        const std::size_t channel = channel_record_offset(
            channel_array,
            index,
            "motion channel record is too large");
        result.channels.push_back({
            read_le32(data + channel),
            read_le32(data + channel + 4u),
            read_le_i32(data + channel + 8u),
            read_le32(data + channel + 12u),
            read_le32(data + channel + 16u),
            read_le32(data + channel + 20u),
            read_le32(data + channel + 24u),
            read_le32(data + channel + 28u),
            read_le32(data + channel + 32u),
            0u,
        });
    }
    for (const KeySpan& span : spans) {
        result.channels[span.channel_index].key_data_offset = span.storage_offset;
    }

    result.key_data.reserve(key_data_size);
    if (!spans.empty()) {
        std::size_t interval_start = spans[0u].start;
        std::size_t interval_end = spans[0u].end;
        for (std::size_t index = 1u; index < spans.size(); ++index) {
            const KeySpan& span = spans[index];
            if (span.start <= interval_end) {
                if (span.end > interval_end) {
                    interval_end = span.end;
                }
                continue;
            }
            result.key_data.insert(
                result.key_data.end(),
                region.data + interval_start,
                region.data + interval_end);
            interval_start = span.start;
            interval_end = span.end;
        }
        result.key_data.insert(
            result.key_data.end(),
            region.data + interval_start,
            region.data + interval_end);
    }
    return result;
}

std::vector<NnMotionA16Key> nn_motion_a16_keys(
    const NnMotionData& motion,
    std::size_t channel_index) {
    if (motion.channels.size() > std::numeric_limits<std::uint32_t>::max() ||
        motion.channel_count != static_cast<std::uint32_t>(motion.channels.size())) {
        fail("motion channel storage is inconsistent");
    }
    if (channel_index >= motion.channels.size()) {
        fail("motion A16 channel index is out of range");
    }
    const NnMotionChannel& channel = motion.channels[channel_index];
    if (!is_packed_a16_axis(channel.flags)) {
        fail("motion channel flags are not a packed A16 axis");
    }
    if (channel.key_count == 0u) {
        fail("motion A16 channel has no keys");
    }
    if (channel.key_stride != 4u) {
        fail("motion A16 key stride is unsupported");
    }
    const std::size_t key_count = checked_size(
        channel.key_count,
        "motion A16 key count is unsupported");
    const std::size_t key_bytes = checked_product(
        key_count,
        4u,
        "motion A16 key span is too large");
    if (!fits_range(channel.key_data_offset, key_bytes, motion.key_data.size())) {
        fail("motion A16 key storage is malformed");
    }

    std::vector<NnMotionA16Key> keys;
    keys.reserve(key_count);
    for (std::size_t index = 0u; index < key_count; ++index) {
        const std::size_t offset = checked_sum(
            channel.key_data_offset,
            checked_product(index, 4u, "motion A16 key span is too large"),
            "motion A16 key span is too large");
        const NnMotionA16Key key{
            read_le_i16(motion.key_data.data() + offset),
            read_le_i16(motion.key_data.data() + offset + 2u),
        };
        if (!keys.empty() && keys.back().frame >= key.frame) {
            fail("motion A16 keys are not strictly ascending");
        }
        keys.push_back(key);
    }
    return keys;
}

std::vector<NnMotionFloatKey> nn_motion_float_keys(
    const NnMotionData& motion,
    std::size_t channel_index) {
    if (motion.channels.size() > std::numeric_limits<std::uint32_t>::max() ||
        motion.channel_count != static_cast<std::uint32_t>(motion.channels.size())) {
        fail("motion channel storage is inconsistent");
    }
    if (channel_index >= motion.channels.size()) {
        fail("motion scalar float channel index is out of range");
    }
    const NnMotionChannel& channel = motion.channels[channel_index];
    if (!is_scalar_float_axis(channel.flags)) {
        fail("motion channel flags are not a scalar float axis");
    }
    if (channel.key_count == 0u || channel.key_count > kMaximumKeyCount) {
        fail("motion scalar float key count is unsupported");
    }
    if (channel.key_stride != 8u) {
        fail("motion scalar float key stride is unsupported");
    }
    const std::size_t key_count = checked_size(
        channel.key_count,
        "motion scalar float key count is unsupported");
    const std::size_t key_bytes = checked_product(
        key_count,
        8u,
        "motion scalar float key span is too large");
    if (!fits_range(channel.key_data_offset, key_bytes, motion.key_data.size())) {
        fail("motion scalar float key storage is malformed");
    }

    std::vector<NnMotionFloatKey> keys;
    keys.reserve(key_count);
    for (std::size_t index = 0u; index < key_count; ++index) {
        const std::size_t offset = checked_sum(
            channel.key_data_offset,
            checked_product(index, 8u, "motion scalar float key span is too large"),
            "motion scalar float key span is too large");
        const NnMotionFloatKey key{
            read_le_float(motion.key_data.data() + offset),
            read_le_float(motion.key_data.data() + offset + 4u),
        };
        if (!std::isfinite(key.frame) || !std::isfinite(key.value)) {
            fail("motion scalar float keys must be finite");
        }
        if (!keys.empty() && !(keys.back().frame < key.frame)) {
            fail("motion scalar float keys are not strictly ascending");
        }
        keys.push_back(key);
    }
    return keys;
}
