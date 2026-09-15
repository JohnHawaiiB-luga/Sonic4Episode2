#pragma once

#include <cstdint>

struct PlayerRingCounters {
    std::uint16_t carried = 0u;
    std::uint16_t total = 0u;
    std::uint16_t next_extra_life = 100u;
};

std::uint16_t add_player_rings(
    PlayerRingCounters& counters,
    std::int16_t amount,
    std::uint32_t game_mode,
    bool special_stage);
