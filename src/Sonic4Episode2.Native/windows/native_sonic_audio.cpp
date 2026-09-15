#include "native_sonic_audio.h"

#include "cri_audio_bank.h"
#include "native_audio_output.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <fstream>
#include <iomanip>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <utility>

namespace sonic4ep2::app {
namespace {

constexpr std::array<SonicSoundCue, 6u> kEffectCues{
    SonicSoundCue::Jump, SonicSoundCue::Spin, SonicSoundCue::Dash1, SonicSoundCue::Dash2,
    SonicSoundCue::Ring1L, SonicSoundCue::Ring1R};
constexpr char kStageMusicCue[] = "ep2_sng_z1a1";

struct Sound {
    std::shared_ptr<const NativeAudioClip> clip;
    float gain = 0.0f;
    std::optional<std::array<float, 2u>> channel_gains;
};

std::vector<std::uint8_t> read_bank(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary | std::ios::ate);
    if (!input) throw std::runtime_error("Cannot open original sound bank: " + path.u8string());
    const auto length = input.tellg();
    if (length <= 0 || length > 512 * 1024 * 1024) {
        throw std::runtime_error("Original sound bank size is unsupported.");
    }
    std::vector<std::uint8_t> bytes(static_cast<std::size_t>(length));
    input.seekg(0);
    input.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    if (!input) throw std::runtime_error("Cannot read original sound bank: " + path.u8string());
    return bytes;
}

Sound decode_cue(const CriAudioCue& cue) {
    if (cue.streams.empty() || cue.synth_volume < 0) {
        throw std::runtime_error("Original sound cue configuration is unsupported.");
    }
    auto clip = std::make_shared<NativeAudioClip>();
    clip->streams.reserve(cue.streams.size());
    for (std::size_t index = 0u; index < cue.streams.size(); ++index) {
        const auto& stream = cue.streams[index];
        if (stream.loop_flag > 1u
            || (stream.loop_flag != 0u && index + 1u != cue.streams.size())) {
            throw std::runtime_error("Original sound cue loop order is unsupported.");
        }
        auto pcm = decode_adx_audio(stream.adx.data(), stream.adx.size());
        if (pcm.channels != cue.channels || pcm.sample_rate != cue.sample_rate) {
            throw std::runtime_error("Original sound cue metadata does not match its stream.");
        }
        clip->streams.push_back(std::move(pcm));
    }
    clip->loop_last_stream = cue.streams.back().loop_flag != 0u;
    return {std::move(clip), static_cast<float>(static_cast<double>(cue.synth_volume) / 1000.0)};
}

std::array<float, 2u> ring_channel_gains(const CriAudioCue& cue, float expected_angle) {
    const auto& spatial = cue.spatial;
    if (cue.channels != 1u || spatial.volume != 1.0f || spatial.volume_gain != 1.0f
        || spatial.angle_degrees != expected_angle || spatial.angle_gain != 1.0f
        || spatial.directionality != 1.0f || spatial.directionality_gain != 1.0f
        || spatial.dry_output != "<NULL>"
        || std::any_of(spatial.dry_levels.begin(), spatial.dry_levels.end(),
            [](std::int32_t value) { return value != 0; })) {
        throw std::runtime_error("Original ring spatial configuration is unsupported.");
    }
    volatile float relative = static_cast<float>(static_cast<double>(spatial.angle_degrees) / 30.0);
    relative = static_cast<float>(static_cast<double>(relative) * 90.0);
    volatile float phase = static_cast<float>((static_cast<double>(relative) + 90.0) / 180.0);
    phase = static_cast<float>(static_cast<double>(phase) * 3.1415927410125732421875);
    phase = static_cast<float>(static_cast<double>(phase) * 0.5);
    return {static_cast<float>(std::cos(static_cast<double>(phase))),
        static_cast<float>(std::sin(static_cast<double>(phase)))};
}

std::array<Sound, kEffectCues.size()> load_effects(const std::filesystem::path& data_root) {
    const auto bytes = read_bank(data_root / "SOUND/EP2_SND_FX_Z1.CSB");
    std::array<Sound, kEffectCues.size()> result;
    for (std::size_t index = 0u; index < result.size(); ++index) {
        const auto cue = read_cri_csb_cue(bytes.data(), bytes.size(), sonic_sound_cue_name(kEffectCues[index]));
        if (!cue.external_path.empty()) {
            throw std::runtime_error("Original Sonic effect must be contained in its bank.");
        }
        result[index] = decode_cue(cue);
        if (kEffectCues[index] == SonicSoundCue::Ring1L || kEffectCues[index] == SonicSoundCue::Ring1R) {
            result[index].channel_gains = ring_channel_gains(
                cue, kEffectCues[index] == SonicSoundCue::Ring1L ? -20.0f : 20.0f);
        }
        if (result[index].clip->loop_last_stream) {
            throw std::runtime_error("Original Sonic effect loop is unsupported.");
        }
    }
    return result;
}

Sound load_music(const std::filesystem::path& data_root) {
    const auto bytes = read_bank(data_root / "SOUND/SONICDL_SNG01.CSB");
    auto cue = read_cri_csb_cue(bytes.data(), bytes.size(), kStageMusicCue);
    if (cue.external_path.empty() || !cue.streams.empty()) {
        throw std::runtime_error("Original stage music must reference its stream bank.");
    }
    const auto archive = read_bank(data_root / "SOUND/SONICDL_SNG01.CPK");
    const auto aax = read_cri_cpk_file(archive.data(), archive.size(), cue.external_path);
    cue.streams = read_cri_aax_streams(aax.data(), aax.size());
    auto result = decode_cue(cue);
    if (!result.clip->loop_last_stream) {
        throw std::runtime_error("Original stage music loop is missing.");
    }
    return result;
}

std::size_t effect_index(SonicSoundCue cue) {
    for (std::size_t index = 0u; index < kEffectCues.size(); ++index) {
        if (kEffectCues[index] == cue) return index;
    }
    throw std::invalid_argument("Sonic sound cue is unsupported.");
}

void write_sound(std::ostream& output, const Sound& sound) {
    if (sound.channel_gains) {
        output << "\"channel_gains\":[" << (*sound.channel_gains)[0u] << ','
            << (*sound.channel_gains)[1u] << "],";
    }
    output << "\"gain\":" << sound.gain << ",\"streams\":[";
    for (std::size_t index = 0u; index < sound.clip->streams.size(); ++index) {
        if (index != 0u) output << ',';
        const auto& pcm = sound.clip->streams[index];
        output << "{\"channels\":" << pcm.channels << ",\"sample_rate\":" << pcm.sample_rate
            << ",\"sample_count\":" << pcm.sample_count << ",\"loop\":"
            << (sound.clip->loop_last_stream && index + 1u == sound.clip->streams.size()) << '}';
    }
    output << ']';
}

}

