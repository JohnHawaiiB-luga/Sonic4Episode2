#include "cri_audio_bank.h"

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {

constexpr std::size_t kUtfBase = 8u;
constexpr std::size_t kUtfHeaderSize = 0x20u;
constexpr std::size_t kContainerHeaderSize = 16u;
constexpr std::uint8_t kStorageZero = 0x10u;
constexpr std::uint8_t kStorageConstant = 0x30u;
constexpr std::uint8_t kStoragePerRow = 0x50u;
constexpr std::uint8_t kTypeU8 = 0x00u;
constexpr std::uint8_t kTypeI8 = 0x01u;
constexpr std::uint8_t kTypeU16 = 0x02u;
constexpr std::uint8_t kTypeI16 = 0x03u;
constexpr std::uint8_t kTypeU32 = 0x04u;
constexpr std::uint8_t kTypeI32 = 0x05u;
constexpr std::uint8_t kTypeU64 = 0x06u;
constexpr std::uint8_t kTypeI64 = 0x07u;
constexpr std::uint8_t kTypeF32 = 0x08u;
constexpr std::uint8_t kTypeF64 = 0x09u;
constexpr std::uint8_t kTypeString = 0x0au;
constexpr std::uint8_t kTypeData = 0x0bu;

[[noreturn]] void fail(const char* message) {
    throw std::invalid_argument(message);
}

bool fits_range(std::size_t offset, std::size_t length, std::size_t size) {
    return offset <= size && length <= size - offset;
}

void require_range(
    std::size_t offset,
    std::size_t length,
    std::size_t size,
    const char* message) {
    if (!fits_range(offset, length, size)) {
        fail(message);
    }
}

std::size_t checked_add(std::size_t left, std::size_t right, const char* message) {
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

std::size_t checked_size(std::uint32_t value, const char* message) {
    const std::size_t result = static_cast<std::size_t>(value);
    if (static_cast<std::uint32_t>(result) != value) {
        fail(message);
    }
    return result;
}

std::size_t checked_size(std::uint64_t value, const char* message) {
    const std::size_t result = static_cast<std::size_t>(value);
    if (static_cast<std::uint64_t>(result) != value) {
        fail(message);
    }
    return result;
}

std::uint16_t read_be16(const std::uint8_t* data, std::size_t offset) {
    return static_cast<std::uint16_t>(
        (static_cast<std::uint16_t>(data[offset]) << 8u) |
        static_cast<std::uint16_t>(data[offset + 1u]));
}

std::uint32_t read_be32(const std::uint8_t* data, std::size_t offset) {
    return (static_cast<std::uint32_t>(data[offset]) << 24u) |
           (static_cast<std::uint32_t>(data[offset + 1u]) << 16u) |
           (static_cast<std::uint32_t>(data[offset + 2u]) << 8u) |
           static_cast<std::uint32_t>(data[offset + 3u]);
}

std::uint64_t read_be64(const std::uint8_t* data, std::size_t offset) {
    return (static_cast<std::uint64_t>(data[offset]) << 56u) |
           (static_cast<std::uint64_t>(data[offset + 1u]) << 48u) |
           (static_cast<std::uint64_t>(data[offset + 2u]) << 40u) |
           (static_cast<std::uint64_t>(data[offset + 3u]) << 32u) |
           (static_cast<std::uint64_t>(data[offset + 4u]) << 24u) |
           (static_cast<std::uint64_t>(data[offset + 5u]) << 16u) |
           (static_cast<std::uint64_t>(data[offset + 6u]) << 8u) |
           static_cast<std::uint64_t>(data[offset + 7u]);
}

std::size_t type_size(std::uint8_t type) {
    switch (type) {
    case kTypeU8:
    case kTypeI8:
        return 1u;
    case kTypeU16:
    case kTypeI16:
        return 2u;
    case kTypeU32:
    case kTypeI32:
    case kTypeF32:
    case kTypeString:
        return 4u;
    case kTypeU64:
    case kTypeI64:
    case kTypeF64:
    case kTypeData:
        return 8u;
    default:
        fail("CRI UTF column type is unsupported.");
    }
}

bool is_unsigned_integer(std::uint8_t type) {
    return type == kTypeU8 || type == kTypeU16 ||
           type == kTypeU32 || type == kTypeU64;
}

bool is_signed_integer(std::uint8_t type) {
    return type == kTypeI8 || type == kTypeI16 ||
           type == kTypeI32 || type == kTypeI64;
}

std::uint64_t unsigned_bits(
    const std::uint8_t* data,
    std::size_t offset,
    std::uint8_t type) {
    switch (type) {
    case kTypeU8:
    case kTypeI8:
        return data[offset];
    case kTypeU16:
    case kTypeI16:
        return read_be16(data, offset);
    case kTypeU32:
    case kTypeI32:
        return read_be32(data, offset);
    case kTypeU64:
    case kTypeI64:
        return read_be64(data, offset);
    default:
        fail("CRI UTF value is not an integer.");
    }
}

std::int64_t signed_bits(std::uint64_t value, std::uint8_t type) {
    std::uint8_t bits = 0u;
    switch (type) {
    case kTypeI8:
        bits = 8u;
        break;
    case kTypeI16:
        bits = 16u;
        break;
    case kTypeI32:
        bits = 32u;
        break;
    case kTypeI64:
        bits = 64u;
        break;
    default:
        fail("CRI UTF value is not signed.");
    }

    const std::uint64_t mask = bits == 64u
        ? std::numeric_limits<std::uint64_t>::max()
        : (static_cast<std::uint64_t>(1u) << bits) - 1u;
    value &= mask;
    const std::uint64_t sign_bit = static_cast<std::uint64_t>(1u) << (bits - 1u);
    if ((value & sign_bit) == 0u) {
        return static_cast<std::int64_t>(value);
    }
    if (bits == 64u && value == sign_bit) {
        return std::numeric_limits<std::int64_t>::min();
    }
    const std::uint64_t magnitude = ((~value) & mask) + 1u;
    return -static_cast<std::int64_t>(magnitude);
}

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
    std::size_t available_;
};

OutputBudget make_output_budget(std::size_t input_size) {
    return OutputBudget(checked_product(
        input_size,
        2u,
        "CRI audio output budget is too large."));
}

struct StringSpan {
    const std::uint8_t* data = nullptr;
    std::size_t size = 0u;
};

struct DataSpan {
    const std::uint8_t* data = nullptr;
    std::size_t size = 0u;
};

bool spans_equal(StringSpan left, StringSpan right) {
    return left.size == right.size &&
           (left.size == 0u || std::memcmp(left.data, right.data, left.size) == 0);
}

bool span_equals(StringSpan left, std::string_view right) {
    return left.size == right.size() &&
           (left.size == 0u || std::memcmp(left.data, right.data(), left.size) == 0);
}

class UtfTable {
public:
    static UtfTable parse(const std::uint8_t* data, std::size_t size) {
        if (data == nullptr) {
            fail("CRI UTF data is null.");
        }
        if (size < kUtfHeaderSize) {
            fail("CRI UTF header is truncated.");
        }
        if (data[0u] != static_cast<std::uint8_t>('@') ||
            data[1u] != static_cast<std::uint8_t>('U') ||
            data[2u] != static_cast<std::uint8_t>('T') ||
            data[3u] != static_cast<std::uint8_t>('F')) {
            fail("CRI UTF magic is unsupported.");
        }

        UtfTable table;
        table.data_ = data;
        const std::size_t table_size = checked_size(
            read_be32(data, 4u),
            "CRI UTF table size is too large.");
        if (table_size < kUtfHeaderSize - kUtfBase) {
            fail("CRI UTF table size is too small.");
        }
        table.table_end_ = checked_add(
            kUtfBase,
            table_size,
            "CRI UTF table size overflows.");
        require_range(
            kUtfBase,
            table_size,
            size,
            "CRI UTF table is truncated.");

        table.rows_ = table.relative_offset(
            read_be32(data, 8u), "CRI UTF rows offset is too large.");
        table.strings_ = table.relative_offset(
            read_be32(data, 12u), "CRI UTF strings offset is too large.");
        table.data_pool_ = table.relative_offset(
            read_be32(data, 16u), "CRI UTF data offset is too large.");
        if (table.rows_ > table.table_end_ ||
            table.strings_ > table.table_end_ ||
            table.data_pool_ > table.table_end_) {
            fail("CRI UTF header offset is outside its table.");
        }

        const std::size_t column_count = read_be16(data, 24u);
        table.row_width_ = read_be16(data, 26u);
        table.row_count_ = checked_size(
            read_be32(data, 28u), "CRI UTF row count is too large.");
        if (table.row_count_ > table_size) {
            fail("CRI UTF row count exceeds its table size.");
        }
        const std::size_t row_bytes = checked_product(
            table.row_width_, table.row_count_, "CRI UTF rows are too large.");
        require_range(
            table.rows_, row_bytes, table.table_end_, "CRI UTF rows are truncated.");

        table.table_name_ = table.string_span_from_relative(read_be32(data, 20u));
        const std::size_t minimum_descriptors = checked_product(
            column_count, 5u, "CRI UTF column descriptors are too large.");
        if (kUtfHeaderSize > table.rows_ ||
            minimum_descriptors > table.rows_ - kUtfHeaderSize) {
            fail("CRI UTF column descriptors overlap rows.");
        }

        std::size_t cursor = kUtfHeaderSize;
        std::size_t per_row_size = 0u;
        table.columns_.reserve(column_count);
        for (std::size_t index = 0u; index < column_count; ++index) {
            require_range(
                cursor, 5u, table.rows_, "CRI UTF column descriptor is truncated.");
            const std::uint8_t flags = data[cursor];
            ++cursor;
            const std::uint8_t storage = flags & 0xf0u;
            const std::uint8_t type = flags & 0x0fu;
            const StringSpan name = table.string_span_from_relative(read_be32(data, cursor));
            cursor += 4u;
            for (const UtfColumn& existing : table.columns_) {
                if (spans_equal(existing.name, name)) {
                    fail("CRI UTF column name is duplicated.");
                }
            }
            const std::size_t value_size = type_size(type);

            UtfColumn column{};
            column.storage = storage;
            column.type = type;
            column.name = name;
            if (storage == kStorageZero) {
            } else if (storage == kStorageConstant) {
                require_range(
                    cursor, value_size, table.rows_, "CRI UTF constant is truncated.");
                column.constant_offset = cursor;
                cursor += value_size;
            } else if (storage == kStoragePerRow) {
                column.row_offset = per_row_size;
                per_row_size = checked_add(
                    per_row_size, value_size, "CRI UTF row layout is too large.");
            } else {
                fail("CRI UTF column storage is unsupported.");
            }
            table.columns_.push_back(column);
        }
        if (per_row_size != table.row_width_) {
            fail("CRI UTF row width does not match its descriptors.");
        }
        return table;
    }

    bool name_equals(std::string_view name) const {
        return span_equals(table_name_, name);
    }

    std::size_t row_count() const {
        return row_count_;
    }

    bool string_equals(
        std::size_t row,
        std::string_view column_name,
        std::string_view value) const {
        return span_equals(string_span_value(row, column_name), value);
    }

    bool string_equals(
        std::size_t row,
        std::string_view column_name,
        StringSpan value) const {
        return spans_equal(string_span_value(row, column_name), value);
    }

    StringSpan string_span_value(std::size_t row, std::string_view column_name) const {
        const UtfColumn& column = find_column(column_name);
        if (column.type != kTypeString) {
            fail("CRI UTF column is not a string.");
        }
        if (column.storage == kStorageZero) {
            return {};
        }
        return string_span_from_relative(read_be32(data_, value_offset(row, column)));
    }

    std::string copy_string(
        std::size_t row,
        std::string_view column_name,
        OutputBudget& output_budget) const {
        const StringSpan value = string_span_value(row, column_name);
        output_budget.claim(value.size, "CRI audio string output is too large.");
        if (value.size == 0u) {
            return {};
        }
        return std::string(reinterpret_cast<const char*>(value.data), value.size);
    }

    std::uint64_t unsigned_value(std::size_t row, std::string_view column_name) const {
        const UtfColumn& column = find_column(column_name);
        if (!is_unsigned_integer(column.type) && !is_signed_integer(column.type)) {
            fail("CRI UTF column is not an integer.");
        }
        if (column.storage == kStorageZero) {
            return 0u;
        }
        const std::uint64_t value = unsigned_bits(
            data_, value_offset(row, column), column.type);
        if (is_signed_integer(column.type)) {
            const std::int64_t signed_value = signed_bits(value, column.type);
            if (signed_value < 0) {
                fail("CRI UTF integer is negative.");
            }
            return static_cast<std::uint64_t>(signed_value);
        }
        return value;
    }

    std::uint32_t u32_value(std::size_t row, std::string_view column_name) const {
        const std::uint64_t value = unsigned_value(row, column_name);
        if (value > std::numeric_limits<std::uint32_t>::max()) {
            fail("CRI UTF integer does not fit u32.");
        }
        return static_cast<std::uint32_t>(value);
    }

    std::int32_t i32_value(std::size_t row, std::string_view column_name) const {
        const UtfColumn& column = find_column(column_name);
        if (!is_unsigned_integer(column.type) && !is_signed_integer(column.type)) {
            fail("CRI UTF column is not an integer.");
        }
        if (column.storage == kStorageZero) {
            return 0;
        }
        const std::uint64_t bits = unsigned_bits(
            data_, value_offset(row, column), column.type);
        const std::int64_t value = is_signed_integer(column.type)
            ? signed_bits(bits, column.type)
            : checked_signed(bits);
        if (value < std::numeric_limits<std::int32_t>::min() ||
            value > std::numeric_limits<std::int32_t>::max()) {
            fail("CRI UTF integer does not fit i32.");
        }
        return static_cast<std::int32_t>(value);
    }

    DataSpan data_value(std::size_t row, std::string_view column_name) const {
        const UtfColumn& column = find_column(column_name);
        if (column.type != kTypeData) {
            fail("CRI UTF column is not data.");
        }
        if (column.storage == kStorageZero) {
            return {};
        }
        const std::size_t offset = checked_size(
            read_be32(data_, value_offset(row, column)),
            "CRI UTF data offset is too large.");
        const std::size_t length = checked_size(
            read_be32(data_, value_offset(row, column) + 4u),
            "CRI UTF data length is too large.");
        const std::size_t start = checked_add(
            data_pool_, offset, "CRI UTF data range overflows.");
        require_range(start, length, table_end_, "CRI UTF data is truncated.");
        return {data_ + start, length};
    }

private:
    struct UtfColumn {
        std::uint8_t storage = 0u;
        std::uint8_t type = 0u;
        StringSpan name{};
        std::size_t constant_offset = 0u;
        std::size_t row_offset = 0u;
    };

    std::size_t relative_offset(std::uint32_t value, const char* message) const {
        return checked_add(kUtfBase, checked_size(value, message), message);
    }

    StringSpan string_span_from_relative(std::uint32_t relative) const {
        const std::size_t start = checked_add(
            strings_,
            checked_size(relative, "CRI UTF string offset is too large."),
            "CRI UTF string range overflows.");
        if (start >= table_end_) {
            fail("CRI UTF string offset is outside its table.");
        }
        std::size_t cursor = start;
        while (data_[cursor] != 0u) {
            if (cursor == table_end_ - 1u) {
                fail("CRI UTF string is not NUL-terminated.");
            }
            ++cursor;
        }
        return {data_ + start, cursor - start};
    }

    const UtfColumn& find_column(std::string_view name) const {
        for (const UtfColumn& column : columns_) {
            if (span_equals(column.name, name)) {
                return column;
            }
        }
        fail("CRI UTF required column is missing.");
    }

    std::size_t value_offset(std::size_t row, const UtfColumn& column) const {
        if (row >= row_count_) {
            fail("CRI UTF row index is outside its table.");
        }
        if (column.storage == kStorageConstant) {
            return column.constant_offset;
        }
        if (column.storage == kStoragePerRow) {
            return rows_ + row * row_width_ + column.row_offset;
        }
        fail("CRI UTF zero storage has no value bytes.");
    }

    static std::int64_t checked_signed(std::uint64_t value) {
        if (value > static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max())) {
            fail("CRI UTF integer does not fit i64.");
        }
        return static_cast<std::int64_t>(value);
    }

    const std::uint8_t* data_ = nullptr;
    std::size_t table_end_ = 0u;
    std::size_t rows_ = 0u;
    std::size_t strings_ = 0u;
    std::size_t data_pool_ = 0u;
    std::size_t row_width_ = 0u;
    std::size_t row_count_ = 0u;
    StringSpan table_name_{};
    std::vector<UtfColumn> columns_;
};

