#include "player_rectangles.h"

namespace {

constexpr PlayerRectangleBounds kDefenseBounds{-8, -19, -500, 8, 13, 500};
constexpr PlayerRectangleBounds kAttackBounds{-16, -19, -500, 16, 13, 500};
constexpr PlayerRectangleBounds kSpindashAttackBounds{-32, -51, -500, 32, 13, 500};

void reset_position(PlayerRectangleState& rectangle) {
    rectangle.position = {};
}

}

PlayerRectangles make_ordinary_player_rectangles() {
    return {
        {kDefenseBounds, {}, 0x00200084u, 0u, 0xfffdu, 2, 1, 0u, 0x44u},
        {kAttackBounds, {}, 0x00200020u, 6u, 0xffffu, 2, 1, 0u, 0x44u},
        {kDefenseBounds, {}, 0x002000e4u, 1u, 0xfffeu, 2, 1, 0u, 0x44u},
    };
}

void enable_player_attack(PlayerRectangleState& rectangle) {
    rectangle.flags |= 0x4u;
    if ((rectangle.flags & 0x400u) == 0u) {
        rectangle.flags &= ~0x300u;
    }
    rectangle.bounds = kAttackBounds;
    reset_position(rectangle);
}

void resize_player_spindash_attack(PlayerRectangleState& rectangle, std::uint32_t player_flags) {
    rectangle.flags |= 0x4u;
    rectangle.bounds = (player_flags & 0x4000u) != 0u ? kSpindashAttackBounds : kAttackBounds;
    reset_position(rectangle);
}

void reset_player_attack_for_sequence(PlayerRectangleState& rectangle, std::uint32_t gimmick_flags) {
    if ((gimmick_flags & 0x02000000u) == 0u) {
        rectangle.flags &= ~0x4u;
    }
}

void restore_player_defense(PlayerRectangleState& rectangle, std::uint32_t player_flags) {
    rectangle.defense_power = static_cast<std::int16_t>((player_flags & 0x4000u) != 0u ? 4 : 1);
}
