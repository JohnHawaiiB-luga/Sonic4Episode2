#include "player_ring_counters.h"

namespace {

std::uint16_t add_wrapped_and_clamped(
    std::uint16_t value,
    std::int16_t amount,
    std::uint16_t maximum) {
    const std::int32_t signed_amount = amount;
    const std::uint32_t amount_bits = signed_amount < 0
        ? static_cast<std::uint32_t>(signed_amount + 0x10000)
        : static_cast<std::uint32_t>(signed_amount);
    const std::uint16_t wrapped = static_cast<std::uint16_t>(
        (static_cast<std::uint32_t>(value) + amount_bits) & 0xffffu);
    std::int32_t result = wrapped;
    if ((wrapped & 0x8000u) != 0u) {
        result -= 0x10000;
    }
    if (result < 0) {
        return 0u;
    }
    if (result > maximum) {
        return maximum;
    }
    return static_cast<std::uint16_t>(result);
}

}

std::uint16_t add_player_rings(
    PlayerRingCounters& counters,
    std::int16_t amount,
    std::uint32_t game_mode,
    bool special_stage) {
    const std::uint16_t old_carried = counters.carried;
    counters.carried = add_wrapped_and_clamped(counters.carried, amount, 999u);
    counters.total = add_wrapped_and_clamped(counters.total, amount, 9999u);

    if (game_mode == 1u) {
        return 0u;
    }
    if (special_stage) {
        return old_carried < 50u && counters.carried >= 50u ? 1u : 0u;
    }

    std::uint16_t awards = 0u;
    for (std::uint16_t mark = counters.next_extra_life; mark <= 900u;
         mark = static_cast<std::uint16_t>(mark + 100u)) {
        if (old_carried < mark && counters.carried >= mark) {
            ++awards;
            counters.next_extra_life = static_cast<std::uint16_t>(counters.next_extra_life + 100u);
        }
    }
    return awards;
}