std::size_t find_unique_row(
    const UtfTable& table,
    std::string_view column_name,
    std::string_view value) {
    bool found = false;
    std::size_t selected = 0u;
    for (std::size_t row = 0u; row < table.row_count(); ++row) {
        if (!table.string_equals(row, column_name, value)) {
            continue;
        }
        if (found) {
            fail("CRI audio selected name is duplicated.");
        }
        found = true;
        selected = row;
    }
    if (!found) {
        fail("CRI audio selected name is missing.");
    }
    return selected;
}

std::size_t find_unique_row(
    const UtfTable& table,
    std::string_view column_name,
    StringSpan value) {
    bool found = false;
    std::size_t selected = 0u;
    for (std::size_t row = 0u; row < table.row_count(); ++row) {
        if (!table.string_equals(row, column_name, value)) {
            continue;
        }
        if (found) {
            fail("CRI audio selected name is duplicated.");
        }
        found = true;
        selected = row;
    }
    if (!found) {
        fail("CRI audio selected name is missing.");
    }
    return selected;
}

struct TableSpan {
    const std::uint8_t* data = nullptr;
    std::size_t size = 0u;
    bool present = false;
};

void select_table_span(TableSpan& selected, DataSpan data) {
    if (selected.present) {
        fail("CRI CSB nested table is duplicated.");
    }
    if (data.size == 0u) {
        fail("CRI CSB nested table is empty.");
    }
    selected.data = data.data;
    selected.size = data.size;
    selected.present = true;
}

