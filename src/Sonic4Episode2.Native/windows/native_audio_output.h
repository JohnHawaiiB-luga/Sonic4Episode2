#pragma once

#include "adx_audio.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

namespace sonic4ep2::app {

struct NativeAudioClip {
    std::vector<AdxAudioData> streams;
    bool loop_last_stream = false;
};

struct NativeAudioPlaybackState {
    std::uint32_t queued_buffers = 0u;
    std::uint64_t samples_played = 0u;
};

struct NativeAudioOutputState {
    std::uint64_t submitted = 0u;
    std::uint64_t completed = 0u;
    std::uint64_t stopped = 0u;
    std::size_t active = 0u;
    bool paused = false;
    bool muted = false;
};

class NativeAudioOutput final {
public:
    explicit NativeAudioOutput(bool muted = false);
    ~NativeAudioOutput();
    NativeAudioOutput(const NativeAudioOutput&) = delete;
    NativeAudioOutput& operator=(const NativeAudioOutput&) = delete;

    std::uint64_t play(std::shared_ptr<const NativeAudioClip> clip, float gain);
    std::uint64_t play_front_stereo(
        std::shared_ptr<const NativeAudioClip> clip,
        float gain,
        const std::array<float, 2u>& channel_gains);
    bool stop(std::uint64_t playback_id);
    void stop_all();
    void set_paused(bool paused);
    void update();
    NativeAudioPlaybackState playback_state(std::uint64_t playback_id) const;
    NativeAudioOutputState state() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}
