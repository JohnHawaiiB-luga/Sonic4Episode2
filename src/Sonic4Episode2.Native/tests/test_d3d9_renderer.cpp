#include "../windows/d3d9_renderer.h"

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <d3dcompiler.h>

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <exception>
#include <iostream>
#include <limits>
#include <memory>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace {

using namespace sonic4ep2::d3d9;

struct Rgba {
    std::uint8_t red;
    std::uint8_t green;
    std::uint8_t blue;
    std::uint8_t alpha;
};

struct Vertex {
    float position[3];
    float uv[2];
    Rgba color;
};

static_assert(sizeof(Vertex) == 24u);

const std::array<float, 4u> kVertexOffset = {{0.0f, 0.0f, 0.0f, 0.0f}};
const std::array<float, 4u> kPixelTint = {{1.0f, 1.0f, 1.0f, 1.0f}};

class Blob final {
public:
    explicit Blob(ID3DBlob* blob = nullptr)
        : blob_(blob) {
    }

    ~Blob() {
        if (blob_ != nullptr) {
            blob_->Release();
        }
    }

    Blob(const Blob&) = delete;
    Blob& operator=(const Blob&) = delete;

    Blob(Blob&& other) noexcept
        : blob_(other.blob_) {
        other.blob_ = nullptr;
    }

    Blob& operator=(Blob&& other) noexcept {
        if (this != &other) {
            if (blob_ != nullptr) {
                blob_->Release();
            }
            blob_ = other.blob_;
            other.blob_ = nullptr;
        }
        return *this;
    }

    ID3DBlob* get() const noexcept {
        return blob_;
    }

private:
    ID3DBlob* blob_ = nullptr;
};

void check(bool condition, const char* message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

template <typename Callable>
void expect_throw(Callable&& callable, const char* message) {
    try {
        callable();
    } catch (const std::exception&) {
        return;
    }
    throw std::runtime_error(message);
}

template <typename ExpectedException, typename Callable>
void expect_throw_contains(Callable&& callable, const char* expected, const char* message) {
    try {
        callable();
    } catch (const ExpectedException& exception) {
        if (std::string(exception.what()).find(expected) != std::string::npos) {
            return;
        }
        throw std::runtime_error(message);
    } catch (const std::exception&) {
        throw std::runtime_error(message);
    }
    throw std::runtime_error(message);
}

std::uint32_t shader_major(std::uint32_t version) noexcept {
    return (version >> 8u) & 0xffu;
}

std::vector<std::uint8_t> compile_shader(const char* entry, const char* target) {
    static constexpr const char kSource[] = R"(
struct VertexInput {
    float3 position : POSITION0;
    float2 uv : TEXCOORD0;
    float4 color : COLOR0;
};

struct VertexOutput {
    float4 position : POSITION0;
    float2 uv : TEXCOORD0;
    float4 color : COLOR0;
};

float4 vs_offset : register(c0);

VertexOutput vs_main(VertexInput input) {
    VertexOutput output;
    output.position = float4(input.position, 1.0f) + vs_offset;
    output.uv = input.uv;
    output.color = input.color;
    return output;
}

sampler test_texture : register(s0);
float4 ps_tint : register(c0);

float4 ps_color(VertexOutput input) : COLOR0 {
    return input.color * ps_tint;
}

float4 ps_texture(VertexOutput input) : COLOR0 {
    return tex2D(test_texture, input.uv) * input.color * ps_tint;
}

float4 ps_texture_mip(VertexOutput input) : COLOR0 {
    return tex2Dlod(test_texture, float4(input.uv, 0.0f, 1.0f)) * input.color * ps_tint;
}
)";

    ID3DBlob* bytecode = nullptr;
    ID3DBlob* errors = nullptr;
    const HRESULT result = D3DCompile(
        kSource,
        sizeof(kSource) - 1u,
        "d3d9_renderer_test.hlsl",
        nullptr,
        nullptr,
        entry,
        target,
        D3DCOMPILE_ENABLE_STRICTNESS,
        0u,
        &bytecode,
        &errors);
    Blob error_blob(errors);
    if (FAILED(result)) {
        std::string message = "D3DCompile failed";
        if (error_blob.get() != nullptr) {
            const auto* text = static_cast<const char*>(error_blob.get()->GetBufferPointer());
            message.append(": ").append(text, error_blob.get()->GetBufferSize());
        }
        throw std::runtime_error(message);
    }

    Blob bytecode_blob(bytecode);
    std::vector<std::uint8_t> result_bytes(bytecode_blob.get()->GetBufferSize());
    std::memcpy(result_bytes.data(), bytecode_blob.get()->GetBufferPointer(), result_bytes.size());
    return result_bytes;
}

std::array<Vertex, 4u> make_fullscreen_vertices(Rgba color, float depth) {
    return {{
        {{-1.0f, -1.0f, depth}, {0.0f, 1.0f}, color},
        {{-1.0f, 1.0f, depth}, {0.0f, 0.0f}, color},
        {{1.0f, 1.0f, depth}, {1.0f, 0.0f}, color},
        {{1.0f, -1.0f, depth}, {1.0f, 1.0f}, color},
    }};
}

ByteView byte_view(const void* data, std::size_t size) {
    return ByteView{static_cast<const std::uint8_t*>(data), size};
}

