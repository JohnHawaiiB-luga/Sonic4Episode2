#include "damage_rings.h"

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <limits>
#include <sstream>
#include <string>
#include <string_view>

namespace {

constexpr std::size_t kInputFieldCount = 12u;
constexpr std::uint32_t kSentinelBits = 0xCDCDCDCDu;

enum InputField : std::size_t {
    kSeed = 0u,
    kEnabled = 1u,
    kCursor = 2u,
    kNullSlot = 3u,
    kExistingCount = 4u,
    kPositionXBits = 5u,
    kPositionYBits = 6u,
    kPositionZBits = 7u,
    kVelocityXBits = 8u,
    kVelocityYBits = 9u,
    kParameterA = 10u,
    kParameterB = 11u,
};

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

bool parse_input_line(const std::string& line, std::uint32_t values[kInputFieldCount]) {
    std::istringstream input(line);
    std::string token;
    for (std::size_t index = 0u; index < kInputFieldCount; ++index) {
        if (!(input >> token)) {
            return false;
        }

        std::uint64_t parsed = 0u;
        if (!parse_unsigned_decimal(token, parsed) ||
            parsed > std::numeric_limits<std::uint32_t>::max()) {
            return false;
        }
        values[index] = static_cast<std::uint32_t>(parsed);
    }

    return !(input >> token);
}

bool has_finite_float_bits(std::uint32_t bits) {
    return (bits & 0x7F800000u) != 0x7F800000u;
}

bool has_valid_values(const std::uint32_t values[kInputFieldCount]) {
    if (values[kEnabled] > 1u || values[kNullSlot] > 1u || values[kCursor] > 97u ||
        values[kExistingCount] > std::min(values[kCursor], static_cast<std::uint32_t>(kDamageRingSlotCount)) ||
        values[kParameterA] > std::numeric_limits<std::uint16_t>::max() ||
        values[kParameterB] > std::numeric_limits<std::uint16_t>::max()) {
        return false;
    }

    return has_finite_float_bits(values[kPositionXBits]) &&
           has_finite_float_bits(values[kPositionYBits]) &&
           has_finite_float_bits(values[kPositionZBits]) &&
           has_finite_float_bits(values[kVelocityXBits]) &&
           has_finite_float_bits(values[kVelocityYBits]);
}

float float_from_bits(std::uint32_t bits) {
    float value = 0.0f;
    std::memcpy(&value, &bits, sizeof(value));
    return value;
}

std::uint32_t float_bits(float value) {
    std::uint32_t bits = 0u;
    std::memcpy(&bits, &value, sizeof(bits));
    return bits;
}

void initialize_ring(DamageRing& ring) {
    for (float& value : ring.position) {
        value = float_from_bits(kSentinelBits);
    }
    for (float& value : ring.velocity) {
        value = float_from_bits(kSentinelBits);
    }
    for (float& value : ring.scale) {
        value = float_from_bits(kSentinelBits);
    }
    ring.timer = static_cast<std::uint16_t>(kSentinelBits);
    ring.parameter_a = 0xCDCDu;
    ring.parameter_b = 0xCDCDu;
    ring.auxiliary_a = kSentinelBits;
    ring.auxiliary_b = kSentinelBits;
    for (float& value : ring.modulation) {
        value = float_from_bits(kSentinelBits);
    }
    ring.previous = nullptr;
    ring.next = nullptr;
}

int handle_index(const DamageRing* handle, const DamageRing rings[kDamageRingSlotCount]) {
    if (handle == nullptr) {
        return -1;
    }

    for (std::size_t index = 0u; index < kDamageRingSlotCount; ++index) {
        if (handle == &rings[index]) {
            return static_cast<int>(index);
        }
    }
    return -1;
}

DamageRingCreateArgs make_args(const std::uint32_t values[kInputFieldCount]) {
    DamageRingCreateArgs args{};
    args.position[0] = float_from_bits(values[kPositionXBits]);
    args.position[1] = float_from_bits(values[kPositionYBits]);
    args.position[2] = float_from_bits(values[kPositionZBits]);
    args.velocity[0] = float_from_bits(values[kVelocityXBits]);
    args.velocity[1] = float_from_bits(values[kVelocityYBits]);
    args.parameter_a = static_cast<std::uint16_t>(values[kParameterA]);
    args.parameter_b = static_cast<std::uint16_t>(values[kParameterB]);
    return args;
}

void write_ring_json(const DamageRing& ring, const DamageRing rings[kDamageRingSlotCount]) {
    std::cout << "{\"position_bits\":[" << float_bits(ring.position[0]) << ','
              << float_bits(ring.position[1]) << ',' << float_bits(ring.position[2])
              << "],\"velocity_bits\":[" << float_bits(ring.velocity[0]) << ','
              << float_bits(ring.velocity[1]) << "],\"scale_bits\":[" << float_bits(ring.scale[0])
              << ',' << float_bits(ring.scale[1]) << ',' << float_bits(ring.scale[2])
              << "],\"timer\":" << ring.timer << ",\"parameter_a\":"
              << static_cast<std::uint32_t>(ring.parameter_a) << ",\"parameter_b\":"
              << static_cast<std::uint32_t>(ring.parameter_b) << ",\"auxiliary_a\":"
              << ring.auxiliary_a << ",\"auxiliary_b\":" << ring.auxiliary_b
              << ",\"modulation_bits\":[" << float_bits(ring.modulation[0]) << ','
              << float_bits(ring.modulation[1]) << ',' << float_bits(ring.modulation[2]) << ','
              << float_bits(ring.modulation[3]) << "],\"previous\":"
              << handle_index(ring.previous, rings) << ",\"next\":" << handle_index(ring.next, rings)
              << '}';
}

void write_result_json(
    DamageRing* returned,
    std::uint32_t seed,
    const DamageRingSystem& system,
    DamageRing* previous_tail,
    const DamageRing rings[kDamageRingSlotCount]) {
    std::cout << "{\"returned_index\":" << handle_index(returned, rings) << ",\"seed\":" << seed
              << ",\"cursor\":" << system.allocation_cursor << ",\"head\":"
              << handle_index(system.damage_head, rings) << ",\"tail\":"
              << handle_index(system.damage_tail, rings) << ",\"previous_tail_next\":";
    if (previous_tail == nullptr) {
        std::cout << -1;
    }
    else {
        std::cout << handle_index(previous_tail->next, rings);
    }
    std::cout << ",\"ring\":";
    if (returned == nullptr) {
        std::cout << "null";
    }
    else {
        write_ring_json(*returned, rings);
    }
    std::cout << "}\n";
}

void execute_request(const std::uint32_t values[kInputFieldCount]) {
    DamageRing rings[kDamageRingSlotCount]{};
    DamageRingSystem system{};
    for (std::size_t index = 0u; index < kDamageRingSlotCount; ++index) {
        initialize_ring(rings[index]);
        system.slots[index] = &rings[index];
    }

    system.allocation_cursor = values[kCursor];
    if (values[kNullSlot] != 0u && system.allocation_cursor < kDamageRingSlotCount) {
        system.slots[system.allocation_cursor] = nullptr;
    }

    const std::uint32_t existing_count = values[kExistingCount];
    if (existing_count != 0u) {
        for (std::uint32_t index = 0u; index < existing_count; ++index) {
            rings[index].previous = index == 0u ? nullptr : &rings[index - 1u];
            rings[index].next = index + 1u == existing_count ? nullptr : &rings[index + 1u];
        }
        system.damage_head = &rings[0];
        system.damage_tail = &rings[existing_count - 1u];
    }

    std::uint32_t seed = values[kSeed];
    const DamageRingCreateArgs args = make_args(values);
    DamageRing* previous_tail = system.damage_tail;
    DamageRing* returned = create_damage_ring(
        seed,
        values[kEnabled] != 0u ? &system : nullptr,
        args);
    write_result_json(returned, seed, system, previous_tail, rings);
}

}

int main() {
    std::string line;
    std::uint64_t line_number = 0u;
    while (std::getline(std::cin, line)) {
        ++line_number;
        std::uint32_t values[kInputFieldCount]{};
        if (!parse_input_line(line, values) || !has_valid_values(values)) {
            std::cerr << "invalid request on line " << line_number << '\n';
            return 2;
        }
        execute_request(values);
    }

    return std::cin.bad() ? 3 : 0;
}
