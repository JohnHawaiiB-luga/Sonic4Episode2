#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <d3d9.h>

#include "d3d9_device.h"

#include <atomic>
#include <cstring>
#include <iomanip>
#include <limits>
#include <sstream>
#include <utility>

namespace sonic4ep2::d3d9 {
namespace {

template <typename T>
class ComPointer final {
public:
    ComPointer() = default;

    ~ComPointer() {
        reset();
    }

    ComPointer(const ComPointer&) = delete;
    ComPointer& operator=(const ComPointer&) = delete;

    ComPointer(ComPointer&& other) noexcept
        : pointer_(other.pointer_) {
        other.pointer_ = nullptr;
    }

    ComPointer& operator=(ComPointer&& other) noexcept {
        if (this != &other) {
            reset();
            pointer_ = other.pointer_;
            other.pointer_ = nullptr;
        }
        return *this;
    }

    T* get() const noexcept {
        return pointer_;
    }

    T** put() noexcept {
        reset();
        return &pointer_;
    }

    void reset(T* pointer = nullptr) noexcept {
        if (pointer_ != nullptr) {
            pointer_->Release();
        }
        pointer_ = pointer;
    }

private:
    T* pointer_ = nullptr;
};

std::string format_error(std::string message, HRESULT result) {
    std::ostringstream stream;
    stream << message << " (HRESULT 0x" << std::uppercase << std::hex
           << static_cast<std::uint32_t>(result) << ')';
    return stream.str();
}

void check_result(HRESULT result, const char* operation) {
    if (FAILED(result)) {
        throw D3d9Error(operation, static_cast<std::int32_t>(result));
    }
}

LRESULT CALLBACK hidden_window_procedure(HWND window, UINT message, WPARAM w_param, LPARAM l_param) {
    return DefWindowProcW(window, message, w_param, l_param);
}

D3DFORMAT to_depth_format(DepthStencilFormat format) {
    switch (format) {
    case DepthStencilFormat::None:
        return D3DFMT_UNKNOWN;
    case DepthStencilFormat::D16:
        return D3DFMT_D16;
    case DepthStencilFormat::D24S8:
        return D3DFMT_D24S8;
    }
    throw std::invalid_argument("Unknown depth-stencil format.");
}

std::uint32_t rgba_to_argb(std::uint32_t rgba) noexcept {
    const std::uint32_t red = (rgba >> 24u) & 0xffu;
    const std::uint32_t green = (rgba >> 16u) & 0xffu;
    const std::uint32_t blue = (rgba >> 8u) & 0xffu;
    const std::uint32_t alpha = rgba & 0xffu;
    return (alpha << 24u) | (red << 16u) | (green << 8u) | blue;
}

DeviceStatus to_device_status(HRESULT result) {
    if (result == D3D_OK) {
        return DeviceStatus::Ready;
    }
    if (result == D3DERR_DEVICENOTRESET) {
        return DeviceStatus::NotReset;
    }
    return DeviceStatus::Lost;
}

DeviceDiagnostics adapter_diagnostics(IDirect3D9* direct3d) {
    DeviceDiagnostics diagnostics{};
    const UINT count = direct3d->GetAdapterCount();
    diagnostics.adapters.reserve(count);

    for (UINT adapter = 0u; adapter < count; ++adapter) {
        D3DCAPS9 caps{};
        const HRESULT caps_result = direct3d->GetDeviceCaps(adapter, D3DDEVTYPE_HAL, &caps);
        D3DADAPTER_IDENTIFIER9 identifier{};
        const HRESULT identifier_result = direct3d->GetAdapterIdentifier(adapter, 0u, &identifier);

        AdapterInfo info{};
        info.index = adapter;
        info.description = SUCCEEDED(identifier_result) ? identifier.Description : "<identifier unavailable>";
        info.caps_result = static_cast<std::int32_t>(caps_result);
        if (SUCCEEDED(caps_result)) {
            info.vertex_shader_version = caps.VertexShaderVersion;
            info.pixel_shader_version = caps.PixelShaderVersion;
            info.max_vertex_shader_constants = caps.MaxVertexShaderConst;
            info.hardware_transform_and_light = (caps.DevCaps & D3DDEVCAPS_HWTRANSFORMANDLIGHT) != 0u;
        }
        diagnostics.adapters.push_back(std::move(info));
    }

    return diagnostics;
}

} // namespace

struct D3d9Device::Implementation final {
    ComPointer<IDirect3D9> direct3d;
    ComPointer<IDirect3DDevice9> device;
    D3DPRESENT_PARAMETERS presentation{};
    DeviceDiagnostics diagnostics{};
    HWND helper_window = nullptr;
    ATOM helper_window_class = 0;
    HINSTANCE helper_window_instance = nullptr;
    bool owns_helper_window = false;
    bool scene_active = false;
    std::atomic<std::uint32_t> default_pool_resource_count{0u};
    std::uint32_t width = 0u;
    std::uint32_t height = 0u;

