#include "mt_math_rand.h"

#include <cstdint>
#include <iostream>
#include <limits>
#include <sstream>
#include <string>
#include <string_view>

namespace {

constexpr std::uint32_t kMaximumStepCount = 1000000u;

bool parse_unsigned_decimal(std::string_view text, std::uint64_t& value) {
    if (text.empty()) {
        return false;
    }

    std::uint64_t parsed = 0u;
    for (const unsigned char character : text) {
        if (character < '0' || character > '9') {
            return false;
        }

        const std::uint64_t digit = static_cast<std::uint64_t>(character - '0');
        if (parsed > (std::numeric_limits<std::uint64_t>::max() - digit) / 10u) {
            return false;
        }
        parsed = parsed * 10u + digit;
    }

    value = parsed;
    return true;
}

bool parse_request(const std::string& line, std::uint32_t& seed, std::uint32_t& step_count) {
    std::istringstream input(line);
    std::string seed_text;
    std::string step_count_text;
    std::string unexpected_text;
    if (!(input >> seed_text >> step_count_text) || (input >> unexpected_text)) {
        return false;
    }

    std::uint64_t parsed_seed = 0u;
    std::uint64_t parsed_step_count = 0u;
    if (!parse_unsigned_decimal(seed_text, parsed_seed) ||
        !parse_unsigned_decimal(step_count_text, parsed_step_count) ||
        parsed_seed > std::numeric_limits<std::uint32_t>::max() ||
        parsed_step_count > kMaximumStepCount) {
        return false;
    }

    seed = static_cast<std::uint32_t>(parsed_seed);
    step_count = static_cast<std::uint32_t>(parsed_step_count);
    return true;
}

}

int main() {
    std::string line;
    std::uint64_t line_number = 0u;
    while (std::getline(std::cin, line)) {
        ++line_number;

        std::uint32_t seed = 0u;
        std::uint32_t step_count = 0u;
        if (!parse_request(line, seed, step_count)) {
            std::cerr << "invalid request on line " << line_number
                      << "; expected unsigned-decimal seed and step_count up to "
                      << kMaximumStepCount << '\n';
            return 2;
        }

        std::uint32_t state = seed;
        for (std::uint32_t step = 0u; step < step_count; ++step) {
            const std::uint16_t returned_value = mt_math_rand(state);
            std::cout << state << ' ' << returned_value << '\n';
        }
    }

    return std::cin.bad() ? 3 : 0;
}
