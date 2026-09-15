#include "stage_data.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <string>
#include <utility>
#include <vector>

namespace {

constexpr std::size_t kArchiveHeaderSize = 32u;
constexpr std::size_t kArchiveEntrySize = 16u;
constexpr std::size_t kArchiveNameSize = 32u;

bool check(bool condition, const char* message) {
    if (condition) {
        return true;
    }
    std::fprintf(stderr, "%s\n", message);
    return false;
}

void write_le16(std::vector<std::uint8_t>& data, std::size_t offset, std::uint16_t value) {
    data[offset] = static_cast<std::uint8_t>(value & 0xFFu);
    data[offset + 1u] = static_cast<std::uint8_t>((value >> 8u) & 0xFFu);
}

void write_le32(std::vector<std::uint8_t>& data, std::size_t offset, std::uint32_t value) {
    data[offset] = static_cast<std::uint8_t>(value & 0xFFu);
    data[offset + 1u] = static_cast<std::uint8_t>((value >> 8u) & 0xFFu);
    data[offset + 2u] = static_cast<std::uint8_t>((value >> 16u) & 0xFFu);
    data[offset + 3u] = static_cast<std::uint8_t>((value >> 24u) & 0xFFu);
}

std::vector<std::uint8_t> make_archive(
    std::size_t size,
    std::uint32_t count,
    std::uint32_t entry_table_offset,
    std::uint32_t name_table_offset) {
    std::vector<std::uint8_t> data(size, 0u);
    data[0] = static_cast<std::uint8_t>('#');
    data[1] = static_cast<std::uint8_t>('A');
    data[2] = static_cast<std::uint8_t>('M');
    data[3] = static_cast<std::uint8_t>('B');
    write_le32(data, 4u, static_cast<std::uint32_t>(kArchiveHeaderSize));
    write_le32(data, 16u, count);
    write_le32(data, 20u, entry_table_offset);
    write_le32(data, 28u, name_table_offset);
    return data;
}

void write_entry(
    std::vector<std::uint8_t>& data,
    std::size_t entry_table_offset,
    std::size_t index,
    std::uint32_t offset,
    std::uint32_t length) {
    const std::size_t entry_offset = entry_table_offset + index * kArchiveEntrySize;
    write_le32(data, entry_offset, offset);
    write_le32(data, entry_offset + 4u, length);
    data[entry_offset + 8u] = 0xA5u;
    data[entry_offset + 15u] = 0x5Au;
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

bool test_archive_decodes_entries_and_names() {
    constexpr std::size_t entry_table_offset = kArchiveHeaderSize;
    constexpr std::size_t name_table_offset = entry_table_offset + 3u * kArchiveEntrySize;
    constexpr std::size_t payload_offset = name_table_offset + 3u * kArchiveNameSize;
    std::vector<std::uint8_t> archive = make_archive(
        payload_offset + 16u,
        3u,
        static_cast<std::uint32_t>(entry_table_offset),
        static_cast<std::uint32_t>(name_table_offset));

    write_entry(archive, entry_table_offset, 0u, static_cast<std::uint32_t>(payload_offset), 5u);
    write_entry(archive, entry_table_offset, 1u, 0u, 0u);
    write_entry(archive, entry_table_offset, 2u, static_cast<std::uint32_t>(payload_offset + 5u), 2u);
    write_le32(archive, entry_table_offset + 12u, 0x81234504u);
    write_le32(archive, entry_table_offset + kArchiveEntrySize + 12u, 0xFFFFFFFFu);

    archive[name_table_offset] = static_cast<std::uint8_t>('a');
    archive[name_table_offset + 1u] = static_cast<std::uint8_t>('c');
    archive[name_table_offset + 2u] = static_cast<std::uint8_t>('t');
    archive[name_table_offset + 3u] = static_cast<std::uint8_t>('0');
    archive[name_table_offset + 4u] = 0u;
    archive[name_table_offset + 5u] = 0xE1u;

    std::array<std::uint8_t, kArchiveNameSize> full_name{};
    for (std::size_t index = 0u; index < full_name.size(); ++index) {
        full_name[index] = static_cast<std::uint8_t>(0x80u + index);
        archive[name_table_offset + 2u * kArchiveNameSize + index] = full_name[index];
    }

    const std::vector<std::uint8_t> before = archive;
    const std::vector<StageArchiveEntry> entries = parse_pc_stage_archive(archive.data(), archive.size());
    const std::string expected_full_name(
        reinterpret_cast<const char*>(full_name.data()),
        full_name.size());

    return check(entries.size() == 3u, "archive entry count mismatch") &&
           check(
               entries[0].offset == payload_offset && entries[0].length == 5u &&
                   entries[0].name == "act0" && entries[0].flags == 0x81234504u,
               "first archive entry mismatch") &&
           check(
               entries[1].offset == 0u && entries[1].length == 0u && entries[1].name == "1" &&
                   entries[1].flags == 0xFFFFFFFFu,
               "null or blank archive entry mismatch") &&
           check(
               entries[2].offset == payload_offset + 5u && entries[2].length == 2u &&
                   entries[2].name == expected_full_name && entries[2].flags == 0x5A000000u,
               "full-width archive name mismatch") &&
           check(archive == before, "archive parser mutated input data");
}

bool test_archive_uses_indexes_without_name_table() {
    constexpr std::size_t entry_table_offset = kArchiveHeaderSize;
    std::vector<std::uint8_t> archive = make_archive(
        96u,
        2u,
        static_cast<std::uint32_t>(entry_table_offset),
        0u);

    write_entry(archive, entry_table_offset, 0u, 64u, 3u);
    write_entry(archive, entry_table_offset, 1u, 67u, 4u);

    const std::vector<StageArchiveEntry> entries = parse_pc_stage_archive(archive.data(), archive.size());
    archive[entry_table_offset + 15u] = 0u;
    return check(StageArchiveEntry{"legacy", 0u, 0u}.flags == 0u, "legacy entry flags are not zero") &&
           check(entries[0].flags == 0x5A000000u, "archive flags retained input storage") &&
           check(entries.size() == 2u, "unnamed archive entry count mismatch") &&
           check(
               entries[0].name == "0" && entries[0].offset == 64u && entries[0].length == 3u,
               "first unnamed entry mismatch") &&
           check(
               entries[1].name == "1" && entries[1].offset == 67u && entries[1].length == 4u,
               "second unnamed entry mismatch");
}

bool test_archive_rejects_malformed_data() {
    std::vector<std::uint8_t> truncated(kArchiveHeaderSize - 1u, 0u);
    if (!expects_stage_data_error(
            [&]() { parse_pc_stage_archive(nullptr, kArchiveHeaderSize); },
            "null archive data was accepted") ||
        !expects_stage_data_error(
            [&]() { parse_pc_stage_archive(truncated.data(), truncated.size()); },
            "truncated archive header was accepted")) {
        return false;
    }

    std::vector<std::uint8_t> bad_magic = make_archive(64u, 0u, 0u, 0u);
    bad_magic[0] = static_cast<std::uint8_t>('!');
    if (!expects_stage_data_error(
            [&]() { parse_pc_stage_archive(bad_magic.data(), bad_magic.size()); },
            "invalid archive magic was accepted")) {
        return false;
    }

    std::vector<std::uint8_t> modern_header = make_archive(64u, 0u, 0u, 0u);
    write_le32(modern_header, 4u, 48u);
    if (!expects_stage_data_error(
            [&]() { parse_pc_stage_archive(modern_header.data(), modern_header.size()); },
            "unsupported 48-byte archive header was accepted")) {
        return false;
    }

    std::vector<std::uint8_t> negative_count = make_archive(64u, 0x80000000u, 32u, 0u);
    if (!expects_stage_data_error(
            [&]() { parse_pc_stage_archive(negative_count.data(), negative_count.size()); },
            "negative archive count was accepted")) {
        return false;
    }

    std::vector<std::uint8_t> huge_count = make_archive(64u, 0x7FFFFFFFu, 32u, 0u);
    if (!expects_stage_data_error(
            [&]() { parse_pc_stage_archive(huge_count.data(), huge_count.size()); },
            "oversized archive table was accepted")) {
        return false;
    }

    std::vector<std::uint8_t> negative_entry_table = make_archive(64u, 1u, 0xFFFFFFFFu, 0u);
    if (!expects_stage_data_error(
            [&]() { parse_pc_stage_archive(negative_entry_table.data(), negative_entry_table.size()); },
            "negative archive table offset was accepted")) {
        return false;
    }

    std::vector<std::uint8_t> out_of_range_entry_table = make_archive(64u, 1u, 64u, 0u);
    if (!expects_stage_data_error(
            [&]() { parse_pc_stage_archive(out_of_range_entry_table.data(), out_of_range_entry_table.size()); },
            "out-of-range archive table was accepted")) {
        return false;
    }

    std::vector<std::uint8_t> header_entry_table = make_archive(64u, 1u, 20u, 0u);
    if (!expects_stage_data_error(
            [&]() { parse_pc_stage_archive(header_entry_table.data(), header_entry_table.size()); },
            "archive entry table overlapping header was accepted")) {
        return false;
    }

    std::vector<std::uint8_t> negative_payload_offset = make_archive(64u, 1u, 32u, 0u);
    write_entry(negative_payload_offset, 32u, 0u, 0xFFFFFFFFu, 0u);
    if (!expects_stage_data_error(
            [&]() { parse_pc_stage_archive(negative_payload_offset.data(), negative_payload_offset.size()); },
            "negative payload offset was accepted")) {
        return false;
    }

    std::vector<std::uint8_t> negative_payload_length = make_archive(64u, 1u, 32u, 0u);
    write_entry(negative_payload_length, 32u, 0u, 48u, 0xFFFFFFFFu);
    if (!expects_stage_data_error(
            [&]() { parse_pc_stage_archive(negative_payload_length.data(), negative_payload_length.size()); },
            "negative payload length was accepted")) {
        return false;
    }

    std::vector<std::uint8_t> out_of_range_payload = make_archive(64u, 1u, 32u, 0u);
    write_entry(out_of_range_payload, 32u, 0u, 60u, 5u);
    if (!expects_stage_data_error(
            [&]() { parse_pc_stage_archive(out_of_range_payload.data(), out_of_range_payload.size()); },
            "out-of-range payload was accepted")) {
        return false;
    }

    std::vector<std::uint8_t> header_payload = make_archive(64u, 1u, 32u, 0u);
    write_entry(header_payload, 32u, 0u, 1u, 1u);
    if (!expects_stage_data_error(
            [&]() { parse_pc_stage_archive(header_payload.data(), header_payload.size()); },
            "payload overlapping header was accepted")) {
        return false;
    }

    std::vector<std::uint8_t> empty_payload_out_of_range = make_archive(64u, 1u, 32u, 0u);
    write_entry(empty_payload_out_of_range, 32u, 0u, 65u, 0u);
    if (!expects_stage_data_error(
            [&]() { parse_pc_stage_archive(empty_payload_out_of_range.data(), empty_payload_out_of_range.size()); },
            "out-of-range empty payload was accepted")) {
        return false;
    }

    std::vector<std::uint8_t> out_of_range_name_table = make_archive(80u, 1u, 32u, 64u);
    write_entry(out_of_range_name_table, 32u, 0u, 0u, 0u);
    if (!expects_stage_data_error(
            [&]() { parse_pc_stage_archive(out_of_range_name_table.data(), out_of_range_name_table.size()); },
            "out-of-range name table was accepted")) {
        return false;
    }

    std::vector<std::uint8_t> header_name_table = make_archive(96u, 1u, 32u, 24u);
    write_entry(header_name_table, 32u, 0u, 0u, 0u);
    return expects_stage_data_error(
        [&]() { parse_pc_stage_archive(header_name_table.data(), header_name_table.size()); },
        "name table overlapping header was accepted");
}

bool test_grid_decodes_byte_and_word_cells() {
    std::vector<std::uint8_t> byte_grid = {
        3u, 0u, 2u, 0u,
        0x00u, 0x7Fu, 0x80u,
        0xFFu, 0x01u, 0x42u,
    };
    const std::vector<std::uint8_t> byte_grid_before = byte_grid;
    const StageGridData decoded_bytes = parse_stage_grid(byte_grid.data(), byte_grid.size(), 1u);
    if (!check(
            decoded_bytes.width == 3u && decoded_bytes.height == 2u && decoded_bytes.cell_bytes == 1u,
            "byte grid dimensions mismatch") ||
        !check(
            decoded_bytes.cells == std::vector<std::uint16_t>({0x0000u, 0x007Fu, 0x0080u, 0x00FFu, 0x0001u, 0x0042u}),
            "byte grid row order or high bits mismatch") ||
        !check(byte_grid == byte_grid_before, "byte grid parser mutated input data")) {
        return false;
    }

    const std::vector<std::uint8_t> word_grid = {
        2u, 0u, 2u, 0u,
        0x34u, 0x12u,
        0x00u, 0x80u,
        0xFFu, 0xFFu,
        0x01u, 0x00u,
    };
    const StageGridData decoded_words = parse_stage_grid(word_grid.data(), word_grid.size(), 2u);
    return check(
               decoded_words.width == 2u && decoded_words.height == 2u && decoded_words.cell_bytes == 2u,
               "word grid dimensions mismatch") &&
           check(
               decoded_words.cells == std::vector<std::uint16_t>({0x1234u, 0x8000u, 0xFFFFu, 0x0001u}),
               "word grid little-endian row order mismatch");
}

bool test_grid_rejects_malformed_data() {
    const std::vector<std::uint8_t> short_header = {1u, 0u, 1u};
    if (!expects_stage_data_error(
            [&]() { parse_stage_grid(nullptr, 0u, 1u); },
            "null grid data was accepted") ||
        !expects_stage_data_error(
            [&]() { parse_stage_grid(short_header.data(), short_header.size(), 1u); },
            "truncated grid header was accepted") ||
        !expects_stage_data_error(
            [&]() { parse_stage_grid(short_header.data(), short_header.size(), 0u); },
            "zero-byte grid cells were accepted") ||
        !expects_stage_data_error(
            [&]() { parse_stage_grid(short_header.data(), short_header.size(), 3u); },
            "three-byte grid cells were accepted")) {
        return false;
    }

    const std::vector<std::uint8_t> zero_width = {0u, 0u, 1u, 0u, 0u};
    const std::vector<std::uint8_t> zero_height = {1u, 0u, 0u, 0u, 0u};
    if (!expects_stage_data_error(
            [&]() { parse_stage_grid(zero_width.data(), zero_width.size(), 1u); },
            "zero grid width was accepted") ||
        !expects_stage_data_error(
            [&]() { parse_stage_grid(zero_height.data(), zero_height.size(), 1u); },
            "zero grid height was accepted")) {
        return false;
    }

    const std::vector<std::uint8_t> short_cells = {1u, 0u, 1u, 0u};
    const std::vector<std::uint8_t> trailing_cells = {1u, 0u, 1u, 0u, 0x12u, 0x34u};
    if (!expects_stage_data_error(
            [&]() { parse_stage_grid(short_cells.data(), short_cells.size(), 1u); },
            "truncated grid cells were accepted") ||
        !expects_stage_data_error(
            [&]() { parse_stage_grid(trailing_cells.data(), trailing_cells.size(), 1u); },
            "grid with trailing cells was accepted")) {
        return false;
    }

    const std::vector<std::uint8_t> huge_dimensions = {0xFFu, 0xFFu, 0xFFu, 0xFFu};
    return expects_stage_data_error(
        [&]() { parse_stage_grid(huge_dimensions.data(), huge_dimensions.size(), 2u); },
        "huge grid dimensions were accepted");
}

bool test_ring_placements_preserve_block_and_record_order() {
    std::vector<std::uint8_t> data(32u, 0u);
    write_le16(data, 0u, 2u);
    write_le16(data, 2u, 2u);
    write_le32(data, 4u, 20u);
    write_le32(data, 8u, 26u);
    write_le32(data, 12u, 26u);
    write_le32(data, 16u, 28u);
    write_le16(data, 20u, 2u);
    data[22u] = 1u;
    data[23u] = 2u;
    data[24u] = 250u;
    data[25u] = 251u;
    write_le16(data, 26u, 0u);
    write_le16(data, 28u, 1u);
    data[30u] = 3u;
    data[31u] = 4u;

    const std::vector<std::uint8_t> before = data;
    const StageRingPlacements placements = parse_stage_ring_placements(data.data(), data.size());
    return check(placements.block_width == 2u && placements.block_height == 2u,
                 "ring block dimensions mismatch") &&
           check(placements.rings.size() == 3u, "ring count or shared empty offset mismatch") &&
           check(placements.rings[0].x == 1 && placements.rings[0].y == 2,
                 "first ring position mismatch") &&
           check(placements.rings[1].x == 250 && placements.rings[1].y == 251,
                 "ring record order mismatch") &&
           check(placements.rings[2].x == 259 && placements.rings[2].y == 260,
                 "ring block origin mismatch") &&
           check(data == before, "ring parser mutated input data");
}

bool test_ring_placements_accept_empty_dimensions() {
    const std::vector<std::uint8_t> data = {0u, 0u, 5u, 0u};
    const StageRingPlacements placements = parse_stage_ring_placements(data.data(), data.size());
    return check(placements.block_width == 0u && placements.block_height == 5u,
                 "empty ring grid dimensions mismatch") &&
           check(placements.rings.empty(), "empty ring grid produced records");
}

bool test_ring_placements_reject_malformed_spans() {
    const std::vector<std::uint8_t> short_header = {1u, 0u, 1u};
    if (!expects_stage_data_error(
            [&]() { parse_stage_ring_placements(nullptr, 0u); },
            "null ring data was accepted") ||
        !expects_stage_data_error(
            [&]() { parse_stage_ring_placements(short_header.data(), short_header.size()); },
            "truncated ring header was accepted")) {
        return false;
    }

    const std::vector<std::uint8_t> truncated_table = {2u, 0u, 1u, 0u, 8u, 0u, 0u, 0u};
    if (!expects_stage_data_error(
            [&]() { parse_stage_ring_placements(truncated_table.data(), truncated_table.size()); },
            "truncated ring offset table was accepted")) {
        return false;
    }

    const std::vector<std::uint8_t> oversized_table = {0xffu, 0xffu, 0xffu, 0xffu};
    if (!expects_stage_data_error(
            [&]() { parse_stage_ring_placements(oversized_table.data(), oversized_table.size()); },
            "oversized ring offset table was accepted")) {
        return false;
    }

    std::vector<std::uint8_t> negative_offset(8u, 0u);
    write_le16(negative_offset, 0u, 1u);
    write_le16(negative_offset, 2u, 1u);
    write_le32(negative_offset, 4u, 0xffffffffu);
    if (!expects_stage_data_error(
            [&]() { parse_stage_ring_placements(negative_offset.data(), negative_offset.size()); },
            "negative ring block offset was accepted")) {
        return false;
    }

    std::vector<std::uint8_t> out_of_range_offset(9u, 0u);
    write_le16(out_of_range_offset, 0u, 1u);
    write_le16(out_of_range_offset, 2u, 1u);
    write_le32(out_of_range_offset, 4u, 8u);
    if (!expects_stage_data_error(
            [&]() { parse_stage_ring_placements(out_of_range_offset.data(), out_of_range_offset.size()); },
            "out-of-range ring block offset was accepted")) {
        return false;
    }

    std::vector<std::uint8_t> truncated_records(12u, 0u);
    write_le16(truncated_records, 0u, 1u);
    write_le16(truncated_records, 2u, 1u);
    write_le32(truncated_records, 4u, 8u);
    write_le16(truncated_records, 8u, 2u);
    return expects_stage_data_error(
        [&]() { parse_stage_ring_placements(truncated_records.data(), truncated_records.size()); },
        "truncated ring records were accepted");
}

}

int main() {
    return test_archive_decodes_entries_and_names() &&
                   test_archive_uses_indexes_without_name_table() &&
                   test_archive_rejects_malformed_data() &&
                   test_grid_decodes_byte_and_word_cells() &&
                   test_grid_rejects_malformed_data() &&
                   test_ring_placements_preserve_block_and_record_order() &&
                   test_ring_placements_accept_empty_dimensions() &&
                   test_ring_placements_reject_malformed_spans()
               ? 0
               : 1;
}
