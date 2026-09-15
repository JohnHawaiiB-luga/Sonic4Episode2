#pragma once

#include <cstdint>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

namespace sonic4ep2::d3d9 {

enum class DeviceStatus : std::uint8_t {
    Ready,
    Lost,
    NotReset,
};

enum class DepthStencilFormat : std::uint8_t {
    None,
    D16,
    D24S8,
};

struct AdapterInfo {
    std::uint32_t index;
    std::string description;
    std::int32_t caps_result;
    std::uint32_t vertex_shader_version;
    std::uint32_t pixel_shader_version;
    std::uint32_t max_vertex_shader_constants;
    bool hardware_transform_and_light;
};

struct DeviceAttempt {
    std::uint32_t behavior_flags;
    std::int32_t result;
    bool software_vertex_processing;
};

struct DeviceDiagnostics {
    std::vector<AdapterInfo> adapters;
    std::vector<DeviceAttempt> attempts;
    std::uint32_t selected_adapter;
    bool used_software_vertex_processing;
};

struct DeviceCreateOptions {
    void* host_window;
    std::uint32_t adapter;
    std::uint32_t width;
    std::uint32_t height;
    DepthStencilFormat depth_stencil_format;
    bool enable_vsync;
    bool allow_software_vertex_processing;
};

struct ClearDescription {
    bool clear_color;
    std::uint32_t color_rgba;
    bool clear_depth;
    float depth;
    bool clear_stencil;
    std::uint32_t stencil;
};

struct ReadbackImage {
    std::uint32_t width;
    std::uint32_t height;
    std::vector<std::uint8_t> rgba;
};

class D3d9Error final : public std::runtime_error {
public:
    D3d9Error(std::string message, std::int32_t result);

    std::int32_t result() const noexcept;

private:
    std::int32_t result_;
};

class D3d9Device final {
public:
    // The caller serializes device and renderer access on one render thread.
    static std::vector<AdapterInfo> enumerate_adapters();
    static std::unique_ptr<D3d9Device> create(
        const DeviceCreateOptions& options, DeviceDiagnostics* diagnostics = nullptr);

    ~D3d9Device();

    D3d9Device(const D3d9Device&) = delete;
    D3d9Device& operator=(const D3d9Device&) = delete;
    D3d9Device(D3d9Device&&) noexcept;
    D3d9Device& operator=(D3d9Device&&) noexcept;

    DeviceStatus cooperative_status() const;
    void resize(std::uint32_t width, std::uint32_t height);
    void begin_frame(const ClearDescription& clear);
    void end_frame();
    void present();
    ReadbackImage readback_current_target() const;

    std::uint32_t width() const noexcept;
    std::uint32_t height() const noexcept;
    const DeviceDiagnostics& diagnostics() const noexcept;

private:
    struct Implementation;

    explicit D3d9Device(std::shared_ptr<Implementation> implementation);

    static void* native_device(const std::shared_ptr<Implementation>& implementation) noexcept;
    static bool scene_active(const std::shared_ptr<Implementation>& implementation) noexcept;
    static void retain_default_pool_resource(const std::shared_ptr<Implementation>& implementation);
    static void release_default_pool_resource(const std::shared_ptr<Implementation>& implementation) noexcept;

    std::shared_ptr<Implementation> implementation_;

    friend class D3d9Renderer;
    friend class D3d9VertexBuffer;
    friend class D3d9IndexBuffer;
    friend class D3d9Texture;
};

}
