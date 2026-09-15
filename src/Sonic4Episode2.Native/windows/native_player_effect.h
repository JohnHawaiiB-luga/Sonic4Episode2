#pragma once

#include "ame_runtime.h"

#include <cstdint>
#include <vector>

namespace sonic4ep2::app {

enum class NativePlayerEffectKind {
    Ring,
    JumpDash,
};

struct NativePlayerEffectFrame {
    NativePlayerEffectKind kind;
    std::uint64_t id;
    std::vector<AmeRuntimeSprite> sprites;
    std::vector<AmeRuntimeLine> lines;
};

}
