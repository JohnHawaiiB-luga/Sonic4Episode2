#include "death_wait.h"

#include <cmath>
#include <stdexcept>

namespace {

void validate_state(const DeathWaitState& state, float time_step) {
    if (!std::isfinite(state.elapsed) || state.elapsed < 0.0f ||
        !std::isfinite(time_step) || time_step < 0.0f ||
        state.remaining_lives < -1 ||
        (state.remaining_lives == -1 && state.elapsed < kDeathWaitDuration)) {
        throw std::invalid_argument("invalid death wait state");
    }
}

}

DeathWaitAction tick_death_wait(
    DeathWaitState& state,
    bool dead,
    bool suppressed,
    float time_step) {
    validate_state(state, time_step);

    if (!dead || suppressed || state.elapsed >= kDeathWaitDuration) {
        return DeathWaitAction::None;
    }

    const float elapsed = state.elapsed + time_step;
    if (!std::isfinite(elapsed)) {
        throw std::invalid_argument("death wait elapsed overflow");
    }

    state.elapsed = elapsed;
    if (state.elapsed < kDeathWaitDuration) {
        return DeathWaitAction::None;
    }

    --state.remaining_lives;
    return state.remaining_lives >= 0 ? DeathWaitAction::Restart : DeathWaitAction::GameOver;
}
