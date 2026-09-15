#include "mt_math_rand.h"

#include <cstdint>
#include <cstdio>

namespace {

struct StepExpectation {
    std::uint32_t input_state;
    std::uint32_t output_state;
    std::uint16_t returned_value;
};

bool check_step(const StepExpectation& expected) {
    std::uint32_t state = expected.input_state;
    const std::uint16_t returned_value = mt_math_rand(state);
    if (state == expected.output_state && returned_value == expected.returned_value) {
        return true;
    }

    std::fprintf(
        stderr,
        "input=%u expected=(%u,%u) actual=(%u,%u)\n",
        expected.input_state,
        expected.output_state,
        static_cast<unsigned>(expected.returned_value),
        state,
        static_cast<unsigned>(returned_value));
    return false;
}

}

int main() {
    const StepExpectation cases[] = {
        {0u, 1013904223u, 15470u},
        {1u, 1015567748u, 15496u},
        {4294967295u, 1012240698u, 15445u},
        {305419896u, 1490117303u, 22737u},
    };

    for (const StepExpectation& expected : cases) {
        if (!check_step(expected)) {
            return 1;
        }
    }

    std::uint32_t state = 0u;
    const std::uint16_t first = mt_math_rand(state);
    const std::uint16_t second = mt_math_rand(state);
    const std::uint16_t third = mt_math_rand(state);
    if (state != 3120439585u || first != 15470u || second != 13801u || third != 47614u) {
        std::fprintf(stderr, "three-step sequence did not match expected state and returns\n");
        return 1;
    }

    return 0;
}
