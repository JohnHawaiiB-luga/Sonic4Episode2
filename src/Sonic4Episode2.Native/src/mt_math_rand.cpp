#include "mt_math_rand.h"

std::uint16_t mt_math_rand(std::uint32_t& state) {
    state = state * 0x00196225u + 0x3C6EF35Fu;
    return static_cast<std::uint16_t>(state >> 16u);
}
