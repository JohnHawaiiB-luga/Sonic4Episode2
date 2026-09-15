#pragma once

#include "camera_matrix.h"

#include <memory>

class D3dxCameraMatrixBackend final : public CameraMatrixBackend {
public:
    D3dxCameraMatrixBackend();
    ~D3dxCameraMatrixBackend() override;

    D3dxCameraMatrixBackend(const D3dxCameraMatrixBackend&) = delete;
    D3dxCameraMatrixBackend& operator=(const D3dxCameraMatrixBackend&) = delete;

    CameraMatrix rotation_z(float radians, CameraPrecision precision) const override;
    CameraMatrix rotation_quaternion(
        const std::array<float, 4u>& rotation, CameraPrecision precision) const override;
    CameraMatrix multiply(
        const CameraMatrix& left, const CameraMatrix& right, CameraPrecision precision) const override;
    CameraMatrix perspective_fov_rh(
        const CameraProjection& projection, CameraPrecision precision) const override;

private:
    struct Implementation;
    std::unique_ptr<Implementation> implementation_;
};