struct CsbTables {
    UtfTable cue;
    UtfTable synth;
    UtfTable sound_element;
};

CsbTables parse_csb_tables(const std::uint8_t* data, std::size_t size) {
    const UtfTable root = UtfTable::parse(data, size);
    if (!root.name_equals("TBLCSB")) {
        fail("CRI CSB root table is unsupported.");
    }

    TableSpan info{};
    TableSpan cue{};
    TableSpan synth{};
    TableSpan sound_element{};
    for (std::size_t row = 0u; row < root.row_count(); ++row) {
        if (root.string_equals(row, "name", "INFO")) {
            select_table_span(info, root.data_value(row, "utf"));
        } else if (root.string_equals(row, "name", "CUE")) {
            select_table_span(cue, root.data_value(row, "utf"));
        } else if (root.string_equals(row, "name", "SYNTH")) {
            select_table_span(synth, root.data_value(row, "utf"));
        } else if (root.string_equals(row, "name", "SOUND_ELEMENT")) {
            select_table_span(sound_element, root.data_value(row, "utf"));
        }
    }
    if (!info.present || !cue.present || !synth.present || !sound_element.present) {
        fail("CRI CSB direct synth tables are missing.");
    }

    const UtfTable info_table = UtfTable::parse(info.data, info.size);
    if (!info_table.name_equals("TBL_INFO")) {
        fail("CRI CSB INFO table is unsupported.");
    }

    CsbTables tables{
        UtfTable::parse(cue.data, cue.size),
        UtfTable::parse(synth.data, synth.size),
        UtfTable::parse(sound_element.data, sound_element.size),
    };
    if (!tables.cue.name_equals("TBLCUE") ||
        !tables.synth.name_equals("TBLSYN") ||
        !tables.sound_element.name_equals("TBLSDL")) {
        fail("CRI CSB direct synth table is unsupported.");
    }
    return tables;
}