std::vector<std::uint8_t> dds_fixture(
    std::uint32_t fourcc,
    std::uint32_t mip_count,
    const std::vector<std::uint8_t>& payload) {
    std::vector<std::uint8_t> bytes(128u, 0u);
    const auto put = [&](std::size_t offset, std::uint32_t value) {
        for (unsigned index = 0u; index < 4u; ++index) {
            bytes.at(offset + index) = static_cast<std::uint8_t>(value >> (index * 8u));
        }
    };
    put(0u, 0x20534444u);
    put(4u, 124u);
    put(8u, 0x1007u);
    put(12u, 4u);
    put(16u, 4u);
    put(28u, mip_count);
    put(76u, 32u);
    put(80u, 4u);
    put(84u, fourcc);
    put(108u, 0x1000u);
    bytes.insert(bytes.end(), payload.begin(), payload.end());
    return bytes;
}

RenderStateDescription render_state() {
    RenderStateDescription state{};
    state.blend = BlendDescription{
        false,
        BlendFactor::One,
        BlendFactor::Zero,
        BlendOperation::Add,
        false,
        BlendFactor::One,
        BlendFactor::Zero,
        BlendOperation::Add,
        0u,
    };
    state.depth_stencil = DepthStencilDescription{
        true,
        true,
        CompareFunction::LessEqual,
        false,
        0u,
        0xffu,
        0xffu,
        StencilFaceDescription{
            CompareFunction::Always,
            StencilOperation::Keep,
            StencilOperation::Keep,
            StencilOperation::Keep,
        },
        StencilFaceDescription{
            CompareFunction::Always,
            StencilOperation::Keep,
            StencilOperation::Keep,
            StencilOperation::Keep,
        },
        false,
    };
    state.alpha_test = AlphaTestDescription{false, CompareFunction::Always, 0u};
    state.color_write_mask = 0x0fu;
    state.rasterizer = RasterizerDescription{
        CullMode::None,
        FillMode::Solid,
        false,
        false,
        false,
        false,
        ScissorRectangle{0, 0, 0, 0},
    };
    return state;
}

SamplerDescription point_sampler() {
    return SamplerDescription{
        TextureAddressMode::Clamp,
        TextureAddressMode::Clamp,
        TextureAddressMode::Clamp,
        TextureFilter::Point,
        TextureFilter::Point,
        TextureFilter::Point,
        1u,
        0u,
        0.0f,
        0u,
        false,
    };
}

SamplerDescription sampler_with_sentinel_state() {
    return SamplerDescription{
        TextureAddressMode::MirrorOnce,
        TextureAddressMode::Border,
        TextureAddressMode::Wrap,
        TextureFilter::Anisotropic,
        TextureFilter::None,
        TextureFilter::Linear,
        11u,
        7u,
        1.25f,
        0x12345678u,
        true,
    };
}

void check_unrelated_sampler_state(
    const SamplerDescription& actual,
    const SamplerDescription& expected,
    const char* message) {
    check(actual.address_u == expected.address_u
            && actual.address_v == expected.address_v
            && actual.address_w == expected.address_w
            && actual.max_mip_level == expected.max_mip_level
            && actual.mip_lod_bias == expected.mip_lod_bias
            && actual.border_color_rgba == expected.border_color_rgba
            && actual.srgb_texture == expected.srgb_texture,
        message);
}

void check_sampler_state(const SamplerDescription& actual, const SamplerDescription& expected, const char* message) {
    check(actual.address_u == expected.address_u
            && actual.address_v == expected.address_v
            && actual.address_w == expected.address_w
            && actual.min_filter == expected.min_filter
            && actual.mag_filter == expected.mag_filter
            && actual.mip_filter == expected.mip_filter
            && actual.max_anisotropy == expected.max_anisotropy
            && actual.max_mip_level == expected.max_mip_level
            && actual.mip_lod_bias == expected.mip_lod_bias
            && actual.border_color_rgba == expected.border_color_rgba
            && actual.srgb_texture == expected.srgb_texture,
        message);
}

