#pragma once

#include <stdexcept>

enum class SonicSoundCue {
    Jump,
    Spin,
    Dash1,
    Dash2,
    Ring1L,
    Ring1R,
};

inline const char* sonic_sound_cue_name(SonicSoundCue cue) {
    switch (cue) {
    case SonicSoundCue::Jump: return "Jump";
    case SonicSoundCue::Spin: return "Spin";
    case SonicSoundCue::Dash1: return "Dash1";
    case SonicSoundCue::Dash2: return "Dash2";
    case SonicSoundCue::Ring1L: return "Ring1L";
    case SonicSoundCue::Ring1R: return "Ring1R";
    }
    throw std::invalid_argument("Sonic sound cue is unsupported.");
}
