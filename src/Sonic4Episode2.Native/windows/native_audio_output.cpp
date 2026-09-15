#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <objbase.h>
#include <xaudio2.h>

#include "native_audio_output.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <utility>

namespace sonic4ep2::app {
namespace {

void check_audio(HRESULT result, const char* operation) {
    if (FAILED(result)) {
        std::ostringstream message;
        message << operation << " failed (0x" << std::hex
            << static_cast<std::uint32_t>(result) << ").";
        throw std::runtime_error(message.str());
    }
}

WAVEFORMATEX validate_clip(const NativeAudioClip& clip) {
    if (clip.streams.empty() || clip.streams.size() > XAUDIO2_MAX_QUEUED_BUFFERS) {
        throw std::invalid_argument("Native audio clip stream count is unsupported.");
    }
    const auto& first = clip.streams.front();
    if ((first.channels != 1u && first.channels != 2u)
            || first.sample_rate < XAUDIO2_MIN_SAMPLE_RATE
            || first.sample_rate > XAUDIO2_MAX_SAMPLE_RATE) {
        throw std::invalid_argument("Native audio clip format is unsupported.");
    }
    for (const auto& stream : clip.streams) {
        const auto samples = static_cast<std::uint64_t>(stream.sample_count) * stream.channels;
        if (stream.channels != first.channels || stream.sample_rate != first.sample_rate
                || stream.sample_count == 0u || samples != stream.samples.size()
                || samples * sizeof(std::int16_t) > XAUDIO2_MAX_BUFFER_BYTES) {
            throw std::invalid_argument("Native audio clip stream is inconsistent.");
        }
    }
    WAVEFORMATEX format{};
    format.wFormatTag = WAVE_FORMAT_PCM;
    format.nChannels = static_cast<WORD>(first.channels);
    format.nSamplesPerSec = first.sample_rate;
    format.wBitsPerSample = 16u;
    format.nBlockAlign = static_cast<WORD>(first.channels * sizeof(std::int16_t));
    format.nAvgBytesPerSec = first.sample_rate * format.nBlockAlign;
    return format;
}

struct ActiveVoice {
    std::shared_ptr<const NativeAudioClip> clip;
    IXAudio2SourceVoice* voice = nullptr;
    std::uint64_t id = 0u;

    ~ActiveVoice() {
        if (voice != nullptr) voice->DestroyVoice();
    }
};

}

struct NativeAudioOutput::Impl {
    IXAudio2* engine = nullptr;
    IXAudio2MasteringVoice* master = nullptr;
    std::vector<std::unique_ptr<ActiveVoice>> voices;
    NativeAudioOutputState counters;
    std::uint64_t next_id = 1u;
    bool com_initialized = false;

    std::uint64_t play_clip(
        std::shared_ptr<const NativeAudioClip> clip,
        float gain,
        const std::array<float, 2u>* channel_gains);

    explicit Impl(bool muted) {
        check_audio(CoInitializeEx(nullptr, COINIT_MULTITHREADED), "Audio COM initialization");
        com_initialized = true;
        try {
            check_audio(XAudio2Create(&engine, 0u, XAUDIO2_DEFAULT_PROCESSOR), "XAudio2 creation");
            check_audio(engine->CreateMasteringVoice(&master), "Audio output creation");
            check_audio(master->SetVolume(muted ? 0.0f : 1.0f), "Audio output volume");
            counters.muted = muted;
        } catch (...) {
            release();
            throw;
        }
    }

    ~Impl() { release(); }