void require_zero(std::uint32_t value, const char* message) {
    if (value != 0u) {
        fail(message);
    }
}

std::string normalize_path(
    const std::uint8_t* data,
    std::size_t size,
    bool allow_empty) {
    if (size == 0u) {
        if (allow_empty) {
            return {};
        }
        fail("CRI CPK path is empty.");
    }

    std::string normalized;
    std::string component;
    for (std::size_t index = 0u; index <= size; ++index) {
        const bool at_end = index == size;
        const std::uint8_t value = at_end ? 0u : data[index];
        if (!at_end && value != static_cast<std::uint8_t>('/') &&
            value != static_cast<std::uint8_t>('\\')) {
            if (value < 0x20u || value == static_cast<std::uint8_t>(':')) {
                fail("CRI CPK path component is unsafe.");
            }
            component.push_back(static_cast<char>(value));
            continue;
        }
        if (component.empty() || component == "." || component == "..") {
            fail("CRI CPK path component is unsafe.");
        }
        if (!normalized.empty()) {
            normalized.push_back('/');
        }
        normalized += component;
        component.clear();
    }
    return normalized;
}

std::string normalized_cpk_path(StringSpan directory, StringSpan file_name) {
    const std::string normalized_directory = normalize_path(
        directory.data, directory.size, true);
    const std::string normalized_file = normalize_path(
        file_name.data, file_name.size, false);
    if (normalized_directory.empty()) {
        return normalized_file;
    }
    return normalized_directory + "/" + normalized_file;
}

