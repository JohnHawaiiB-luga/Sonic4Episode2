#include "death_wait.h"

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <limits>
#include <stdexcept>

namespace {

bool check(bool condition, const char* message) {
    if (condition) {
        return true;
    }
    std::fprintf(stderr, "%s\n", message);
    return false;
}

std::uint32_t float_bits(float value) {
    std::uint32_t bits = 0u;
    std::memcpy(&bits, &value, sizeof(bits));
    return bits;
}

struct StateSnapshot {
    std::uint32_t elapsed_bits;
    std::int32_t remaining_lives;
};

StateSnapshot snapshot(const DeathWaitState& state) {
    return {float_bits(state.elapsed), state.remaining_lives};
}

bool same_state(const StateSnapshot& expected, const DeathWaitState& actual) {
    return expected.elapsed_bits == float_bits(actual.elapsed) &&
           expected.remaining_lives == actual.remaining_lives;
}

bool expects_invalid_without_mutation(
    DeathWaitState& state,
    bool dead,
    bool suppressed,
    float time_step,
    const char* message) {
    const StateSnapshot before = snapshot(state);
    try {
        static_cast<void>(tick_death_wait(state, dead, suppressed, time_step));
    } catch (const std::invalid_argument&) {
        return check(same_state(before, state), message);
    } catch (...) {
        std::fprintf(stderr, "%s (wrong exception type)\n", message);
        return false;
    }

    std::fprintf(stderr, "%s (did not reject)\n", message);
    return false;
}

bool test_waits_119_frames_then_crosses_on_the_120th() {
    DeathWaitState state{0.0f, 2};
    for (std::int32_t frame = 1; frame < 120; ++frame) {
        if (!check(
                tick_death_wait(state, true, false) == DeathWaitAction::None,
                "pre-duration tick returned an action") ||
            !check(
                state.elapsed == static_cast<float>(frame) && state.remaining_lives == 2,
                "pre-duration tick changed the state incorrectly")) {
            return false;
        }
    }

    return check(
               tick_death_wait(state, true, false) == DeathWaitAction::Restart,
               "120th tick did not request a retry") &&
           check(state.elapsed == kDeathWaitDuration, "120th tick duration mismatch") &&
           check(state.remaining_lives == 1, "120th tick life decrement mismatch");
}

bool test_pc_clamp_preserves_negative_zero() {
    DeathWaitState state{-0.0f, 3};
    return check(tick_death_wait(state, true, false, -0.0f) == DeathWaitAction::None,
                 "zero time advance returned an action") &&
           check(float_bits(state.elapsed) == 0x80000000u,
                 "PC equal clamp comparison changed the sign of zero");
}

bool test_fractional_overshoot_and_repeated_tick() {
    DeathWaitState state{119.5f, 1};

    if (!check(
            tick_death_wait(state, true, false, 0.75f) == DeathWaitAction::Restart,
            "fractional crossing did not request a retry") ||
        !check(
            state.elapsed == 120.25f && state.remaining_lives == 0,
            "fractional crossing did not retain the overshoot")) {
        return false;
    }

    const StateSnapshot before_repeat = snapshot(state);
    return check(
               tick_death_wait(state, true, false, 2.0f) == DeathWaitAction::None,
               "completed wait returned an action again") &&
           check(same_state(before_repeat, state), "completed wait decremented twice");
}

bool test_alive_suppressed_and_completed_states_are_unchanged() {
    DeathWaitState alive{17.0f, 3};
    const StateSnapshot alive_before = snapshot(alive);
    if (!check(
            tick_death_wait(alive, false, false, 2.0f) == DeathWaitAction::None,
            "alive state returned an action") ||
        !check(same_state(alive_before, alive), "alive state changed")) {
        return false;
    }

    DeathWaitState suppressed{17.0f, 3};
    const StateSnapshot suppressed_before = snapshot(suppressed);
    if (!check(
            tick_death_wait(suppressed, true, true, 2.0f) == DeathWaitAction::None,
            "suppressed state returned an action") ||
        !check(same_state(suppressed_before, suppressed), "suppressed state changed")) {
        return false;
    }

    DeathWaitState completed{kDeathWaitDuration, 0};
    const StateSnapshot completed_before = snapshot(completed);
    if (!check(
            tick_death_wait(completed, true, false, 2.0f) == DeathWaitAction::None,
            "completed state returned an action") ||
        !check(same_state(completed_before, completed), "completed state changed")) {
        return false;
    }

    DeathWaitState completed_maximum{std::numeric_limits<float>::max(), 0};
    const StateSnapshot completed_maximum_before = snapshot(completed_maximum);
    return check(
               tick_death_wait(
                   completed_maximum,
                   false,
                   false,
                   std::numeric_limits<float>::max()) == DeathWaitAction::None,
               "completed maximum state returned an action") &&
           check(
               same_state(completed_maximum_before, completed_maximum),
               "completed maximum state evaluated the time step");
}

bool test_zero_life_retries_once_then_game_over() {
    DeathWaitState retry{119.0f, 1};
    if (!check(
            tick_death_wait(retry, true, false) == DeathWaitAction::Restart,
            "life count zero did not request retry") ||
        !check(retry.elapsed == kDeathWaitDuration && retry.remaining_lives == 0,
               "life count one crossing mismatch")) {
        return false;
    }

    DeathWaitState terminal{119.0f, 0};
    if (!check(
            tick_death_wait(terminal, true, false) == DeathWaitAction::GameOver,
            "life count zero did not request game over") ||
        !check(terminal.elapsed == kDeathWaitDuration && terminal.remaining_lives == -1,
               "terminal crossing mismatch")) {
        return false;
    }

    const StateSnapshot terminal_before = snapshot(terminal);
    return check(
               tick_death_wait(terminal, true, false) == DeathWaitAction::None,
               "terminal state returned an action twice") &&
           check(same_state(terminal_before, terminal), "terminal state changed after game over");
}

bool test_terminal_state_is_permitted_only_after_completion() {
    DeathWaitState completed{kDeathWaitDuration, -1};
    const StateSnapshot completed_before = snapshot(completed);
    if (!check(
            tick_death_wait(completed, true, false) == DeathWaitAction::None,
            "completed terminal state was rejected") ||
        !check(same_state(completed_before, completed), "completed terminal state changed")) {
        return false;
    }

    DeathWaitState early_terminal{119.0f, -1};
    return expects_invalid_without_mutation(
        early_terminal,
        false,
        false,
        1.0f,
        "early terminal state bypassed validation while alive");
}

bool test_invalid_inputs_reject_without_mutation() {
    DeathWaitState negative_elapsed{-0.25f, 1};
    DeathWaitState infinite_elapsed{std::numeric_limits<float>::infinity(), 1};
    DeathWaitState nan_elapsed{std::numeric_limits<float>::quiet_NaN(), 1};
    DeathWaitState negative_lives{1.0f, -2};
    DeathWaitState negative_step{1.0f, 1};
    DeathWaitState infinite_step{1.0f, 1};
    DeathWaitState nan_step{1.0f, 1};

    return expects_invalid_without_mutation(
               negative_elapsed, false, false, 1.0f, "negative elapsed was accepted") &&
           expects_invalid_without_mutation(
               infinite_elapsed, false, false, 1.0f, "infinite elapsed was accepted") &&
           expects_invalid_without_mutation(
               nan_elapsed, false, false, 1.0f, "NaN elapsed was accepted") &&
           expects_invalid_without_mutation(
               negative_lives, false, false, 1.0f, "life count below terminal was accepted") &&
           expects_invalid_without_mutation(
               negative_step, false, false, -0.25f, "negative time step was accepted") &&
           expects_invalid_without_mutation(
               infinite_step,
               true,
               true,
               std::numeric_limits<float>::infinity(),
               "infinite time step bypassed suppression validation") &&
           expects_invalid_without_mutation(
               nan_step,
               false,
               false,
               std::numeric_limits<float>::quiet_NaN(),
               "NaN time step bypassed alive validation");
}

}

int main() {
    if (!test_pc_clamp_preserves_negative_zero() ||
        !test_waits_119_frames_then_crosses_on_the_120th() ||
        !test_fractional_overshoot_and_repeated_tick() ||
        !test_alive_suppressed_and_completed_states_are_unchanged() ||
        !test_zero_life_retries_once_then_game_over() ||
        !test_terminal_state_is_permitted_only_after_completion() ||
        !test_invalid_inputs_reject_without_mutation()) {
        return 1;
    }

    std::printf("death wait tests: 6 groups, 119 ordinary ticks\n");
    return 0;
}
