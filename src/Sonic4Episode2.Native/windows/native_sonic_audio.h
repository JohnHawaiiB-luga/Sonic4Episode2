#pragma once

#include "sonic_sound.h"

#include <filesystem>
#include <memory>
#include <string>

namespace sonic4ep2::app {

class NativeSonicAudio final {
public:
    explicit NativeSonicAudio(const std::filesystem::path& data_root, bool muted = false);
    ~NativeSonicAudio();
    NativeSonicAudio(const NativeSonicAudio&) = delete;
    NativeSonicAudio& operator=(const NativeSonicAudio&) = delete;

    void start_stage_music();
    void play(SonicSoundCue cue);
    void set_paused(bool paused);
    void update();
    std::string report_json() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}
