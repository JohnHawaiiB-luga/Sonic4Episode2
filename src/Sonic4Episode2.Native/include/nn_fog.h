#pragma once

#include <array>
#include <cstdint>

enum class NnFogPrecision {
    Single,
    Double,
};

struct NnFogParameters {
    float density;
    float start;
    float end;
    float scale;
};

struct NnFogState {
    std::array<float, 4u> color;
    NnFogParameters configured;
    NnFogParameters active;
    std::uint32_t packed_argb;
    std::uint32_t mode;
    float range_near;
    float range_far;
    float coefficient_a;
    float coefficient_b;
    std::uint32_t requested;
    std::uint32_t effective;
};

void nn_set_fog_request(NnFogState& state, bool requested, NnFogPrecision precision);
void nn_set_fog_color(
    NnFogState& state,
    const std::array<float, 3u>& rgb,
    NnFogPrecision precision);
void nn_set_fog_range(
    NnFogState& state,
    float near_distance,
    float far_distance,
    NnFogPrecision precision);
void nn_apply_fog(
    NnFogState& state,
    bool enabled,
    float fallback_distance,
    NnFogPrecision precision);