void run_txb_filter_mode_tests() {
    struct MinimumExpectation {
        TextureFilter min_filter;
        TextureFilter mip_filter;
        std::uint32_t max_anisotropy;
    };
    static constexpr std::array<MinimumExpectation, 15u> kMinimumExpectations = {{
        {TextureFilter::Point, TextureFilter::None, 4u},
        {TextureFilter::Linear, TextureFilter::None, 1u},
        {TextureFilter::Point, TextureFilter::Point, 1u},
        {TextureFilter::Point, TextureFilter::Linear, 1u},
        {TextureFilter::Linear, TextureFilter::Point, 1u},
        {TextureFilter::Linear, TextureFilter::Linear, 1u},
        {TextureFilter::Anisotropic, TextureFilter::None, 2u},
        {TextureFilter::Anisotropic, TextureFilter::Point, 2u},
        {TextureFilter::Anisotropic, TextureFilter::Linear, 2u},
        {TextureFilter::Anisotropic, TextureFilter::None, 4u},
        {TextureFilter::Anisotropic, TextureFilter::Point, 4u},
        {TextureFilter::Anisotropic, TextureFilter::Linear, 4u},
        {TextureFilter::Point, TextureFilter::None, 4u},
        {TextureFilter::Point, TextureFilter::None, 4u},
        {TextureFilter::Point, TextureFilter::None, 4u},
    }};
    static constexpr std::array<TextureFilter, 3u> kMagnificationExpectations = {{
        TextureFilter::Point,
        TextureFilter::Linear,
        TextureFilter::Anisotropic,
    }};

    for (std::uint16_t minimum = 0u; minimum < kMinimumExpectations.size(); ++minimum) {
        for (std::uint16_t magnification = 0u; magnification < kMagnificationExpectations.size(); ++magnification) {
            const SamplerDescription original = sampler_with_sentinel_state();
            SamplerDescription description = original;
            set_txb_filter_modes(description, minimum, magnification);

            const MinimumExpectation& expected_minimum = kMinimumExpectations[minimum];
            check(description.min_filter == expected_minimum.min_filter
                    && description.mag_filter == kMagnificationExpectations[magnification]
                    && description.mip_filter == expected_minimum.mip_filter
                    && description.max_anisotropy == expected_minimum.max_anisotropy,
                "TXB filter conversion did not reproduce a valid compact filter pair.");
            check_unrelated_sampler_state(
                description,
                original,
                "TXB filter conversion changed an unrelated sampler field.");
        }
    }

    struct CorpusExpectation {
        std::uint16_t minimum;
        std::uint16_t magnification;
        TextureFilter min_filter;
        TextureFilter mag_filter;
        TextureFilter mip_filter;
        std::uint32_t max_anisotropy;
    };
    static constexpr std::array<CorpusExpectation, 4u> kCorpusExpectations = {{
        {0u, 0u, TextureFilter::Point, TextureFilter::Point, TextureFilter::None, 4u},
        {1u, 1u, TextureFilter::Linear, TextureFilter::Linear, TextureFilter::None, 1u},
        {4u, 1u, TextureFilter::Linear, TextureFilter::Linear, TextureFilter::Point, 1u},
        {5u, 1u, TextureFilter::Linear, TextureFilter::Linear, TextureFilter::Linear, 1u},
    }};
    for (const CorpusExpectation& expected : kCorpusExpectations) {
        SamplerDescription description = sampler_with_sentinel_state();
        set_txb_filter_modes(description, expected.minimum, expected.magnification);
        check(description.min_filter == expected.min_filter
                && description.mag_filter == expected.mag_filter
                && description.mip_filter == expected.mip_filter
                && description.max_anisotropy == expected.max_anisotropy,
            "TXB filter conversion did not reproduce a corpus filter pair.");
    }

    const SamplerDescription original = sampler_with_sentinel_state();
    SamplerDescription invalid_minimum = original;
    expect_throw_contains<std::out_of_range>(
        [&] {
            set_txb_filter_modes(invalid_minimum, 15u, 2u);
        },
        "minimum",
        "TXB filter conversion accepted an out-of-range minimum filter ordinal.");
    check_sampler_state(
        invalid_minimum,
        original,
        "TXB filter conversion partially mutated a rejected minimum filter ordinal.");

    SamplerDescription invalid_magnification = original;
    expect_throw_contains<std::out_of_range>(
        [&] {
            set_txb_filter_modes(invalid_magnification, 14u, 3u);
        },
        "magnification",
        "TXB filter conversion accepted an out-of-range magnification filter ordinal.");
    check_sampler_state(
        invalid_magnification,
        original,
        "TXB filter conversion partially mutated a rejected magnification filter ordinal.");
}

DrawSubmission draw_submission(
    const D3d9VertexDeclaration& declaration,
    const D3d9VertexBuffer& vertex_buffer,
    const D3d9IndexBuffer& index_buffer,
    const D3d9VertexShader& vertex_shader,
    const D3d9PixelShader& pixel_shader,
    RenderStateDescription state,
    std::vector<TextureBinding> textures = {}) {
    DrawSubmission submission{
        &declaration,
        {{0u, &vertex_buffer, 0u}},
        &index_buffer,
        &vertex_shader,
        &pixel_shader,
        std::move(textures),
        ShaderConstantBindings{},
        state,
        PrimitiveTopology::TriangleList,
        0,
        0u,
        4u,
        0u,
        2u,
    };
    submission.constants.vertex_float4 = {{Float4Constants{0u, kVertexOffset.data(), 1u}}};
    submission.constants.pixel_float4 = {{Float4Constants{0u, kPixelTint.data(), 1u}}};
    return submission;
}

DrawSubmission nonindexed_draw_submission(
    const D3d9VertexDeclaration& declaration,
    const D3d9VertexBuffer& vertex_buffer,
    const D3d9VertexShader& vertex_shader,
    const D3d9PixelShader& pixel_shader,
    RenderStateDescription state,
    std::uint32_t first_vertex,
    std::uint32_t stream_offset) {
    DrawSubmission submission{
        &declaration,
        {{0u, &vertex_buffer, stream_offset}},
        nullptr,
        &vertex_shader,
        &pixel_shader,
        {},
        ShaderConstantBindings{},
        state,
        PrimitiveTopology::TriangleList,
        0,
        0u,
        0u,
        0u,
        2u,
        DrawSubmissionMode::NonIndexed,
        first_vertex,
    };
    submission.constants.vertex_float4 = {{Float4Constants{0u, kVertexOffset.data(), 1u}}};
    submission.constants.pixel_float4 = {{Float4Constants{0u, kPixelTint.data(), 1u}}};
    return submission;
}

