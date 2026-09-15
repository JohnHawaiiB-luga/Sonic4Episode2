#include "nn_shader_name.h"

#include <array>
#include <cstdint>
#include <cstdio>
#include <string>

namespace {

constexpr char kAlphabet[] = "0123456789ABCDEFGHIJKLMNOPQRSTUV";

bool check(bool condition, const char* message) {
    if (condition) {
        return true;
    }
    std::fprintf(stderr, "%s\n", message);
    return false;
}

bool check_case(
    const std::array<std::uint8_t, 16u>& key,
    const char* expected,
    const char* message) {
    const std::string actual = nn_shader_archive_basename(key);
    return check(actual == expected, message) &&
           check(actual.size() == 26u, "shader archive basename length mismatch") &&
           check(actual.find_first_not_of(kAlphabet) == std::string::npos, "shader archive basename alphabet mismatch");
}

bool test_zero_and_all_ones() {
    const std::array<std::uint8_t, 16u> zero{};
    const std::array<std::uint8_t, 16u> all_ones = {{
        0xffu, 0xffu, 0xffu, 0xffu,
        0xffu, 0xffu, 0xffu, 0xffu,
        0xffu, 0xffu, 0xffu, 0xffu,
        0xffu, 0xffu, 0xffu, 0xffu,
    }};
    return check_case(zero, "00000000000000000000000000", "zero shader archive basename mismatch") &&
           check_case(all_ones, "7VVVVVVVVVVVVVVVVVVVVVVVVV", "all-ones shader archive basename mismatch");
}

bool test_little_endian_bit_boundaries() {
    const std::array<std::uint8_t, 16u> low_one = {{
        0x01u, 0x00u, 0x00u, 0x00u,
        0x00u, 0x00u, 0x00u, 0x00u,
        0x00u, 0x00u, 0x00u, 0x00u,
        0x00u, 0x00u, 0x00u, 0x00u,
    }};
    const std::array<std::uint8_t, 16u> low_group_boundary = {{
        0x20u, 0x00u, 0x00u, 0x00u,
        0x00u, 0x00u, 0x00u, 0x00u,
        0x00u, 0x00u, 0x00u, 0x00u,
        0x00u, 0x00u, 0x00u, 0x00u,
    }};
    const std::array<std::uint8_t, 16u> byte_crossing = {{
        0x80u, 0x01u, 0x00u, 0x00u,
        0x00u, 0x00u, 0x00u, 0x00u,
        0x00u, 0x00u, 0x00u, 0x00u,
        0x00u, 0x00u, 0x00u, 0x00u,
    }};
    const std::array<std::uint8_t, 16u> top_bit = {{
        0x00u, 0x00u, 0x00u, 0x00u,
        0x00u, 0x00u, 0x00u, 0x00u,
        0x00u, 0x00u, 0x00u, 0x00u,
        0x00u, 0x00u, 0x00u, 0x80u,
    }};
    const std::array<std::uint8_t, 16u> top_low_bit = {{
        0x00u, 0x00u, 0x00u, 0x00u,
        0x00u, 0x00u, 0x00u, 0x00u,
        0x00u, 0x00u, 0x00u, 0x00u,
        0x00u, 0x00u, 0x00u, 0x01u,
    }};

    return check_case(low_one, "00000000000000000000000001", "low-bit little-endian basename mismatch") &&
           check_case(low_group_boundary, "00000000000000000000000010", "five-bit boundary basename mismatch") &&
           check_case(byte_crossing, "000000000000000000000000C0", "byte-crossing basename mismatch") &&
           check_case(top_bit, "40000000000000000000000000", "top-bit basename mismatch") &&
           check_case(top_low_bit, "01000000000000000000000000", "top-byte little-endian basename mismatch");
}

bool test_mixed_pattern() {
    const std::array<std::uint8_t, 16u> key = {{
        0x00u, 0x01u, 0x02u, 0x03u,
        0x04u, 0x05u, 0x06u, 0x07u,
        0x08u, 0x09u, 0x0au, 0x0bu,
        0x0cu, 0x0du, 0x0eu, 0x0fu,
    }};
    return check_case(key, "0F1O6GO2OA1440E1G50G1G4080", "mixed shader archive basename mismatch");
}

} // namespace

int main() {
    return test_zero_and_all_ones() &&
                   test_little_endian_bit_boundaries() &&
                   test_mixed_pattern()
               ? 0
               : 1;
}