    void release() noexcept {
        voices.clear();
        if (master != nullptr) {
            master->DestroyVoice();
            master = nullptr;
        }
        if (engine != nullptr) {
            engine->Release();
            engine = nullptr;
        }
        if (com_initialized) {
            CoUninitialize();
            com_initialized = false;
        }
    }
};

NativeAudioOutput::NativeAudioOutput(bool muted) : impl_(std::make_unique<Impl>(muted)) {}
NativeAudioOutput::~NativeAudioOutput() = default;

std::uint64_t NativeAudioOutput::Impl::play_clip(
    std::shared_ptr<const NativeAudioClip> clip,
    float gain,
    const std::array<float, 2u>* channel_gains) {
    if (!clip || !std::isfinite(gain) || gain < 0.0f || gain > XAUDIO2_MAX_VOLUME_LEVEL) {
        throw std::invalid_argument("Native audio clip or gain is invalid.");
    }
    const auto format = validate_clip(*clip);
    std::array<float, 8u> output_matrix{};
    UINT32 output_channels = 0u;
    if (channel_gains != nullptr) {
        if (format.nChannels != 1u) {
            throw std::invalid_argument("Native front stereo playback requires a mono clip.");
        }
        for (const float channel_gain : *channel_gains) {
            if (!std::isfinite(channel_gain) || channel_gain < 0.0f || channel_gain > 1.0f) {
                throw std::invalid_argument("Native front stereo channel gain is invalid.");
            }
        }

        XAUDIO2_VOICE_DETAILS details{};
        master->GetVoiceDetails(&details);
        DWORD channel_mask = 0u;
        check_audio(master->GetChannelMask(&channel_mask), "Audio output channel mask");
        constexpr DWORD front_stereo_mask = SPEAKER_FRONT_LEFT | SPEAKER_FRONT_RIGHT;
        constexpr DWORD surround_mask = front_stereo_mask | SPEAKER_FRONT_CENTER
            | SPEAKER_LOW_FREQUENCY | SPEAKER_BACK_LEFT | SPEAKER_BACK_RIGHT
            | SPEAKER_SIDE_LEFT | SPEAKER_SIDE_RIGHT;
        if (!((details.InputChannels == 2u && channel_mask == front_stereo_mask)
                || (details.InputChannels == 8u && channel_mask == surround_mask))) {
            throw std::runtime_error("Native front stereo output layout is unsupported.");
        }
        output_channels = details.InputChannels;
        output_matrix[0u] = (*channel_gains)[0u];
        output_matrix[1u] = (*channel_gains)[1u];
    }
    if (next_id == std::numeric_limits<std::uint64_t>::max()) {
        throw std::overflow_error("Native audio playback identifier exhausted.");
    }
    auto active = std::make_unique<ActiveVoice>();
    active->clip = std::move(clip);
    active->id = next_id;
    check_audio(engine->CreateSourceVoice(&active->voice, &format), "Audio source creation");
    check_audio(active->voice->SetVolume(gain), "Audio source volume");
    if (channel_gains != nullptr) {
        check_audio(
            active->voice->SetOutputMatrix(master, 1u, output_channels, output_matrix.data()),
            "Audio source output matrix");
    }
    for (std::size_t index = 0u; index < active->clip->streams.size(); ++index) {
        const auto& stream = active->clip->streams[index];
        const bool last = index + 1u == active->clip->streams.size();
        XAUDIO2_BUFFER buffer{};
        buffer.AudioBytes = static_cast<UINT32>(stream.samples.size() * sizeof(std::int16_t));
        buffer.pAudioData = reinterpret_cast<const BYTE*>(stream.samples.data());
        if (last && active->clip->loop_last_stream) {
            buffer.LoopLength = stream.sample_count;
            buffer.LoopCount = XAUDIO2_LOOP_INFINITE;
        } else if (last) {
            buffer.Flags = XAUDIO2_END_OF_STREAM;
        }
        check_audio(active->voice->SubmitSourceBuffer(&buffer), "Audio buffer submission");
    }
    check_audio(active->voice->Start(), "Audio source start");
    const auto id = active->id;
    voices.push_back(std::move(active));
    ++next_id;
    ++counters.submitted;
    return id;
}

std::uint64_t NativeAudioOutput::play(std::shared_ptr<const NativeAudioClip> clip, float gain) {
    return impl_->play_clip(std::move(clip), gain, nullptr);
}

std::uint64_t NativeAudioOutput::play_front_stereo(
    std::shared_ptr<const NativeAudioClip> clip,
    float gain,
    const std::array<float, 2u>& channel_gains) {
    return impl_->play_clip(std::move(clip), gain, &channel_gains);
}

bool NativeAudioOutput::stop(std::uint64_t playback_id) {
    const auto found = std::find_if(impl_->voices.begin(), impl_->voices.end(),
        [playback_id](const auto& voice) { return voice->id == playback_id; });
    if (found == impl_->voices.end()) return false;
    impl_->voices.erase(found);
    ++impl_->counters.stopped;
    return true;
}

void NativeAudioOutput::stop_all() {
    impl_->counters.stopped += impl_->voices.size();
    impl_->voices.clear();
}

void NativeAudioOutput::set_paused(bool paused) {
    if (paused == impl_->counters.paused) return;
    if (paused) impl_->engine->StopEngine();
    else check_audio(impl_->engine->StartEngine(), "Audio engine resume");
    impl_->counters.paused = paused;
}

void NativeAudioOutput::update() {
    for (auto cursor = impl_->voices.begin(); cursor != impl_->voices.end();) {
        XAUDIO2_VOICE_STATE state{};
        (*cursor)->voice->GetState(&state, XAUDIO2_VOICE_NOSAMPLESPLAYED);
        if (state.BuffersQueued == 0u) {
            cursor = impl_->voices.erase(cursor);
            ++impl_->counters.completed;
        } else {
            ++cursor;
        }
    }
}

NativeAudioPlaybackState NativeAudioOutput::playback_state(std::uint64_t playback_id) const {
    const auto found = std::find_if(impl_->voices.begin(), impl_->voices.end(),
        [playback_id](const auto& voice) { return voice->id == playback_id; });
    if (found == impl_->voices.end()) return {};
    XAUDIO2_VOICE_STATE state{};
    (*found)->voice->GetState(&state);
    return {state.BuffersQueued, state.SamplesPlayed};
}

NativeAudioOutputState NativeAudioOutput::state() const {
    auto result = impl_->counters;
    result.active = impl_->voices.size();
    return result;
}

}