std::array<std::uint8_t, 4u> pixel_at(const ReadbackImage& image, std::uint32_t x, std::uint32_t y) {
    check(x < image.width && y < image.height, "Readback pixel is outside the image.");
    const std::size_t offset = (static_cast<std::size_t>(y) * image.width + x) * 4u;
    return {{image.rgba[offset], image.rgba[offset + 1u], image.rgba[offset + 2u], image.rgba[offset + 3u]}};
}

bool channel_matches(std::uint8_t value, std::uint8_t expected, std::uint8_t tolerance = 3u) {
    return std::abs(static_cast<int>(value) - static_cast<int>(expected)) <= tolerance;
}

void run_renderer_test(std::uint32_t adapter) {
    DeviceDiagnostics diagnostics{};
    auto device = D3d9Device::create(
        DeviceCreateOptions{nullptr, adapter, 64u, 64u, DepthStencilFormat::D24S8, false, true},
        &diagnostics);
    check(device->cooperative_status() == DeviceStatus::Ready, "New D3D9 device is not ready.");
    check(!diagnostics.attempts.empty(), "D3D9 device diagnostics did not record CreateDevice attempts.");

    D3d9Renderer renderer(*device);
    expect_throw(
        [&] {
            renderer.create_vertex_buffer(VertexBufferDescription{ByteView{nullptr, 0u}, sizeof(Vertex), false});
        },
        "Invalid empty vertex buffer description was accepted.");
    expect_throw(
        [&] {
            renderer.create_vertex_declaration(VertexDeclarationDescription{{
                VertexElementDescription{0u, 0u, VertexElementType::Float3, VertexElementUsage::Position, 0u},
                VertexElementDescription{0u, 0u, VertexElementType::Float2, VertexElementUsage::TextureCoordinate, 0u},
            }});
        },
        "Duplicate vertex declaration offsets were accepted.");

    const std::array<Vertex, 4u> red_vertices = make_fullscreen_vertices(Rgba{255u, 0u, 0u, 255u}, 0.5f);
    const std::array<Vertex, 4u> half_green_vertices = make_fullscreen_vertices(Rgba{0u, 255u, 0u, 128u}, 0.5f);
    const std::array<Vertex, 4u> blue_vertices = make_fullscreen_vertices(Rgba{0u, 0u, 255u, 255u}, 0.25f);
    const std::array<Vertex, 4u> far_red_vertices = make_fullscreen_vertices(Rgba{255u, 0u, 0u, 255u}, 0.75f);
    const std::array<Vertex, 4u> alpha_blue_vertices = make_fullscreen_vertices(Rgba{0u, 0u, 255u, 64u}, 0.5f);
    const std::array<Vertex, 4u> white_vertices = make_fullscreen_vertices(Rgba{255u, 255u, 255u, 255u}, 0.5f);
    const std::array<Vertex, 10u> nonindexed_vertices = {{
        {{-1.0f, -1.0f, 0.5f}, {0.0f, 1.0f}, Rgba{0u, 255u, 0u, 255u}},
        {{-1.0f, -1.0f, 0.5f}, {0.0f, 1.0f}, Rgba{0u, 255u, 0u, 255u}},
        {{-1.0f, -1.0f, 0.5f}, {0.0f, 1.0f}, Rgba{0u, 255u, 0u, 255u}},
        {{-1.0f, -1.0f, 0.5f}, {0.0f, 1.0f}, Rgba{0u, 255u, 0u, 255u}},
        {{-1.0f, -1.0f, 0.5f}, {0.0f, 1.0f}, Rgba{255u, 128u, 0u, 255u}},
        {{-1.0f, 1.0f, 0.5f}, {0.0f, 0.0f}, Rgba{255u, 128u, 0u, 255u}},
        {{1.0f, 1.0f, 0.5f}, {1.0f, 0.0f}, Rgba{255u, 128u, 0u, 255u}},
        {{-1.0f, -1.0f, 0.5f}, {0.0f, 1.0f}, Rgba{255u, 128u, 0u, 255u}},
        {{1.0f, 1.0f, 0.5f}, {1.0f, 0.0f}, Rgba{255u, 128u, 0u, 255u}},
        {{1.0f, -1.0f, 0.5f}, {1.0f, 1.0f}, Rgba{255u, 128u, 0u, 255u}},
    }};
    const std::array<Vertex, 4u> nonindexed_strip_vertices = {{
        {{-1.0f, -1.0f, 0.5f}, {0.0f, 1.0f}, Rgba{0u, 192u, 255u, 255u}},
        {{-1.0f, 1.0f, 0.5f}, {0.0f, 0.0f}, Rgba{0u, 192u, 255u, 255u}},
        {{1.0f, -1.0f, 0.5f}, {1.0f, 1.0f}, Rgba{0u, 192u, 255u, 255u}},
        {{1.0f, 1.0f, 0.5f}, {1.0f, 0.0f}, Rgba{0u, 192u, 255u, 255u}},
    }};
    const std::array<std::uint16_t, 6u> indices = {{0u, 1u, 2u, 0u, 2u, 3u}};

    auto declaration = renderer.create_vertex_declaration(VertexDeclarationDescription{{
        VertexElementDescription{0u, 0u, VertexElementType::Float3, VertexElementUsage::Position, 0u},
        VertexElementDescription{0u, 12u, VertexElementType::Float2, VertexElementUsage::TextureCoordinate, 0u},
        VertexElementDescription{0u, 20u, VertexElementType::Color, VertexElementUsage::Color, 0u},
    }});
    auto red_buffer = renderer.create_vertex_buffer(VertexBufferDescription{byte_view(red_vertices.data(), sizeof(red_vertices)), sizeof(Vertex), false});
    auto half_green_buffer = renderer.create_vertex_buffer(VertexBufferDescription{byte_view(half_green_vertices.data(), sizeof(half_green_vertices)), sizeof(Vertex), false});
    auto blue_buffer = renderer.create_vertex_buffer(VertexBufferDescription{byte_view(blue_vertices.data(), sizeof(blue_vertices)), sizeof(Vertex), false});
    auto far_red_buffer = renderer.create_vertex_buffer(VertexBufferDescription{byte_view(far_red_vertices.data(), sizeof(far_red_vertices)), sizeof(Vertex), false});
    auto alpha_blue_buffer = renderer.create_vertex_buffer(VertexBufferDescription{byte_view(alpha_blue_vertices.data(), sizeof(alpha_blue_vertices)), sizeof(Vertex), false});
    auto white_buffer = renderer.create_vertex_buffer(VertexBufferDescription{byte_view(white_vertices.data(), sizeof(white_vertices)), sizeof(Vertex), false});
    auto nonindexed_buffer = renderer.create_vertex_buffer(VertexBufferDescription{byte_view(nonindexed_vertices.data(), sizeof(nonindexed_vertices)), sizeof(Vertex), false});
    auto nonindexed_strip_buffer = renderer.create_vertex_buffer(VertexBufferDescription{byte_view(nonindexed_strip_vertices.data(), sizeof(nonindexed_strip_vertices)), sizeof(Vertex), false});
    auto index_buffer = renderer.create_index_buffer(IndexBufferDescription{byte_view(indices.data(), sizeof(indices)), IndexFormat::UInt16, false});

    const std::vector<std::uint8_t> vertex_shader_bytecode = compile_shader("vs_main", "vs_3_0");
    const std::vector<std::uint8_t> color_shader_bytecode = compile_shader("ps_color", "ps_3_0");
    const std::vector<std::uint8_t> texture_shader_bytecode = compile_shader("ps_texture", "ps_3_0");
    auto vertex_shader = renderer.create_vertex_shader(byte_view(vertex_shader_bytecode.data(), vertex_shader_bytecode.size()));
    auto color_shader = renderer.create_pixel_shader(byte_view(color_shader_bytecode.data(), color_shader_bytecode.size()));
    auto texture_shader = renderer.create_pixel_shader(byte_view(texture_shader_bytecode.data(), texture_shader_bytecode.size()));

    const std::array<std::uint8_t, 16u> texture_pixels = {{
        0u, 0u, 255u, 255u,
        0u, 255u, 0u, 255u,
        0u, 0u, 255u, 255u,
        0u, 255u, 0u, 255u,
    }};
    auto texture = renderer.create_texture(TextureDescription{
        2u,
        2u,
        TextureFormat::A8R8G8B8,
        {{TextureMipDescription{byte_view(texture_pixels.data(), texture_pixels.size()), 8u}}},
    });
    expect_throw(
        [&] {
            renderer.create_texture(TextureDescription{
                2u,
                2u,
                TextureFormat::A8R8G8B8,
                {{TextureMipDescription{byte_view(texture_pixels.data(), texture_pixels.size() - 1u), 8u}}},
            });
        },
        "Truncated texture mip description was accepted.");

    auto dynamic_buffer = renderer.create_vertex_buffer(VertexBufferDescription{byte_view(white_vertices.data(), sizeof(white_vertices)), sizeof(Vertex), true});
    expect_throw(
        [&] {
            device->resize(64u, 64u);
        },
        "D3D9 reset accepted an active default-pool resource.");
    dynamic_buffer.reset();
    device->resize(64u, 64u);
    check(device->width() == 64u && device->height() == 64u, "D3D9 resize did not preserve expected dimensions.");

    RenderStateDescription invalid_two_sided_state = render_state();
    invalid_two_sided_state.depth_stencil.two_sided_stencil_enable = true;
    invalid_two_sided_state.rasterizer.cull_mode = CullMode::Clockwise;
    device->begin_frame(ClearDescription{true, 0x000000ffu, true, 1.0f, true, 0u});
    expect_throw_contains<std::invalid_argument>(
        [&] {
            renderer.draw(draw_submission(
                *declaration,
                *red_buffer,
                *index_buffer,
                *vertex_shader,
                *color_shader,
                invalid_two_sided_state));
        },
        "CullMode::None",
        "Two-sided stencil mode accepted a non-none cull mode.");
    device->end_frame();

    device->begin_frame(ClearDescription{true, 0x000000ffu, true, 1.0f, true, 0u});
    expect_throw_contains<std::logic_error>(
        [&] {
            renderer.readback_current_target();
        },
        "End the D3D9 scene",
        "Renderer readback did not reject an active D3D9 scene.");
    device->end_frame();

    RenderStateDescription invalid_scissor_state = render_state();
    invalid_scissor_state.rasterizer.scissor_enable = true;
    invalid_scissor_state.rasterizer.scissor_rectangle = ScissorRectangle{0, 0, 0, 64};
    device->begin_frame(ClearDescription{true, 0x000000ffu, true, 1.0f, true, 0u});
    expect_throw_contains<std::invalid_argument>(
        [&] {
            renderer.draw(draw_submission(
                *declaration,
                *red_buffer,
                *index_buffer,
                *vertex_shader,
                *color_shader,
                invalid_scissor_state));
        },
        "scissor rectangle",
        "D3D9 accepted an empty scissor rectangle.");
    device->end_frame();

    RenderStateDescription scissor_state = render_state();
    scissor_state.rasterizer.scissor_enable = true;
    scissor_state.rasterizer.scissor_rectangle = ScissorRectangle{0, 0, 32, 64};
    device->begin_frame(ClearDescription{true, 0x000000ffu, true, 1.0f, true, 0u});
    renderer.draw(draw_submission(
        *declaration,
        *red_buffer,
        *index_buffer,
        *vertex_shader,
        *color_shader,
        scissor_state));
    device->end_frame();
    const ReadbackImage scissor_image = renderer.readback_current_target();
    const auto scissor_left_pixel = pixel_at(scissor_image, 16u, 32u);
    const auto scissor_right_pixel = pixel_at(scissor_image, 48u, 32u);
    check(scissor_left_pixel[0] > 245u && scissor_left_pixel[1] < 8u && scissor_left_pixel[2] < 8u,
        "D3D9 scissor state did not retain pixels inside the requested rectangle.");
    check(scissor_right_pixel[0] < 8u && scissor_right_pixel[1] < 8u && scissor_right_pixel[2] < 8u,
        "D3D9 scissor state did not reject pixels outside the requested rectangle.");

    RenderStateDescription state = render_state();
    device->begin_frame(ClearDescription{true, 0x000000ffu, true, 1.0f, true, 0u});
    renderer.draw(draw_submission(*declaration, *red_buffer, *index_buffer, *vertex_shader, *color_shader, state));
    state.blend.enabled = true;
    state.blend.source_color = BlendFactor::SourceAlpha;
    state.blend.destination_color = BlendFactor::InverseSourceAlpha;
    state.blend.source_alpha = BlendFactor::SourceAlpha;
    state.blend.destination_alpha = BlendFactor::InverseSourceAlpha;
    renderer.draw(draw_submission(*declaration, *half_green_buffer, *index_buffer, *vertex_shader, *color_shader, state));
    state.alpha_test = AlphaTestDescription{true, CompareFunction::Greater, 128u};
    renderer.draw(draw_submission(*declaration, *alpha_blue_buffer, *index_buffer, *vertex_shader, *color_shader, state));
    state.alpha_test.enabled = false;
    state.blend.enabled = false;
    state.color_write_mask = 0x04u;
    renderer.draw(draw_submission(*declaration, *blue_buffer, *index_buffer, *vertex_shader, *color_shader, state));
    device->end_frame();
    const ReadbackImage blend_image = renderer.readback_current_target();
    const auto blend_pixel = pixel_at(blend_image, 32u, 32u);
    check(channel_matches(blend_pixel[0], 127u) && channel_matches(blend_pixel[1], 128u) && channel_matches(blend_pixel[2], 255u),
        "D3D9 blend, alpha-test, or color-write state did not produce the expected pixel.");

    device->begin_frame(ClearDescription{true, 0x000000ffu, true, 1.0f, true, 0u});
    state = render_state();
    renderer.draw(draw_submission(*declaration, *far_red_buffer, *index_buffer, *vertex_shader, *color_shader, state));
    renderer.draw(draw_submission(*declaration, *blue_buffer, *index_buffer, *vertex_shader, *color_shader, state));
    device->end_frame();
    const ReadbackImage depth_image = device->readback_current_target();
    const auto depth_pixel = pixel_at(depth_image, 32u, 32u);
    check(depth_pixel[2] > 245u && depth_pixel[0] < 8u && depth_pixel[1] < 8u,
        "D3D9 depth state did not retain the nearer indexed draw.");

    device->begin_frame(ClearDescription{true, 0x000000ffu, true, 1.0f, true, 0u});
    state = render_state();
    renderer.draw(draw_submission(
        *declaration,
        *white_buffer,
        *index_buffer,
        *vertex_shader,
        *texture_shader,
        state,
        {{TextureBinding{0u, texture.get(), point_sampler()}}}));
    device->end_frame();
    const ReadbackImage texture_image = renderer.readback_current_target();
    const auto left_pixel = pixel_at(texture_image, 16u, 32u);
    const auto right_pixel = pixel_at(texture_image, 48u, 32u);
    check(left_pixel[0] > 245u && left_pixel[1] < 8u && left_pixel[2] < 8u,
        "D3D9 point-sampled texture did not preserve the left UV texel.");
    check(right_pixel[1] > 245u && right_pixel[0] < 8u && right_pixel[2] < 8u,
        "D3D9 point-sampled texture did not preserve the right UV texel.");

    const auto mip_shader_bytes = compile_shader("ps_texture_mip", "ps_3_0");
    auto mip_shader = renderer.create_pixel_shader(byte_view(mip_shader_bytes.data(), mip_shader_bytes.size()));
    const std::array<std::uint32_t, 3u> dds_formats{{0x31545844u, 0x33545844u, 0x35545844u}};
    const std::array<std::vector<std::uint8_t>, 3u> dds_payloads{{
        {0x00u, 0xf8u, 0x00u, 0x00u, 0u, 0u, 0u, 0u,
         0xe0u, 0x07u, 0x00u, 0x00u, 0u, 0u, 0u, 0u},
        {0x88u, 0x88u, 0x88u, 0x88u, 0x88u, 0x88u, 0x88u, 0x88u,
         0x1fu, 0x00u, 0x00u, 0x00u, 0u, 0u, 0u, 0u},
        {200u, 0u, 0u, 0u, 0u, 0u, 0u, 0u,
         0x00u, 0xf8u, 0x00u, 0x00u, 0u, 0u, 0u, 0u},
    }};
    const std::array<std::array<std::uint8_t, 3u>, 3u> expected_dds_pixels{{
        {{255u, 0u, 0u}}, {{0u, 0u, 136u}}, {{200u, 0u, 0u}},
    }};
    for (std::size_t format = 0u; format < dds_formats.size(); ++format) {
        auto dds = dds_fixture(dds_formats[format], format == 0u ? 2u : 0u, dds_payloads[format]);
        expect_throw_contains<std::invalid_argument>(
            [&] { renderer.create_dds_texture(byte_view(dds.data(), dds.size() - 1u)); },
            "DDS", "Truncated DDS texture was accepted.");
        auto compressed = renderer.create_dds_texture(byte_view(dds.data(), dds.size()));
        for (auto& value : dds) value = 0u;
        std::vector<std::uint8_t>().swap(dds);
        device->resize(64u, 64u);
        state = render_state();
        state.blend.enabled = true;
        state.blend.source_color = BlendFactor::SourceAlpha;
        state.blend.destination_color = BlendFactor::InverseSourceAlpha;
        auto sampler = point_sampler();
        sampler.mip_filter = TextureFilter::Point;
        device->begin_frame(ClearDescription{true, 0x000000ffu, true, 1.0f, true, 0u});
        renderer.draw(draw_submission(*declaration, *white_buffer, *index_buffer, *vertex_shader,
            *texture_shader, state, {{TextureBinding{0u, compressed.get(), sampler}}}));
        device->end_frame();
        const auto base_pixel = pixel_at(renderer.readback_current_target(), 32u, 32u);
        for (std::size_t channel = 0u; channel < 3u; ++channel) {
            check(channel_matches(base_pixel[channel], expected_dds_pixels[format][channel]),
                "DDS format, alpha, ownership, or reset handling produced the wrong base-level pixel.");
        }
        if (format == 0u) {
            device->begin_frame(ClearDescription{true, 0x000000ffu, true, 1.0f, true, 0u});
            renderer.draw(draw_submission(*declaration, *white_buffer, *index_buffer, *vertex_shader,
                *mip_shader, state, {{TextureBinding{0u, compressed.get(), sampler}}}));
            device->end_frame();
            const auto mip_pixel = pixel_at(renderer.readback_current_target(), 32u, 32u);
            check(mip_pixel[0] < 8u && mip_pixel[1] > 245u && mip_pixel[2] < 8u,
                "DDS second mip was not preserved by native texture creation.");
        }
    }

    DrawSubmission nonindexed_draw = nonindexed_draw_submission(
        *declaration,
        *nonindexed_buffer,
        *vertex_shader,
        *color_shader,
        render_state(),
        2u,
        static_cast<std::uint32_t>(sizeof(Vertex) * 2u));
    device->begin_frame(ClearDescription{true, 0x000000ffu, true, 1.0f, true, 0u});
    renderer.draw(nonindexed_draw);
    device->end_frame();
    const ReadbackImage nonindexed_image = renderer.readback_current_target();
    const auto nonindexed_top_right_pixel = pixel_at(nonindexed_image, 56u, 16u);
    const auto nonindexed_bottom_right_pixel = pixel_at(nonindexed_image, 56u, 48u);
    check(nonindexed_top_right_pixel[0] > 245u
            && channel_matches(nonindexed_top_right_pixel[1], 128u)
            && nonindexed_top_right_pixel[2] < 8u
            && nonindexed_bottom_right_pixel[0] > 245u
            && channel_matches(nonindexed_bottom_right_pixel[1], 128u)
            && nonindexed_bottom_right_pixel[2] < 8u,
        "D3D9 nonindexed drawing did not honor first vertex and stream byte offset.");

    DrawSubmission nonindexed_strip_draw = nonindexed_draw_submission(
        *declaration,
        *nonindexed_strip_buffer,
        *vertex_shader,
        *color_shader,
        render_state(),
        0u,
        0u);
    nonindexed_strip_draw.topology = PrimitiveTopology::TriangleStrip;
    device->begin_frame(ClearDescription{true, 0x000000ffu, true, 1.0f, true, 0u});
    renderer.draw(nonindexed_strip_draw);
    device->end_frame();
    const ReadbackImage nonindexed_strip_image = renderer.readback_current_target();
    const auto nonindexed_strip_first_triangle_pixel = pixel_at(nonindexed_strip_image, 16u, 48u);
    const auto nonindexed_strip_second_triangle_pixel = pixel_at(nonindexed_strip_image, 48u, 16u);
    check(nonindexed_strip_first_triangle_pixel[0] < 8u
            && channel_matches(nonindexed_strip_first_triangle_pixel[1], 192u)
            && nonindexed_strip_first_triangle_pixel[2] > 245u
            && nonindexed_strip_second_triangle_pixel[0] < 8u
            && channel_matches(nonindexed_strip_second_triangle_pixel[1], 192u)
            && nonindexed_strip_second_triangle_pixel[2] > 245u,
        "D3D9 nonindexed triangle strip did not draw both quad triangles.");

    device->begin_frame(ClearDescription{true, 0x000000ffu, true, 1.0f, true, 0u});
    renderer.draw(nonindexed_draw);
    renderer.draw(draw_submission(*declaration, *red_buffer, *index_buffer, *vertex_shader, *color_shader, render_state()));
    device->end_frame();
    const ReadbackImage indexed_after_nonindexed_image = renderer.readback_current_target();
    const auto indexed_after_nonindexed_pixel = pixel_at(indexed_after_nonindexed_image, 32u, 32u);
    check(indexed_after_nonindexed_pixel[0] > 245u
            && indexed_after_nonindexed_pixel[1] < 8u
            && indexed_after_nonindexed_pixel[2] < 8u,
        "D3D9 indexed drawing did not work after a nonindexed submission.");

    DrawSubmission nonindexed_out_of_range = nonindexed_draw;
    nonindexed_out_of_range.first_vertex = 3u;
    expect_throw_contains<std::out_of_range>(
        [&] {
            renderer.draw(nonindexed_out_of_range);
        },
        "vertex buffer",
        "Out-of-range nonindexed vertex addressing was accepted.");

    DrawSubmission nonindexed_overflow = nonindexed_draw;
    nonindexed_overflow.first_vertex = std::numeric_limits<std::uint32_t>::max();
    expect_throw_contains<std::out_of_range>(
        [&] {
            renderer.draw(nonindexed_overflow);
        },
        "first vertex",
        "Overflowing nonindexed vertex addressing was accepted.");

    DrawSubmission nonindexed_missing_stream = nonindexed_draw;
    nonindexed_missing_stream.vertex_streams.clear();
    expect_throw_contains<std::invalid_argument>(
        [&] {
            renderer.draw(nonindexed_missing_stream);
        },
        "no vertex streams",
        "Nonindexed drawing accepted a missing vertex stream.");

    DrawSubmission nonindexed_invalid_mode = nonindexed_draw;
    nonindexed_invalid_mode.mode = static_cast<DrawSubmissionMode>(0xffu);
    expect_throw_contains<std::invalid_argument>(
        [&] {
            renderer.draw(nonindexed_invalid_mode);
        },
        "mode",
        "D3D9 accepted an invalid draw submission mode.");

    DrawSubmission nonindexed_invalid_topology = nonindexed_draw;
    nonindexed_invalid_topology.topology = static_cast<PrimitiveTopology>(0xffu);
    expect_throw_contains<std::invalid_argument>(
        [&] {
            renderer.draw(nonindexed_invalid_topology);
        },
        "topology",
        "D3D9 accepted an invalid nonindexed topology.");

    DrawSubmission invalid_draw = draw_submission(
        *declaration,
        *white_buffer,
        *index_buffer,
        *vertex_shader,
        *color_shader,
        render_state());
    invalid_draw.start_index = 5u;
    expect_throw(
        [&] {
            renderer.draw(invalid_draw);
        },
        "Out-of-range indexed draw submission was accepted.");
}

} // namespace

