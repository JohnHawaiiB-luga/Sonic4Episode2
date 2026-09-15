#pragma once

#include <cstdint>

constexpr float kDeathWaitDuration = 120.0f;

struct DeathWaitState {
    float elapsed;
    std::int32_t remaining_lives;
};

enum class DeathWaitAction {
    None,
    Restart,
    GameOver,
};

DeathWaitAction tick_death_wait(
    DeathWaitState& state,
    bool dead,
    bool suppressed,
    float time_step = 1.0f);