    ~Implementation() {
        device.reset();
        direct3d.reset();
        if (owns_helper_window && helper_window != nullptr) {
            DestroyWindow(helper_window);
        }
        if (helper_window_class != 0u && helper_window_instance != nullptr) {
            UnregisterClassW(L"Sonic4Episode2D3d9HiddenWindow", helper_window_instance);
        }
    }
};

D3d9Error::D3d9Error(std::string message, std::int32_t result)
    : std::runtime_error(format_error(std::move(message), static_cast<HRESULT>(result)))
    , result_(result) {
}

std::int32_t D3d9Error::result() const noexcept {
    return result_;
}

D3d9Device::D3d9Device(std::shared_ptr<Implementation> implementation)
    : implementation_(std::move(implementation)) {
}

D3d9Device::~D3d9Device() = default;
D3d9Device::D3d9Device(D3d9Device&&) noexcept = default;
D3d9Device& D3d9Device::operator=(D3d9Device&&) noexcept = default;

std::vector<AdapterInfo> D3d9Device::enumerate_adapters() {
    ComPointer<IDirect3D9> direct3d;
    direct3d.reset(Direct3DCreate9(D3D_SDK_VERSION));
    if (direct3d.get() == nullptr) {
        throw D3d9Error("Direct3DCreate9 failed", static_cast<std::int32_t>(E_FAIL));
    }
    return adapter_diagnostics(direct3d.get()).adapters;
}

std::unique_ptr<D3d9Device> D3d9Device::create(
    const DeviceCreateOptions& options, DeviceDiagnostics* diagnostics) {
    if (options.width == 0u || options.height == 0u) {
        throw std::invalid_argument("D3D9 device dimensions must be nonzero.");
    }

    auto implementation = std::make_shared<Implementation>();
    implementation->direct3d.reset(Direct3DCreate9(D3D_SDK_VERSION));
    if (implementation->direct3d.get() == nullptr) {
        throw D3d9Error("Direct3DCreate9 failed", static_cast<std::int32_t>(E_FAIL));
    }

    implementation->diagnostics = adapter_diagnostics(implementation->direct3d.get());
    if (options.adapter >= implementation->diagnostics.adapters.size()) {
        throw std::out_of_range("Requested D3D9 adapter is unavailable.");
    }
    implementation->diagnostics.selected_adapter = options.adapter;
    implementation->diagnostics.used_software_vertex_processing = false;

    HWND device_window = static_cast<HWND>(options.host_window);
    if (device_window != nullptr && !IsWindow(device_window)) {
        throw std::invalid_argument("The supplied D3D9 host window is not valid.");
    }
    if (device_window == nullptr) {
        implementation->helper_window_instance = GetModuleHandleW(nullptr);
        WNDCLASSEXW window_class{};
        window_class.cbSize = sizeof(window_class);
        window_class.lpfnWndProc = hidden_window_procedure;
        window_class.hInstance = implementation->helper_window_instance;
        window_class.lpszClassName = L"Sonic4Episode2D3d9HiddenWindow";
        implementation->helper_window_class = RegisterClassExW(&window_class);
        if (implementation->helper_window_class == 0u) {
            const DWORD error = GetLastError();
            if (error != ERROR_CLASS_ALREADY_EXISTS) {
                throw D3d9Error("RegisterClassExW for hidden D3D9 window failed", static_cast<std::int32_t>(HRESULT_FROM_WIN32(error)));
            }
        }
        implementation->helper_window = CreateWindowExW(
            WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE,
            window_class.lpszClassName,
            L"",
            WS_POPUP,
            0,
            0,
            static_cast<int>(options.width),
            static_cast<int>(options.height),
            nullptr,
            nullptr,
            implementation->helper_window_instance,
            nullptr);
        if (implementation->helper_window == nullptr) {
            const DWORD error = GetLastError();
            throw D3d9Error("CreateWindowExW for hidden D3D9 window failed", static_cast<std::int32_t>(HRESULT_FROM_WIN32(error)));
        }
        implementation->owns_helper_window = true;
        device_window = implementation->helper_window;
    }

    const D3DFORMAT depth_format = to_depth_format(options.depth_stencil_format);
    implementation->presentation.BackBufferWidth = options.width;
    implementation->presentation.BackBufferHeight = options.height;
    implementation->presentation.BackBufferFormat = D3DFMT_UNKNOWN;
    implementation->presentation.BackBufferCount = 1u;
    implementation->presentation.MultiSampleType = D3DMULTISAMPLE_NONE;
    implementation->presentation.MultiSampleQuality = 0u;
    implementation->presentation.SwapEffect = D3DSWAPEFFECT_DISCARD;
    implementation->presentation.hDeviceWindow = device_window;
    implementation->presentation.Windowed = TRUE;
    implementation->presentation.EnableAutoDepthStencil = depth_format != D3DFMT_UNKNOWN ? TRUE : FALSE;
    implementation->presentation.AutoDepthStencilFormat = depth_format;
    implementation->presentation.Flags = 0u;
    implementation->presentation.FullScreen_RefreshRateInHz = 0u;
    implementation->presentation.PresentationInterval = options.enable_vsync
        ? D3DPRESENT_INTERVAL_ONE
        : D3DPRESENT_INTERVAL_IMMEDIATE;

    const auto try_create = [&](DWORD behavior_flags, bool software_vertex_processing) -> HRESULT {
        ComPointer<IDirect3DDevice9> candidate;
        const HRESULT result = implementation->direct3d.get()->CreateDevice(
            options.adapter,
            D3DDEVTYPE_HAL,
            device_window,
            behavior_flags,
            &implementation->presentation,
            candidate.put());
        implementation->diagnostics.attempts.push_back(DeviceAttempt{
            behavior_flags,
            static_cast<std::int32_t>(result),
            software_vertex_processing,
        });
        if (SUCCEEDED(result)) {
            implementation->device = std::move(candidate);
            implementation->diagnostics.used_software_vertex_processing = software_vertex_processing;
        }
        return result;
    };

    HRESULT result = try_create(
        D3DCREATE_MULTITHREADED | D3DCREATE_HARDWARE_VERTEXPROCESSING,
        false);
    if (FAILED(result) && options.allow_software_vertex_processing) {
        result = try_create(
            D3DCREATE_MULTITHREADED | D3DCREATE_SOFTWARE_VERTEXPROCESSING,
            true);
    }
    if (FAILED(result)) {
        if (diagnostics != nullptr) {
            *diagnostics = implementation->diagnostics;
        }
        throw D3d9Error("IDirect3D9::CreateDevice failed", static_cast<std::int32_t>(result));
    }

    implementation->width = options.width;
    implementation->height = options.height;
    if (diagnostics != nullptr) {
        *diagnostics = implementation->diagnostics;
    }
    return std::unique_ptr<D3d9Device>(new D3d9Device(std::move(implementation)));
}

DeviceStatus D3d9Device::cooperative_status() const {
    if (implementation_ == nullptr || implementation_->device.get() == nullptr) {
        return DeviceStatus::Lost;
    }
    return to_device_status(implementation_->device.get()->TestCooperativeLevel());
}

void D3d9Device::resize(std::uint32_t width, std::uint32_t height) {
    if (implementation_ == nullptr) {
        throw std::logic_error("D3D9 device is not initialized.");
    }
    if (width == 0u || height == 0u) {
        throw std::invalid_argument("D3D9 resize dimensions must be nonzero.");
    }
    if (implementation_->scene_active) {
        throw std::logic_error("Cannot reset a D3D9 device while a scene is active.");
    }
    if (implementation_->default_pool_resource_count.load(std::memory_order_acquire) != 0u) {
        throw D3d9Error(
            "Cannot reset D3D9 device while default-pool resources are still active",
            static_cast<std::int32_t>(D3DERR_INVALIDCALL));
    }

    D3DPRESENT_PARAMETERS presentation = implementation_->presentation;
    presentation.BackBufferWidth = width;
    presentation.BackBufferHeight = height;
    const HRESULT result = implementation_->device.get()->Reset(&presentation);
    check_result(result, "IDirect3DDevice9::Reset failed");

    implementation_->presentation = presentation;
    implementation_->width = width;
    implementation_->height = height;
}

void D3d9Device::begin_frame(const ClearDescription& clear) {
    if (implementation_ == nullptr || implementation_->device.get() == nullptr) {
        throw std::logic_error("D3D9 device is not initialized.");
    }
    if (implementation_->scene_active) {
        throw std::logic_error("D3D9 scene is already active.");
    }
    const HRESULT cooperative_result = implementation_->device.get()->TestCooperativeLevel();
    if (cooperative_result != D3D_OK) {
        throw D3d9Error("D3D9 device is not ready for a frame", static_cast<std::int32_t>(cooperative_result));
    }

    DWORD flags = 0u;
    if (clear.clear_color) {
        flags |= D3DCLEAR_TARGET;
    }
    if (clear.clear_depth) {
        flags |= D3DCLEAR_ZBUFFER;
    }
    if (clear.clear_stencil) {
        flags |= D3DCLEAR_STENCIL;
    }
    if (flags != 0u) {
        check_result(
            implementation_->device.get()->Clear(
                0u,
                nullptr,
                flags,
                rgba_to_argb(clear.color_rgba),
                clear.depth,
                clear.stencil),
            "IDirect3DDevice9::Clear failed");
    }

    check_result(implementation_->device.get()->BeginScene(), "IDirect3DDevice9::BeginScene failed");
    implementation_->scene_active = true;
}

void D3d9Device::end_frame() {
    if (implementation_ == nullptr || !implementation_->scene_active) {
        throw std::logic_error("D3D9 scene is not active.");
    }
    const HRESULT result = implementation_->device.get()->EndScene();
    implementation_->scene_active = false;
    check_result(result, "IDirect3DDevice9::EndScene failed");
}

void D3d9Device::present() {
    if (implementation_ == nullptr || implementation_->device.get() == nullptr) {
        throw std::logic_error("D3D9 device is not initialized.");
    }
    if (implementation_->scene_active) {
        throw std::logic_error("Cannot present while a D3D9 scene is active.");
    }
    check_result(
        implementation_->device.get()->Present(nullptr, nullptr, nullptr, nullptr),
        "IDirect3DDevice9::Present failed");
}

ReadbackImage D3d9Device::readback_current_target() const {
    if (implementation_ == nullptr || implementation_->device.get() == nullptr) {
        throw std::logic_error("D3D9 device is not initialized.");
    }
    if (implementation_->scene_active) {
        throw std::logic_error("End the D3D9 scene before reading the render target.");
    }

    ComPointer<IDirect3DSurface9> render_target;
    check_result(
        implementation_->device.get()->GetRenderTarget(0u, render_target.put()),
        "IDirect3DDevice9::GetRenderTarget failed");

    D3DSURFACE_DESC description{};
    check_result(render_target.get()->GetDesc(&description), "IDirect3DSurface9::GetDesc failed");
    if (description.Format != D3DFMT_A8R8G8B8
        && description.Format != D3DFMT_X8R8G8B8
        && description.Format != D3DFMT_A8B8G8R8) {
        throw std::runtime_error("D3D9 render target format cannot be converted to RGBA readback.");
    }
    if (description.Width == 0u || description.Height == 0u
        || static_cast<std::size_t>(description.Width) > std::numeric_limits<std::size_t>::max() / 4u) {
        throw std::overflow_error("D3D9 render target dimensions overflow the RGBA readback buffer.");
    }
    const std::size_t row_bytes = static_cast<std::size_t>(description.Width) * 4u;
    if (row_bytes > static_cast<std::size_t>(std::numeric_limits<LONG>::max())
        || static_cast<std::size_t>(description.Height) > std::numeric_limits<std::size_t>::max() / row_bytes) {
        throw std::overflow_error("D3D9 render target dimensions overflow the RGBA readback buffer.");
    }

    ComPointer<IDirect3DSurface9> system_memory_surface;
    check_result(
        implementation_->device.get()->CreateOffscreenPlainSurface(
            description.Width,
            description.Height,
            description.Format,
            D3DPOOL_SYSTEMMEM,
            system_memory_surface.put(),
            nullptr),
        "IDirect3DDevice9::CreateOffscreenPlainSurface failed");
    check_result(
        implementation_->device.get()->GetRenderTargetData(render_target.get(), system_memory_surface.get()),
        "IDirect3DDevice9::GetRenderTargetData failed");

    D3DLOCKED_RECT locked{};
    check_result(system_memory_surface.get()->LockRect(&locked, nullptr, D3DLOCK_READONLY), "IDirect3DSurface9::LockRect failed");
    struct UnlockGuard final {
        IDirect3DSurface9* surface;
        ~UnlockGuard() {
            surface->UnlockRect();
        }
    } unlock_guard{system_memory_surface.get()};

    if (locked.pBits == nullptr || locked.Pitch < 0 || static_cast<std::size_t>(locked.Pitch) < row_bytes) {
        throw std::runtime_error("D3D9 render target readback returned an invalid row pitch.");
    }

    ReadbackImage readback{};
    readback.width = description.Width;
    readback.height = description.Height;
    readback.rgba.resize(row_bytes * description.Height);
    const auto* source = static_cast<const std::uint8_t*>(locked.pBits);
    for (UINT y = 0u; y < description.Height; ++y) {
        const auto* source_row = source + static_cast<std::size_t>(locked.Pitch) * y;
        auto* destination_row = readback.rgba.data() + static_cast<std::size_t>(description.Width) * y * 4u;
        for (UINT x = 0u; x < description.Width; ++x) {
            const auto* pixel = source_row + x * 4u;
            auto* destination = destination_row + x * 4u;
            if (description.Format == D3DFMT_A8B8G8R8) {
                destination[0] = pixel[0];
                destination[1] = pixel[1];
                destination[2] = pixel[2];
                destination[3] = pixel[3];
            } else {
                destination[0] = pixel[2];
                destination[1] = pixel[1];
                destination[2] = pixel[0];
                destination[3] = description.Format == D3DFMT_X8R8G8B8 ? 0xffu : pixel[3];
            }
        }
    }
    return readback;
}

std::uint32_t D3d9Device::width() const noexcept {
    return implementation_ == nullptr ? 0u : implementation_->width;
}

std::uint32_t D3d9Device::height() const noexcept {
    return implementation_ == nullptr ? 0u : implementation_->height;
}

const DeviceDiagnostics& D3d9Device::diagnostics() const noexcept {
    static const DeviceDiagnostics empty{};
    return implementation_ == nullptr ? empty : implementation_->diagnostics;
}

void* D3d9Device::native_device(const std::shared_ptr<Implementation>& implementation) noexcept {
    return implementation == nullptr ? nullptr : implementation->device.get();
}

bool D3d9Device::scene_active(const std::shared_ptr<Implementation>& implementation) noexcept {
    return implementation != nullptr && implementation->scene_active;
}

void D3d9Device::retain_default_pool_resource(const std::shared_ptr<Implementation>& implementation) {
    if (implementation == nullptr || implementation->device.get() == nullptr) {
        throw std::logic_error("Cannot retain a resource for an uninitialized D3D9 device.");
    }
    implementation->default_pool_resource_count.fetch_add(1u, std::memory_order_acq_rel);
}

void D3d9Device::release_default_pool_resource(const std::shared_ptr<Implementation>& implementation) noexcept {
    if (implementation == nullptr) {
        return;
    }
    std::uint32_t current = implementation->default_pool_resource_count.load(std::memory_order_acquire);
    while (current != 0u) {
        if (implementation->default_pool_resource_count.compare_exchange_weak(
                current,
                current - 1u,
                std::memory_order_acq_rel,
                std::memory_order_acquire)) {
            return;
        }
    }
}

} // namespace sonic4ep2::d3d9