int main() {
    try {
        run_txb_filter_mode_tests();

        std::vector<AdapterInfo> adapters;
        try {
            adapters = D3d9Device::enumerate_adapters();
        } catch (const std::exception& exception) {
            std::cout << "SKIP: D3D9 adapter query unavailable: " << exception.what() << '\n';
            return 77;
        }

        check(!compile_shader("vs_main", "vs_3_0").empty(), "Controlled vertex shader compilation produced no bytecode.");
        check(!compile_shader("ps_color", "ps_3_0").empty(), "Controlled color shader compilation produced no bytecode.");
        check(!compile_shader("ps_texture", "ps_3_0").empty(), "Controlled texture shader compilation produced no bytecode.");

        std::uint32_t adapter = 0u;
        bool found_shader_model_3 = false;
        for (const AdapterInfo& candidate : adapters) {
            std::cout << "Adapter " << candidate.index
                      << ": caps=" << candidate.caps_result
                      << " vs=0x" << std::hex << candidate.vertex_shader_version
                      << " ps=0x" << candidate.pixel_shader_version << std::dec << '\n';
            if (candidate.caps_result == 0
                && shader_major(candidate.vertex_shader_version) >= 3u
                && shader_major(candidate.pixel_shader_version) >= 3u) {
                adapter = candidate.index;
                found_shader_model_3 = true;
                break;
            }
        }
        if (!found_shader_model_3) {
            std::cout << "SKIP: no D3D9 Shader Model 3 adapter is available.\n";
            return 77;
        }

        run_renderer_test(adapter);
        std::cout << "D3D9 renderer regression test passed.\n";
        return 0;
    } catch (const std::exception& exception) {
        std::cerr << "D3D9 renderer regression test failed: " << exception.what() << '\n';
        return 1;
    }
}
