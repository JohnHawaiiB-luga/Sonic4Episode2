#pragma once

#include <array>
#include <cstdint>

struct PlayerRectangleBounds {
    std::int16_t left;
    std::int16_t top;
    std::int16_t back;
    std::int16_t right;
    std::int16_t bottom;
    std::int16_t front;
};

struct PlayerRectangleState {
    PlayerRectangleBounds bounds;
    std::array<float, 3> position{};
    std::uint32_t flags;
    std::uint16_t attack_mask;
    std::uint16_t defense_mask;
    std::int16_t attack_power;
    std::int16_t defense_power;
    std::uint8_t group;
    std::uint8_t target_groups;
};

struct PlayerRectangles {
    PlayerRectangleState defense;
    PlayerRectangleState attack;
    PlayerRectangleState interaction;
};

PlayerRectangles make_ordinary_player_rectangles();

void enable_player_attack(PlayerRectangleState& rectangle);
void resize_player_spindash_attack(PlayerRectangleState& rectangle, std::uint32_t player_flags);
void reset_player_attack_for_sequence(PlayerRectangleState& rectangle, std::uint32_t gimmick_flags);
void restore_player_defense(PlayerRectangleState& rectangle, std::uint32_t player_flags);
