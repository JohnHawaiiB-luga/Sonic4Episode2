#pragma once

#include <cstdint>

enum class CameraPrecision {
    Single,
    Double,
};

struct CameraProjectionDefaults {
    std::int32_t fov_angle;
    float aspect;
    float near_plane;
    float far_plane;
    float initial_scale;
};

CameraProjectionDefaults make_normal_main_camera_projection_defaults(float aspect);

struct CameraProjection {
    float fov_radians;
    float aspect;
    float near_plane;
    float far_plane;
};

CameraProjection make_camera_projection(
    std::int32_t angle,
    float aspect,
    float near_plane,
    float far_plane,
    CameraPrecision precision);

struct CameraScaleState {
    float current;
    float target;
    float speed;
};

void update_camera_scale(CameraScaleState& state, float& camera_scale, CameraPrecision precision);