struct NativeSonicAudio::Impl {
    explicit Impl(const std::filesystem::path& data_root, bool muted)
        : effects(load_effects(data_root)), music(load_music(data_root)), output(muted) {
        output.set_paused(true);
    }

    std::array<Sound, kEffectCues.size()> effects;
    Sound music;
    NativeAudioOutput output;
    std::array<std::uint64_t, kEffectCues.size()> requests{};
    std::uint64_t music_playback_id = 0u;
};

NativeSonicAudio::NativeSonicAudio(const std::filesystem::path& data_root, bool muted)
    : impl_(std::make_unique<Impl>(data_root, muted)) {}

NativeSonicAudio::~NativeSonicAudio() = default;

void NativeSonicAudio::start_stage_music() {
    if (impl_->music_playback_id == 0u) {
        impl_->music_playback_id = impl_->output.play(impl_->music.clip, impl_->music.gain);
    }
}

void NativeSonicAudio::play(SonicSoundCue cue) {
    const auto index = effect_index(cue);
    const auto& sound = impl_->effects[index];
    if (sound.channel_gains) {
        impl_->output.play_front_stereo(sound.clip, sound.gain, *sound.channel_gains);
    } else {
        impl_->output.play(sound.clip, sound.gain);
    }
    ++impl_->requests[index];
}

void NativeSonicAudio::set_paused(bool paused) {
    impl_->output.set_paused(paused);
}

void NativeSonicAudio::update() {
    impl_->output.update();
}

std::string NativeSonicAudio::report_json() const {
    const auto state = impl_->output.state();
    std::ostringstream output;
    output << std::boolalpha << std::setprecision(9)
        << "{\"muted\":" << state.muted << ",\"paused\":" << state.paused
        << ",\"submitted\":" << state.submitted << ",\"completed\":" << state.completed
        << ",\"stopped\":" << state.stopped << ",\"active\":" << state.active
        << ",\"effects\":[";
    for (std::size_t index = 0u; index < kEffectCues.size(); ++index) {
        if (index != 0u) output << ',';
        output << "{\"cue\":\"" << sonic_sound_cue_name(kEffectCues[index])
            << "\",\"requests\":" << impl_->requests[index] << ',';
        write_sound(output, impl_->effects[index]);
        output << '}';
    }
    output << "],\"music\":{\"cue\":\"" << kStageMusicCue
        << "\",\"started\":" << (impl_->music_playback_id != 0u) << ',';
    write_sound(output, impl_->music);
    output << "}}";
    return output.str();
}

}