bool has_magic(
    const std::uint8_t* data,
    std::size_t offset,
    char first,
    char second,
    char third,
    char fourth) {
    return data[offset] == static_cast<std::uint8_t>(first) &&
           data[offset + 1u] == static_cast<std::uint8_t>(second) &&
           data[offset + 2u] == static_cast<std::uint8_t>(third) &&
           data[offset + 3u] == static_cast<std::uint8_t>(fourth);
}

}

std::vector<CriAudioStream> read_cri_aax_streams(
    const std::uint8_t* data,
    std::size_t size) {
    const UtfTable table = UtfTable::parse(data, size);
    if (!table.name_equals("AAX")) {
        fail("CRI AAX table is unsupported.");
    }
    if (table.row_count() == 0u) {
        fail("CRI AAX table has no streams.");
    }

    OutputBudget output_budget = make_output_budget(size);
    std::vector<CriAudioStream> streams;
    for (std::size_t row = 0u; row < table.row_count(); ++row) {
        const DataSpan payload = table.data_value(row, "data");
        if (payload.size == 0u) {
            fail("CRI AAX stream has no payload.");
        }
        const std::uint32_t loop_flag = table.u32_value(row, "lpflg");
        if (loop_flag > 1u) {
            fail("CRI AAX loop flag is unsupported.");
        }
        output_budget.claim(
            sizeof(CriAudioStream), "CRI AAX stream metadata is too large.");
        output_budget.claim(payload.size, "CRI AAX stream output is too large.");

        CriAudioStream stream{};
        stream.loop_flag = loop_flag;
        stream.adx.assign(payload.data, payload.data + payload.size);
        streams.push_back(std::move(stream));
    }
    return streams;
}

