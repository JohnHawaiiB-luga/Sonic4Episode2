#include "camera_matrix_d3dx.h"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <d3d9.h>
#include <xmmintrin.h>

#include <cstring>
#include <stdexcept>
#include <string>

namespace {

class FloatingPointScope {
public:
    explicit FloatingPointScope(CameraPrecision precision) : mxcsr_(_mm_getcsr()) {
        if (precision != CameraPrecision::Single && precision != CameraPrecision::Double) {
            throw std::invalid_argument("camera precision is unsupported");
        }
#if defined(_M_IX86)
        unsigned short saved = 0u;
        __asm fnstcw saved
        control_word_ = saved;
        const unsigned short selected = precision == CameraPrecision::Single ? 0x007fu : 0x027fu;
        __asm fldcw selected
#endif
        _mm_setcsr(0x1f80u);
    }

    ~FloatingPointScope() {
#if defined(_M_IX86)
        const unsigned short saved = control_word_;
        __asm fldcw saved
#endif
        _mm_setcsr(mxcsr_);
    }

private:
    unsigned int mxcsr_;
#if defined(_M_IX86)
    unsigned short control_word_;
#endif
};

template <typename Function>
Function resolve(HMODULE module, const char* name) {
    const FARPROC address = GetProcAddress(module, name);
    if (address == nullptr) {
        throw std::runtime_error(std::string("DirectX matrix entry point is missing: ") + name);
    }
    static_assert(sizeof(Function) == sizeof(address));
    Function result = nullptr;
    std::memcpy(&result, &address, sizeof(result));
    return result;
}

D3DMATRIX to_native(const CameraMatrix& value) {
    static_assert(sizeof(D3DMATRIX) == sizeof(CameraMatrix));
    D3DMATRIX result{};
    std::memcpy(&result, value.data(), sizeof(result));
    return result;
}

CameraMatrix from_native(const D3DMATRIX& value) {
    CameraMatrix result{};
    std::memcpy(result.data(), &value, sizeof(value));
    return result;
}

}

struct D3dxCameraMatrixBackend::Implementation {
    struct QuaternionValue {
        float x;
        float y;
        float z;
        float w;
    };
    using Rotation = D3DMATRIX* (WINAPI*)(D3DMATRIX*, float);
    using QuaternionRotation = D3DMATRIX* (WINAPI*)(D3DMATRIX*, const QuaternionValue*);
    using Multiply = D3DMATRIX* (WINAPI*)(D3DMATRIX*, const D3DMATRIX*, const D3DMATRIX*);
    using Perspective = D3DMATRIX* (WINAPI*)(D3DMATRIX*, float, float, float, float);

    HMODULE module = nullptr;
    Rotation rotation = nullptr;
    QuaternionRotation quaternion_rotation = nullptr;
    Multiply multiply = nullptr;
    Perspective perspective = nullptr;

    ~Implementation() {
        if (module != nullptr) {
            FreeLibrary(module);
        }
    }
};

D3dxCameraMatrixBackend::D3dxCameraMatrixBackend()
    : implementation_(std::make_unique<Implementation>()) {
    auto& impl = *implementation_;
    impl.module = LoadLibraryExW(L"d3dx9_43.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
    if (impl.module == nullptr) {
        throw std::runtime_error("The system DirectX runtime d3dx9_43.dll could not be loaded.");
    }
    impl.rotation = resolve<Implementation::Rotation>(impl.module, "D3DXMatrixRotationZ");
    impl.quaternion_rotation = resolve<Implementation::QuaternionRotation>(impl.module, "D3DXMatrixRotationQuaternion");
    impl.multiply = resolve<Implementation::Multiply>(impl.module, "D3DXMatrixMultiply");
    impl.perspective = resolve<Implementation::Perspective>(impl.module, "D3DXMatrixPerspectiveFovRH");
}

D3dxCameraMatrixBackend::~D3dxCameraMatrixBackend() = default;

CameraMatrix D3dxCameraMatrixBackend::rotation_z(float radians, CameraPrecision precision) const {
    D3DMATRIX result{};
    const FloatingPointScope scope{precision};
    implementation_->rotation(&result, radians);
    return from_native(result);
}

CameraMatrix D3dxCameraMatrixBackend::rotation_quaternion(
    const std::array<float, 4u>& rotation, CameraPrecision precision) const {
    D3DMATRIX result{};
    const Implementation::QuaternionValue quaternion{rotation[0u], rotation[1u], rotation[2u], rotation[3u]};
    static_assert(sizeof(quaternion) == 4u * sizeof(float));
    const FloatingPointScope scope{precision};
    implementation_->quaternion_rotation(&result, &quaternion);
#if defined(_M_X64)
    if (precision == CameraPrecision::Double) {
        // The x86 runtime spills the products to float but retains double precision between diagonal subtractions.
        std::array<float, 3u> squares{};
        for (std::size_t axis = 0u; axis < squares.size(); ++axis) {
            const float doubled = rotation[axis] + rotation[axis];
            squares[axis] = static_cast<float>(static_cast<double>(doubled)
                * static_cast<double>(rotation[axis]));
        }
        result.m[0][0] = static_cast<float>((1.0 - static_cast<double>(squares[1u]))
            - static_cast<double>(squares[2u]));
        result.m[1][1] = static_cast<float>((1.0 - static_cast<double>(squares[0u]))
            - static_cast<double>(squares[2u]));
        result.m[2][2] = static_cast<float>((1.0 - static_cast<double>(squares[0u]))
            - static_cast<double>(squares[1u]));
    }
#endif
    return from_native(result);
}

CameraMatrix D3dxCameraMatrixBackend::multiply(
    const CameraMatrix& left, const CameraMatrix& right, CameraPrecision precision) const {
    D3DMATRIX result = to_native(left);
    const D3DMATRIX right_matrix = to_native(right);
    const FloatingPointScope scope{precision};
    implementation_->multiply(&result, &result, &right_matrix);
    return from_native(result);
}

CameraMatrix D3dxCameraMatrixBackend::perspective_fov_rh(
    const CameraProjection& projection, CameraPrecision precision) const {
    D3DMATRIX result{};
    const FloatingPointScope scope{precision};
    implementation_->perspective(
        &result, projection.fov_radians, projection.aspect, projection.near_plane, projection.far_plane);
#if defined(_M_X64)
    if (precision == CameraPrecision::Double) {
        // The x86 runtime rounds the depth ratio to float before multiplying by the near plane.
        const double difference = static_cast<double>(projection.near_plane)
            - static_cast<double>(projection.far_plane);
        const float depth = static_cast<float>(static_cast<double>(projection.far_plane) / difference);
        result.m[2][2] = depth;
        result.m[3][2] = static_cast<float>(static_cast<double>(depth)
            * static_cast<double>(projection.near_plane));
    }
#endif
    return from_native(result);
}
