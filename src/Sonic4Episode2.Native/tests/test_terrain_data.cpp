#include "terrain_data.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <limits>
#include <utility>
#include <vector>

namespace {

constexpr std::size_t kTerrainHeaderSize = 4u;

bool check(bool condition, const char* message) {
    if (condition) {
        return true;
    }
    std::fprintf(stderr, "%s\n", message);
    return false;
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

void write_le16(std::vector<std::uint8_t>& data, std::size_t offset, std::uint16_t value) {
    data[offset] = static_cast<std::uint8_t>(value & 0x00FFu);
    data[offset + 1u] = static_cast<std::uint8_t>((value >> 8u) & 0x00FFu);
}

std::vector<std::uint8_t> make_table(
    TerrainRecordKind kind,
    const std::vector<std::uint8_t>& records,
    const std::vector<std::uint16_t>& chip_records) {
    const std::size_t record_size = terrain_record_size(kind);
    if (records.size() % record_size != 0u ||
        records.size() / record_size > std::numeric_limits<std::uint16_t>::max() ||
        chip_records.size() > std::numeric_limits<std::uint16_t>::max()) {
        return {};
    }

    const std::size_t record_count = records.size() / record_size;
    std::vector<std::uint8_t> data(
        kTerrainHeaderSize + records.size() + chip_records.size() * 2u,
        0u);
    write_le16(data, 0u, static_cast<std::uint16_t>(chip_records.size()));
    write_le16(data, 2u, static_cast<std::uint16_t>(record_count));

    for (std::size_t index = 0u; index < records.size(); ++index) {
        data[kTerrainHeaderSize + index] = records[index];
    }

    const std::size_t index_table_offset = kTerrainHeaderSize + records.size();
    for (std::size_t index = 0u; index < chip_records.size(); ++index) {
        write_le16(data, index_table_offset + index * 2u, chip_records[index]);
    }

    return data;
}

bool test_record_sizes_and_height_table() {
    if (!check(
            terrain_record_size(TerrainRecordKind::Height) == 4096u,
            "height record size mismatch") ||
        !check(
            terrain_record_size(TerrainRecordKind::Angle) == 64u,
            "angle record size mismatch") ||
        !check(
            terrain_record_size(TerrainRecordKind::Attribute) == 64u,
            "attribute record size mismatch")) {
        return false;
    }

    const std::size_t record_size = terrain_record_size(TerrainRecordKind::Height);
    std::vector<std::uint8_t> records(record_size * 2u, 0u);
    records[record_size - 1u] = 0x80u;
    records[record_size] = 0xFFu;
    records[record_size + 63u] = 0xA5u;

    std::vector<std::uint8_t> source =
        make_table(TerrainRecordKind::Height, records, {1u, 0u, 1u});
    const std::vector<std::uint8_t> source_before = source;
    TerrainTable table =
        parse_terrain_table(source.data(), source.size(), TerrainRecordKind::Height);

    const std::optional<std::uint8_t> first = terrain_byte(table, 0u, 0u);
    const std::optional<std::uint8_t> second = terrain_byte(table, 1u, record_size - 1u);
    const std::optional<std::uint8_t> repeated = terrain_byte(table, 2u, 63u);
    if (!check(table.kind == TerrainRecordKind::Height, "height table kind mismatch") ||
        !check(table.record_count == 2u, "height table record count mismatch") ||
        !check(table.records == records, "height table raw record bytes mismatch") ||
        !check(
            table.chip_records == std::vector<std::uint16_t>({1u, 0u, 1u}),
            "height table chip mapping mismatch") ||
        !check(
            first.has_value() && *first == 0xFFu &&
                second.has_value() && *second == 0x80u &&
                repeated.has_value() && *repeated == 0xA5u,
            "height table lookup mismatch") ||
        !check(source == source_before, "terrain parser mutated height input")) {
        return false;
    }

    source[kTerrainHeaderSize] = 0x12u;
    if (!check(table.records[0] == 0u, "terrain output aliases input storage")) {
        return false;
    }

    table.records[0] = 0x34u;
    return check(source[kTerrainHeaderSize] == 0x12u, "terrain input aliases output storage");
}

bool test_angle_table_decodes_large_little_endian_chip_ids() {
    constexpr std::size_t chip_record = 0x0123u;
    const std::size_t record_size = terrain_record_size(TerrainRecordKind::Angle);
    std::vector<std::uint8_t> records((chip_record + 1u) * record_size, 0u);
    records[2u] = 0xA5u;
    records[chip_record * record_size] = 0xE1u;
    records[chip_record * record_size + record_size - 1u] = 0xF2u;

    std::vector<std::uint8_t> source =
        make_table(TerrainRecordKind::Angle, records, {0x0123u, 0u, 0x0123u});
    const std::size_t index_table_offset = kTerrainHeaderSize + records.size();
    if (!check(
            source[index_table_offset] == 0x23u && source[index_table_offset + 1u] == 0x01u,
            "large chip id test fixture is not little-endian")) {
        return false;
    }

    const TerrainTable table =
        parse_terrain_table(source.data(), source.size(), TerrainRecordKind::Angle);
    const std::optional<std::uint8_t> first = terrain_byte(table, 0u, 0u);
    const std::optional<std::uint8_t> second = terrain_byte(table, 1u, 2u);
    const std::optional<std::uint8_t> repeated =
        terrain_byte(table, 2u, record_size - 1u);
    return check(table.kind == TerrainRecordKind::Angle, "angle table kind mismatch") &&
           check(
               table.record_count == chip_record + 1u,
               "angle table large record count mismatch") &&
           check(table.records == records, "angle table raw record bytes mismatch") &&
           check(
               table.chip_records == std::vector<std::uint16_t>({0x0123u, 0u, 0x0123u}),
               "angle table little-endian chip ids mismatch") &&
           check(
               first.has_value() && *first == 0xE1u &&
                   second.has_value() && *second == 0xA5u &&
                   repeated.has_value() && *repeated == 0xF2u,
               "angle table reordered or repeated lookup mismatch");
}

bool test_attribute_table_preserves_high_bit_values() {
    const std::size_t record_size = terrain_record_size(TerrainRecordKind::Attribute);
    std::vector<std::uint8_t> records(record_size * 2u, 0u);
    records[0u] = 0x80u;
    records[record_size - 1u] = 0xFEu;
    records[record_size] = 0xFFu;

    const std::vector<std::uint8_t> source =
        make_table(TerrainRecordKind::Attribute, records, {1u, 0u, 1u});
    const TerrainTable table =
        parse_terrain_table(source.data(), source.size(), TerrainRecordKind::Attribute);
    const std::optional<std::uint8_t> first = terrain_byte(table, 0u, 0u);
    const std::optional<std::uint8_t> second =
        terrain_byte(table, 1u, record_size - 1u);
    return check(table.kind == TerrainRecordKind::Attribute, "attribute table kind mismatch") &&
           check(table.record_count == 2u, "attribute table record count mismatch") &&
           check(table.records == records, "attribute table raw record bytes mismatch") &&
           check(
               first.has_value() && *first == 0xFFu &&
                   second.has_value() && *second == 0xFEu,
               "attribute table high-bit lookup mismatch");
}

bool test_empty_tables_are_valid() {
    const std::array<TerrainRecordKind, 3u> kinds = {
        TerrainRecordKind::Height,
        TerrainRecordKind::Angle,
        TerrainRecordKind::Attribute,
    };
    std::vector<std::uint8_t> source = {0u, 0u, 0u, 0u};
    const std::vector<std::uint8_t> source_before = source;

    for (const TerrainRecordKind kind : kinds) {
        const TerrainTable table = parse_terrain_table(source.data(), source.size(), kind);
        if (!check(table.kind == kind, "empty table kind mismatch") ||
            !check(table.record_count == 0u, "empty table record count mismatch") ||
            !check(table.records.empty() && table.chip_records.empty(), "empty table storage mismatch") ||
            !check(!terrain_byte(table, 0u, 0u).has_value(), "empty table returned a byte")) {
            return false;
        }
    }

    if (!check(source == source_before, "empty terrain parser mutated input")) {
        return false;
    }

    const std::vector<std::uint8_t> unused_records(
        terrain_record_size(TerrainRecordKind::Angle),
        0x9Au);
    const std::vector<std::uint8_t> unused_source =
        make_table(TerrainRecordKind::Angle, unused_records, {});
    const TerrainTable unused_table =
        parse_terrain_table(
            unused_source.data(),
            unused_source.size(),
            TerrainRecordKind::Angle);
    return check(
               unused_table.record_count == 1u &&
                   unused_table.records == unused_records &&
                   unused_table.chip_records.empty(),
               "unused terrain records without chips were rejected or lost") &&
           check(
               !terrain_byte(unused_table, 0u, 0u).has_value(),
               "terrain lookup found a chip in an empty chip table");
}

bool test_parser_rejects_invalid_data() {
    const TerrainRecordKind invalid_kind = static_cast<TerrainRecordKind>(99);
    const std::vector<std::uint8_t> empty = {0u, 0u, 0u, 0u};
    const std::vector<std::uint8_t> short_header = {0u, 0u, 0u};
    std::vector<std::uint8_t> truncated_body(
        kTerrainHeaderSize + terrain_record_size(TerrainRecordKind::Height) - 1u,
        0u);
    write_le16(truncated_body, 2u, 1u);
    std::vector<std::uint8_t> truncated_index(
        kTerrainHeaderSize + terrain_record_size(TerrainRecordKind::Angle),
        0u);
    write_le16(truncated_index, 0u, 1u);
    write_le16(truncated_index, 2u, 1u);

    const std::vector<std::uint8_t> trailing = {0u, 0u, 0u, 0u, 0xA5u};
    const std::vector<std::uint8_t> angle_table = make_table(
        TerrainRecordKind::Angle,
        std::vector<std::uint8_t>(terrain_record_size(TerrainRecordKind::Angle), 0u),
        {});
    const std::vector<std::uint8_t> huge_counts = {0xFFu, 0xFFu, 0xFFu, 0xFFu};
    const std::vector<std::uint8_t> out_of_range_chip = make_table(
        TerrainRecordKind::Angle,
        std::vector<std::uint8_t>(terrain_record_size(TerrainRecordKind::Angle), 0u),
        {1u});
    const std::vector<std::uint8_t> absent_record = {1u, 0u, 0u, 0u, 0u, 0u};

    return expects_stage_data_error(
               [&]() { terrain_record_size(invalid_kind); },
               "invalid terrain kind was accepted for its record size") &&
           expects_stage_data_error(
               [&]() { parse_terrain_table(empty.data(), empty.size(), invalid_kind); },
               "invalid terrain kind was accepted by the parser") &&
           expects_stage_data_error(
               [&]() { parse_terrain_table(nullptr, 0u, TerrainRecordKind::Height); },
               "null terrain data was accepted") &&
           expects_stage_data_error(
               [&]() {
                   parse_terrain_table(
                       short_header.data(),
                       short_header.size(),
                       TerrainRecordKind::Height);
               },
               "truncated terrain header was accepted") &&
           expects_stage_data_error(
               [&]() {
                   parse_terrain_table(
                       truncated_body.data(),
                       truncated_body.size(),
                       TerrainRecordKind::Height);
               },
               "truncated terrain record body was accepted") &&
           expects_stage_data_error(
               [&]() {
                   parse_terrain_table(
                       truncated_index.data(),
                       truncated_index.size(),
                       TerrainRecordKind::Angle);
               },
               "truncated terrain chip table was accepted") &&
           expects_stage_data_error(
               [&]() { parse_terrain_table(trailing.data(), trailing.size(), TerrainRecordKind::Angle); },
               "terrain table with trailing bytes was accepted") &&
           expects_stage_data_error(
               [&]() {
                   parse_terrain_table(
                       angle_table.data(),
                       angle_table.size(),
                       TerrainRecordKind::Height);
               },
               "terrain table with mismatched record format was accepted") &&
           expects_stage_data_error(
               [&]() {
                   parse_terrain_table(
                       huge_counts.data(),
                       huge_counts.size(),
                       TerrainRecordKind::Angle);
               },
               "huge terrain counts with tiny input were accepted") &&
           expects_stage_data_error(
               [&]() {
                   parse_terrain_table(
                       out_of_range_chip.data(),
                       out_of_range_chip.size(),
                       TerrainRecordKind::Angle);
               },
               "out-of-range terrain record id was accepted") &&
           expects_stage_data_error(
               [&]() {
                   parse_terrain_table(
                       absent_record.data(),
                       absent_record.size(),
                       TerrainRecordKind::Angle);
               },
               "terrain chip referenced an absent record");
}

bool test_lookup_rejects_out_of_range_and_mutated_tables() {
    const std::size_t record_size = terrain_record_size(TerrainRecordKind::Attribute);
    std::vector<std::uint8_t> records(record_size * 2u, 0u);
    records[record_size] = 0xB4u;
    const std::vector<std::uint8_t> source =
        make_table(TerrainRecordKind::Attribute, records, {1u, 0u});
    const TerrainTable table =
        parse_terrain_table(source.data(), source.size(), TerrainRecordKind::Attribute);

    TerrainTable short_records = table;
    short_records.records.pop_back();
    TerrainTable mismatched_count = table;
    mismatched_count.record_count = 1u;
    TerrainTable bad_chip_record = table;
    bad_chip_record.chip_records[0u] = 2u;
    TerrainTable invalid_kind = table;
    invalid_kind.kind = static_cast<TerrainRecordKind>(99);

    return check(!terrain_byte(table, 2u, 0u).has_value(), "out-of-range terrain chip lookup succeeded") &&
           check(
               !terrain_byte(table, 0u, record_size).has_value(),
               "out-of-range terrain byte lookup succeeded") &&
           check(
               !terrain_byte(short_records, 0u, 0u).has_value(),
               "lookup accepted truncated mutable terrain records") &&
           check(
               !terrain_byte(mismatched_count, 0u, 0u).has_value(),
               "lookup accepted mismatched mutable terrain record count") &&
           check(
               !terrain_byte(bad_chip_record, 0u, 0u).has_value(),
               "lookup accepted mutable out-of-range terrain record id") &&
           expects_stage_data_error(
               [&]() { terrain_byte(invalid_kind, 0u, 0u); },
               "lookup accepted an unsupported mutable terrain kind");
}

}

int main() {
    return test_record_sizes_and_height_table() &&
                   test_angle_table_decodes_large_little_endian_chip_ids() &&
                   test_attribute_table_preserves_high_bit_values() &&
                   test_empty_tables_are_valid() &&
                   test_parser_rejects_invalid_data() &&
                   test_lookup_rejects_out_of_range_and_mutated_tables()
               ? 0
               : 1;
}