CriAudioCue read_cri_csb_cue(
    const std::uint8_t* data,
    std::size_t size,
    const std::string& cue_name) {
    if (cue_name.empty()) {
        fail("CRI CSB cue name is empty.");
    }
    const CsbTables tables = parse_csb_tables(data, size);
    const std::size_t cue_row = find_unique_row(tables.cue, "name", cue_name);
    const StringSpan synth_name = tables.cue.string_span_value(cue_row, "synth");
    const std::size_t synth_row = find_unique_row(tables.synth, "synname", synth_name);
    const StringSpan element_name = tables.synth.string_span_value(synth_row, "lnkname");
    const std::size_t element_row = find_unique_row(
        tables.sound_element, "name", element_name);

    require_zero(tables.synth.u32_value(synth_row, "syntype"), "CRI synth type is unsupported.");
    require_zero(tables.synth.u32_value(synth_row, "cmplxtype"), "CRI synth complexity is unsupported.");
    require_zero(tables.synth.u32_value(synth_row, "pitch"), "CRI synth pitch is unsupported.");
    require_zero(tables.synth.u32_value(synth_row, "dlytim"), "CRI synth delay is unsupported.");
    require_zero(tables.synth.u32_value(synth_row, "repeat"), "CRI synth repeat is unsupported.");
    require_zero(tables.sound_element.u32_value(element_row, "fmt"), "CRI sound element format is unsupported.");

    CriAudioCue cue{};
    OutputBudget output_budget = make_output_budget(size);
    cue.name = tables.cue.copy_string(cue_row, "name", output_budget);
    cue.id = tables.cue.u32_value(cue_row, "id");
    cue.synth_volume = tables.synth.i32_value(synth_row, "volume");
    const auto spatial_value = [&](const char* name, double scale) {
        const auto value = tables.synth.i32_value(synth_row, name);
        if (value < -32768 || value > 32767) {
            fail("CRI spatial parameter exceeds its signed16 range.");
        }
        return static_cast<float>(static_cast<double>(value) / scale);
    };
    cue.spatial.volume = spatial_value("p3d_vo", 1000.0);
    cue.spatial.volume_gain = spatial_value("p3d_vg", 1000.0);
    cue.spatial.angle_degrees = spatial_value("p3d_ao", 1.0);
    cue.spatial.angle_gain = spatial_value("p3d_ag", 1000.0);
    cue.spatial.directionality = spatial_value("p3d_ido", 1000.0);
    cue.spatial.directionality_gain = spatial_value("p3d_idg", 1000.0);
    for (std::size_t index = 0u; index < cue.spatial.dry_levels.size(); ++index) {
        cue.spatial.dry_levels[index] = tables.synth.i32_value(synth_row, "dry" + std::to_string(index));
    }
    cue.spatial.dry_output = tables.synth.copy_string(synth_row, "dryoname", output_budget);
    cue.release_time = tables.synth.u32_value(synth_row, "eg_rel");
    cue.channels = tables.sound_element.u32_value(element_row, "nch");
    cue.sample_rate = tables.sound_element.u32_value(element_row, "sfreq");
    cue.declared_sample_count = tables.sound_element.u32_value(element_row, "nsmpl");
    if (cue.channels == 0u || cue.sample_rate == 0u || cue.declared_sample_count == 0u) {
        fail("CRI sound element metadata is invalid.");
    }

    const std::uint32_t stream_flag = tables.sound_element.u32_value(element_row, "stmflg");
    const DataSpan payload = tables.sound_element.data_value(element_row, "data");
    if (stream_flag == 0u) {
        cue.streams = read_cri_aax_streams(payload.data, payload.size);
    } else if (stream_flag == 1u) {
        if (payload.size != 0u) {
            fail("CRI streamed sound element has embedded data.");
        }
        cue.external_path = tables.sound_element.copy_string(
            element_row, "name", output_budget);
    } else {
        fail("CRI sound element stream flag is unsupported.");
    }
    return cue;
}

