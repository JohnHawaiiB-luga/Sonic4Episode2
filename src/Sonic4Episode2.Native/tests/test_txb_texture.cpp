#include "txb_texture.h"

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <exception>
#include <string>
#include <vector>

namespace {

constexpr std::size_t kHeaderSize = 16u;
constexpr std::size_t kListSize = 8u;
constexpr std::size_t kRecordSize = 20u;

struct RecordSpec {
    std::uint32_t file_type;
    std::uint32_t name_offset;
    std::uint16_t min_filter;
    std::uint16_t mag_filter;
    std::uint32_t global_index;
    std::uint32_t bank;
};

bool check(bool condition, const char* message) {
    if (condition) {
        return true;
    }
    std::fprintf(stderr, "%s\n", message);
    return false;
}

void write_be16(std::vector<std::uint8_t>& data, std::size_t offset, std::uint16_t value) {
    data[offset] = static_cast<std::uint8_t>(value >> 8u);
    data[offset + 1u] = static_cast<std::uint8_t>(value & 0xffu);
}

void write_be32(std::vector<std::uint8_t>& data, std::size_t offset, std::uint32_t value) {
    data[offset] = static_cast<std::uint8_t>(value >> 24u);
    data[offset + 1u] = static_cast<std::uint8_t>((value >> 16u) & 0xffu);
    data[offset + 2u] = static_cast<std::uint8_t>((value >> 8u) & 0xffu);
    data[offset + 3u] = static_cast<std::uint8_t>(value & 0xffu);
}

void write_magic(std::vector<std::uint8_t>& data) {
    data[0u] = static_cast<std::uint8_t>('#');
    data[1u] = static_cast<std::uint8_t>('T');
    data[2u] = static_cast<std::uint8_t>('X');
    data[3u] = static_cast<std::uint8_t>('B');
}

void write_name(std::vector<std::uint8_t>& data, std::size_t offset, const std::string& name) {
    for (std::size_t index = 0u; index < name.size(); ++index) {
        data[offset + index] = static_cast<std::uint8_t>(name[index]);
    }
    data[offset + name.size()] = 0u;
}

std::vector<std::uint8_t> make_txb(
    std::size_t list_offset,
    std::size_t records_offset,
    const std::vector<RecordSpec>& records,
    std::size_t size) {
    std::vector<std::uint8_t> data(size, 0u);
    write_magic(data);
    write_be32(data, 4u, static_cast<std::uint32_t>(list_offset));
    write_be32(data, list_offset, static_cast<std::uint32_t>(records.size()));
    write_be32(data, list_offset + 4u, static_cast<std::uint32_t>(records_offset));
    for (std::size_t index = 0u; index < records.size(); ++index) {
        const RecordSpec& record = records[index];
        const std::size_t offset = records_offset + index * kRecordSize;
        write_be32(data, offset, record.file_type);
        write_be32(data, offset + 4u, record.name_offset);
        write_be16(data, offset + 8u, record.min_filter);
        write_be16(data, offset + 10u, record.mag_filter);
        write_be32(data, offset + 12u, record.global_index);
        write_be32(data, offset + 16u, record.bank);
    }
    return data;
}

std::vector<std::uint8_t> single_record_bytes(const std::vector<std::uint8_t>& name) {
    const std::size_t records_offset = kHeaderSize + kListSize;
    const std::size_t name_offset = records_offset + kRecordSize;
    std::vector<std::uint8_t> data = make_txb(
        kHeaderSize,
        records_offset,
        {{0x10203040u, static_cast<std::uint32_t>(name_offset), 0x0001u, 0x0005u, 0x50607080u, 0x90a0b0c0u}},
        name_offset + name.size());
    for (std::size_t index = 0u; index < name.size(); ++index) {
        data[name_offset + index] = name[index];
    }
    return data;
}

bool check_texture(
    const TxbTextureData& texture,
    std::uint32_t file_type,
    std::uint16_t min_filter,
    std::uint16_t mag_filter,
    std::uint32_t global_index,
    std::uint32_t bank,
    const std::string& name,
    const char* message) {
    return check(
        texture.file_type == file_type &&
            texture.min_filter == min_filter &&
            texture.mag_filter == mag_filter &&
            texture.global_index == global_index &&
            texture.bank == bank &&
            texture.name == name,
        message);
}

bool rejects(std::vector<std::uint8_t> input, const char* message) {
    const std::vector<std::uint8_t> original = input;
    try {
        static_cast<void>(parse_pc_txb_texture_list(input.empty() ? nullptr : input.data(), input.size()));
    } catch (const TxbTextureError&) {
        return check(input == original, "rejected TXB parser input was modified");
    } catch (const std::exception&) {
        return check(false, message);
    }
    return check(false, message);
}

bool test_parses_big_endian_records_with_gaps_and_preserves_input() {
    const std::vector<RecordSpec> records = {{
        {0xfedcba98u, 120u, 0xfedcu, 0x8001u, 0xf1234567u, 0x89abcdefu},
        {0x80000001u, 100u, 0x0004u, 0x0001u, 0x7fffffffu, 0x00000002u},
        {0x01234567u, 100u, 0x0005u, 0x0000u, 0x80000000u, 0xffffffffu},
    }};
    std::vector<std::uint8_t> input = make_txb(20u, 36u, records, 160u);
    for (std::size_t index = 8u; index < kHeaderSize; ++index) {
        input[index] = static_cast<std::uint8_t>(0x90u + static_cast<std::uint8_t>(index));
    }
    for (std::size_t index = kHeaderSize; index < 20u; ++index) {
        input[index] = 0x55u;
    }
    for (std::size_t index = 28u; index < 36u; ++index) {
        input[index] = 0xaau;
    }
    write_name(input, 100u, "shared-name.dds");
    write_name(input, 120u, "Later\\MiXeD_01.DDS");
    const std::vector<std::uint8_t> original = input;

    std::vector<TxbTextureData> parsed = parse_pc_txb_texture_list(input.data(), input.size());
    const bool input_unchanged = input == original;
    const bool parsed_expected =
        check(parsed.size() == 3u, "TXB record count mismatch") &&
        check_texture(
            parsed[0u],
            0xfedcba98u,
            0xfedcu,
            0x8001u,
            0xf1234567u,
            0x89abcdefu,
            "Later\\MiXeD_01.DDS",
            "first mixed-endian TXB record mismatch") &&
        check_texture(
            parsed[1u],
            0x80000001u,
            0x0004u,
            0x0001u,
            0x7fffffffu,
            0x00000002u,
            "shared-name.dds",
            "second mixed-endian TXB record mismatch") &&
        check_texture(
            parsed[2u],
            0x01234567u,
            0x0005u,
            0x0000u,
            0x80000000u,
            0xffffffffu,
            "shared-name.dds",
            "third mixed-endian TXB record mismatch");
    input.assign(input.size(), 0u);

    return parsed_expected &&
           check(input_unchanged, "accepted TXB parser input was modified") &&
           check(original != input, "TXB ownership fixture did not change input") &&
           check(
               parsed[0u].name == "Later\\MiXeD_01.DDS" &&
                   parsed[1u].name == "shared-name.dds" &&
                   parsed[2u].name == "shared-name.dds",
               "TXB parsed names did not own their bytes") &&
           check(
               original[8u] != 0u && original[16u] == 0x55u && original[28u] == 0xaau,
               "TXB reserved or gap fixture was not populated") &&
           check(parsed_expected, "TXB parsed values changed after input mutation");
}

bool test_accepts_empty_lists_and_exact_end_terminator() {
    const std::vector<std::uint8_t> zero_table = make_txb(kHeaderSize, 0u, {}, kHeaderSize + kListSize);
    const std::vector<std::uint8_t> post_list_table = make_txb(20u, 32u, {}, 32u);
    const std::vector<std::uint8_t> exact_end = single_record_bytes({0x78u, 0u});
    const std::vector<TxbTextureData> zero_table_result =
        parse_pc_txb_texture_list(zero_table.data(), zero_table.size());
    const std::vector<TxbTextureData> post_list_result =
        parse_pc_txb_texture_list(post_list_table.data(), post_list_table.size());
    const std::vector<TxbTextureData> exact_end_result =
        parse_pc_txb_texture_list(exact_end.data(), exact_end.size());

    return check(zero_table_result.empty(), "empty TXB with null table was not accepted") &&
           check(post_list_result.empty(), "empty TXB with post-list table was not accepted") &&
           check(exact_end_result.size() == 1u, "exact-end TXB name record count mismatch") &&
           check_texture(
               exact_end_result[0u],
               0x10203040u,
               0x0001u,
               0x0005u,
               0x50607080u,
               0x90a0b0c0u,
               "x",
               "exact-end TXB name was not preserved");
}

bool test_rejects_truncation_magic_overlap_and_huge_offsets() {
    const std::vector<std::uint8_t> valid = single_record_bytes({0x6eu, 0x61u, 0x6du, 0x65u, 0u});
    std::vector<std::uint8_t> truncated_list(kHeaderSize, 0u);
    write_magic(truncated_list);
    write_be32(truncated_list, 4u, static_cast<std::uint32_t>(kHeaderSize));
    std::vector<std::uint8_t> truncated_record = make_txb(kHeaderSize, 24u, {}, 43u);
    write_be32(truncated_record, kHeaderSize, 1u);
    std::vector<std::uint8_t> unsupported_magic = valid;
    unsupported_magic[0u] = static_cast<std::uint8_t>('?');
    std::vector<std::uint8_t> list_overlaps_header = valid;
    write_be32(list_overlaps_header, 4u, 12u);
    std::vector<std::uint8_t> records_overlap_list = valid;
    write_be32(records_overlap_list, 20u, 20u);
    std::vector<std::uint8_t> name_overlaps_records = valid;
    write_be32(name_overlaps_records, 28u, 24u);
    std::vector<std::uint8_t> name_out_of_range = valid;
    write_be32(name_out_of_range, 28u, static_cast<std::uint32_t>(name_out_of_range.size()));
    std::vector<std::uint8_t> huge_count = valid;
    write_be32(huge_count, kHeaderSize, 0xffffffffu);
    std::vector<std::uint8_t> huge_list_offset = valid;
    write_be32(huge_list_offset, 4u, 0xffffffffu);
    std::vector<std::uint8_t> huge_records_offset = valid;
    write_be32(huge_records_offset, 20u, 0xffffffffu);

    return rejects({}, "null TXB input was accepted") &&
           rejects(std::vector<std::uint8_t>(15u, 0u), "truncated TXB header was accepted") &&
           rejects(truncated_list, "truncated TXB list was accepted") &&
           rejects(truncated_record, "truncated TXB record table was accepted") &&
           rejects(unsupported_magic, "unsupported TXB magic was accepted") &&
           rejects(list_overlaps_header, "TXB list overlapped header") &&
           rejects(records_overlap_list, "TXB record table overlapped list") &&
           rejects(name_overlaps_records, "TXB name overlapped record table") &&
           rejects(name_out_of_range, "TXB name offset at end of input was accepted") &&
           rejects(huge_count, "huge TXB count was accepted") &&
           rejects(huge_list_offset, "huge TXB list offset was accepted") &&
           rejects(huge_records_offset, "huge TXB record offset was accepted");
}

bool test_rejects_empty_nonprintable_and_unterminated_names() {
    return rejects(single_record_bytes({0u}), "empty TXB name was accepted") &&
           rejects(single_record_bytes({0x80u, 0u}), "non-ASCII TXB name was accepted") &&
           rejects(single_record_bytes({0x1fu, 0u}), "nonprintable TXB name was accepted") &&
           rejects(
               single_record_bytes({0x6eu, 0x61u, 0x6du, 0x65u}),
               "unterminated TXB name was accepted");
}

bool test_rejects_invalid_empty_locations_and_amplified_names() {
    const auto overlapping_empty = make_txb(16u, 20u, {}, 24u);
    const auto out_of_range_empty = make_txb(16u, 25u, {}, 24u);
    constexpr std::uint32_t name_offset = 24u + 32u * 20u;
    const std::vector<RecordSpec> records(32u, {0u, name_offset, 1u, 1u, 0u, 0u});
    auto shared = make_txb(16u, 24u, records, name_offset + 4097u);
    write_name(shared, name_offset, std::string(4096u, 'a'));
    const auto original = shared;
    bool budget_rejected = false;
    try {
        static_cast<void>(parse_pc_txb_texture_list(shared.data(), shared.size()));
    } catch (const TxbTextureError& error) {
        budget_rejected = std::string(error.what()) == "TXB texture name output is too large.";
    }
    return rejects(overlapping_empty, "empty TXB table overlapped its list") &&
           rejects(out_of_range_empty, "empty TXB table was outside the input") &&
           check(budget_rejected, "shared TXB names bypassed the output budget") &&
           check(shared == original, "output-budget rejection modified TXB input");
}

}

int main() {
    return test_parses_big_endian_records_with_gaps_and_preserves_input() &&
                   test_accepts_empty_lists_and_exact_end_terminator() &&
                   test_rejects_truncation_magic_overlap_and_huge_offsets() &&
                   test_rejects_empty_nonprintable_and_unterminated_names() &&
                   test_rejects_invalid_empty_locations_and_amplified_names()
               ? 0
               : 1;
}
