#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

struct CriAudioStream {
    std::uint32_t loop_flag = 0u;
    std::vector<std::uint8_t> adx;
};

struct CriAudioSpatialParameters {
    float volume = 1.0f;
    float volume_gain = 1.0f;
    float angle_degrees = 0.0f;
    float angle_gain = 1.0f;
    float directionality = 1.0f;
    float directionality_gain = 1.0f;
    std::array<std::int32_t, 8u> dry_levels{};
    std::string dry_output;
};

struct CriAudioCue {
    std::string name;
    std::uint32_t id = 0u;
    std::int32_t synth_volume = 0;
    CriAudioSpatialParameters spatial;
    std::uint32_t release_time = 0u;
    std::uint32_t channels = 0u;
    std::uint32_t sample_rate = 0u;
    std::uint32_t declared_sample_count = 0u;
    std::string external_path;
    std::vector<CriAudioStream> streams;
};

CriAudioCue read_cri_csb_cue(
    const std::uint8_t* data,
    std::size_t size,
    const std::string& cue_name);

std::vector<CriAudioStream> read_cri_aax_streams(
    const std::uint8_t* data,
    std::size_t size);

std::vector<std::uint8_t> read_cri_cpk_file(
    const std::uint8_t* data,
    std::size_t size,
    const std::string& path);