std::vector<std::uint8_t> read_cri_cpk_file(
    const std::uint8_t* data,
    std::size_t size,
    const std::string& path) {
    if (data == nullptr) {
        fail("CRI CPK data is null.");
    }
    require_range(0u, kContainerHeaderSize, size, "CRI CPK header is truncated.");
    if (!has_magic(data, 0u, 'C', 'P', 'K', ' ')) {
        fail("CRI CPK magic is unsupported.");
    }

    const UtfTable header = UtfTable::parse(
        data + kContainerHeaderSize, size - kContainerHeaderSize);
    if (!header.name_equals("CpkHeader") || header.row_count() != 1u) {
        fail("CRI CPK header table is unsupported.");
    }
    const std::size_t toc_offset = checked_size(
        header.unsigned_value(0u, "TocOffset"), "CRI CPK TOC offset is too large.");
    const std::size_t content_offset = checked_size(
        header.unsigned_value(0u, "ContentOffset"), "CRI CPK content offset is too large.");
    const std::size_t declared_files = checked_size(
        header.unsigned_value(0u, "Files"), "CRI CPK file count is too large.");
    require_range(
        toc_offset, kContainerHeaderSize, size, "CRI CPK TOC wrapper is truncated.");
    if (!has_magic(data, toc_offset, 'T', 'O', 'C', ' ')) {
        fail("CRI CPK TOC magic is unsupported.");
    }

    const UtfTable toc = UtfTable::parse(
        data + toc_offset + kContainerHeaderSize,
        size - toc_offset - kContainerHeaderSize);
    if (!toc.name_equals("CpkTocInfo") || toc.row_count() != declared_files) {
        fail("CRI CPK TOC table is unsupported.");
    }

    const std::string requested_path = normalize_path(
        reinterpret_cast<const std::uint8_t*>(path.data()), path.size(), false);
    const std::size_t archive_base = toc_offset < content_offset ? toc_offset : content_offset;
    bool found = false;
    DataSpan selected{};
    for (std::size_t row = 0u; row < toc.row_count(); ++row) {
        const std::string relative = normalized_cpk_path(
            toc.string_span_value(row, "DirName"),
            toc.string_span_value(row, "FileName"));
        const std::size_t stored_size = checked_size(
            toc.unsigned_value(row, "FileSize"), "CRI CPK file size is too large.");
        const std::size_t extracted_size = checked_size(
            toc.unsigned_value(row, "ExtractSize"), "CRI CPK extracted size is too large.");
        const std::size_t file_offset = checked_size(
            toc.unsigned_value(row, "FileOffset"), "CRI CPK file offset is too large.");
        static_cast<void>(toc.unsigned_value(row, "ID"));
        if (stored_size != extracted_size) {
            fail("CRI CPK compressed entries are unsupported.");
        }
        const std::size_t start = checked_add(
            archive_base, file_offset, "CRI CPK file range overflows.");
        require_range(start, stored_size, size, "CRI CPK file is truncated.");
        if (relative != requested_path) {
            continue;
        }
        if (found) {
            fail("CRI CPK selected path is duplicated.");
        }
        found = true;
        selected = {data + start, stored_size};
    }
    if (!found) {
        fail("CRI CPK selected path is missing.");
    }

    OutputBudget output_budget = make_output_budget(size);
    output_budget.claim(selected.size, "CRI CPK output is too large.");
    return std::vector<std::uint8_t>(selected.data, selected.data + selected.size);
}
