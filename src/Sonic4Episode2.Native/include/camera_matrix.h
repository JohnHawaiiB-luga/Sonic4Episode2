#pragma once

#include "camera_math.h"
#include "camera_view.h"

#include <array>

using CameraMatrix = std::array<float, 16u>;

class CameraMatrixBackend {
public:
    virtual ~CameraMatrixBackend() = default;

    virtual CameraMatrix rotation_z(float radians, CameraPrecision precision) const = 0;
    virtual CameraMatrix rotation_quaternion(
        const std::array<float, 4u>& rotation, CameraPrecision precision) const;
    virtual CameraMatrix multiply(
        const CameraMatrix& left, const CameraMatrix& right, CameraPrecision precision) const = 0;
    virtual CameraMatrix perspective_fov_rh(
        const CameraProjection& projection, CameraPrecision precision) const = 0;
};

CameraMatrix make_camera_view_basis(const CameraViewParameters& view, CameraPrecision precision);

CameraMatrix make_camera_view_matrix(
    const CameraViewParameters& view, CameraPrecision precision, const CameraMatrixBackend& backend);

CameraMatrix make_camera_projection_matrix(
    const CameraProjection& projection, CameraPrecision precision, const CameraMatrixBackend& backend);
