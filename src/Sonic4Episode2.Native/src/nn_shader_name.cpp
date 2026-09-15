#include "nn_shader_name.h"

#include <cstddef>

namespace {

constexpr char kBase32Alphabet[] = "0123456789ABCDEFGHIJKLMNOPQRSTUV";
constexpr std::size_t kOutputLength = 26u;
constexpr std::size_t kDigitBitCount = 5u;
constexpr std::size_t kInputBitCount = 128u;
constexpr std::size_t kEncodedBitCount = 130u;

}

std::string nn_shader_archive_basename(const std::array<std::uint8_t, 16u>& key) {
    std::string basename(kOutputLength, '0');
    for (std::size_t digit_index = 0u; digit_index < basename.size(); ++digit_index) {
        std::uint8_t digit = 0u;
        for (std::size_t digit_bit = 0u; digit_bit < kDigitBitCount; ++digit_bit) {
            const std::size_t encoded_bit = kEncodedBitCount - 1u - (digit_index * kDigitBitCount + digit_bit);
            digit = static_cast<std::uint8_t>(digit << 1u);
            if (encoded_bit < kInputBitCount) {
                digit = static_cast<std::uint8_t>(
                    digit | ((key[encoded_bit / 8u] >> (encoded_bit % 8u)) & 1u));
            }
        }
        basename[digit_index] = kBase32Alphabet[digit];
    }
    return basename;
}
