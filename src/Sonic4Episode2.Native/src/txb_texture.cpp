#include "txb_texture.h"

#include <cstddef>
#include <cstdint>
#include <limits>
#include <string>
#include <utility>
#include <vector>

namespace {

constexpr std::size_t kHeaderSize = 16u;
constexpr std::size_t kListSize = 8u;
constexpr std::size_t kRecordSize = 20u;
constexpr std::size_t kOutputBudgetMultiplier = 8u;

[[noreturn]] void fail(const char* message) {
    throw TxbTextureError(message);
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

std::size_t checked_size(std::uint32_t value, const char* message) {
    const std::size_t result = static_cast<std::size_t>(value);
    if (static_cast<std::uint32_t>(result) != value) {
        fail(message);
    }
    return result;
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

std::string read_name(
    const std::uint8_t* data,
    std::size_t size,
    std::size_t offset,
    OutputBudget& output_budget) {
    if (offset >= size) {
        fail("TXB texture name offset is out of range.");
    }

    std::size_t cursor = offset;
    for (;;) {
        const std::uint8_t value = data[cursor];
        if (value == 0u) {
            if (cursor == offset) {
                fail("TXB texture name is empty.");
            }
            const std::size_t length = cursor - offset;
            output_budget.claim(length, "TXB texture name output is too large.");
            return std::string(reinterpret_cast<const char*>(data + offset), length);
        }
        if (value < 0x20u || value > 0x7eu) {
            fail("TXB texture name is not printable ASCII.");
        }
        if (cursor == size - 1u) {
            break;
        }
        ++cursor;
    }
    fail("TXB texture name is not NUL-terminated.");
}

}

std::vector<TxbTextureData> parse_pc_txb_texture_list(const std::uint8_t* data, std::size_t size) {
    if (data == nullptr) {
        fail("TXB data is null.");
    }
    if (size < kHeaderSize) {
        fail("TXB header is truncated.");
    }
    if (data[0u] != static_cast<std::uint8_t>('#') ||
        data[1u] != static_cast<std::uint8_t>('T') ||
        data[2u] != static_cast<std::uint8_t>('X') ||
        data[3u] != static_cast<std::uint8_t>('B')) {
        fail("TXB magic is unsupported.");
    }

    const std::size_t list_offset = checked_size(read_be32(data, 4u), "TXB list offset is too large.");
    if (list_offset < kHeaderSize) {
        fail("TXB list overlaps its header.");
    }
    require_range(list_offset, kListSize, size, "TXB list is truncated.");
    const std::size_t list_end = checked_sum(list_offset, kListSize, "TXB list layout is too large.");

    const std::size_t count = checked_size(read_be32(data, list_offset), "TXB record count is too large.");
    const std::size_t records_offset = checked_size(
        read_be32(data, list_offset + 4u),
        "TXB record offset is too large.");
    if (count == 0u) {
        if (records_offset != 0u) {
            if (records_offset < list_end) {
                fail("TXB empty record location overlaps its list.");
            }
            require_range(records_offset, 0u, size, "TXB empty record location is out of range.");
        }
        return {};
    }

    if (records_offset < list_end) {
        fail("TXB record table overlaps its list.");
    }
    const std::size_t records_size = checked_product(count, kRecordSize, "TXB record table is too large.");
    require_range(records_offset, records_size, size, "TXB record table is truncated.");
    const std::size_t records_end = checked_sum(records_offset, records_size, "TXB record table is too large.");

    OutputBudget output_budget(checked_product(
        size,
        kOutputBudgetMultiplier,
        "TXB output budget is too large."));
    output_budget.claim(
        checked_product(count, sizeof(TxbTextureData), "TXB texture output is too large."),
        "TXB texture output is too large.");
    std::vector<TxbTextureData> textures;
    textures.reserve(count);
    for (std::size_t index = 0u; index < count; ++index) {
        const std::size_t record_offset = records_offset + index * kRecordSize;
        const std::size_t name_offset = checked_size(
            read_be32(data, record_offset + 4u),
            "TXB texture name offset is too large.");
        if (name_offset < records_end) {
            fail("TXB texture name overlaps its record table.");
        }

        TxbTextureData texture{};
        texture.file_type = read_be32(data, record_offset);
        texture.min_filter = read_be16(data, record_offset + 8u);
        texture.mag_filter = read_be16(data, record_offset + 10u);
        texture.global_index = read_be32(data, record_offset + 12u);
        texture.bank = read_be32(data, record_offset + 16u);
        texture.name = read_name(data, size, name_offset, output_budget);
        textures.push_back(std::move(texture));
    }
    return textures;
}
