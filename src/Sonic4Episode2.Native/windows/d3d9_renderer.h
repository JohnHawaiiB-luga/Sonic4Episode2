#pragma once

#include "d3d9_device.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

namespace sonic4ep2::d3d9 {

struct ByteView {
    const std::uint8_t* data;
    std::size_t size;
};

enum class VertexElementType : std::uint8_t {
    Float1,
    Float2,
    Float3,
    Float4,
    Color, // Four normalized RGBA bytes.
    UByte4,
};

enum class VertexElementUsage : std::uint8_t {
    Position,
    BlendWeight,
    BlendIndices,
    Normal,
    PointSize,
    TextureCoordinate,
    Tangent,
    Binormal,
    TessellateFactor,
    PositionTransformed,
    Color,
    Fog,
    Depth,
    Sample,
};

struct VertexElementDescription {
    std::uint16_t stream;
    std::uint16_t offset;
    VertexElementType type;
    VertexElementUsage usage;
    std::uint8_t usage_index;
};

struct VertexDeclarationDescription {
    std::vector<VertexElementDescription> elements;
};

struct VertexBufferDescription {
    ByteView bytes;
    std::uint32_t stride;
    bool dynamic;
};

enum class IndexFormat : std::uint8_t {
    UInt16,
    UInt32,
};

struct IndexBufferDescription {
    ByteView bytes;
    IndexFormat format;
    bool dynamic;
};

enum class TextureFormat : std::uint8_t {
    A8R8G8B8,
    X8R8G8B8,
    A8B8G8R8,
    Dxt1,
    Dxt3,
    Dxt5,
};

struct TextureMipDescription {
    ByteView bytes;
    std::uint32_t row_pitch;
};

struct TextureDescription {
    std::uint32_t width;
    std::uint32_t height;
    TextureFormat format;
    std::vector<TextureMipDescription> mips;
};

enum class TextureAddressMode : std::uint8_t {
    Wrap,
    Mirror,
    Clamp,
    Border,
    MirrorOnce,
};

enum class TextureFilter : std::uint8_t {
    None,
    Point,
    Linear,
    Anisotropic,
};

struct SamplerDescription {
    TextureAddressMode address_u;
    TextureAddressMode address_v;
    TextureAddressMode address_w;
    TextureFilter min_filter;
    TextureFilter mag_filter;
    TextureFilter mip_filter;
    std::uint32_t max_anisotropy;
    std::uint32_t max_mip_level;
    float mip_lod_bias;
    std::uint32_t border_color_rgba;
    bool srgb_texture;
};

void set_txb_filter_modes(
    SamplerDescription& description,
    std::uint16_t minimum,
    std::uint16_t magnification);

enum class CompareFunction : std::uint8_t {
    Never,
    Less,
    Equal,
    LessEqual,
    Greater,
    NotEqual,
    GreaterEqual,
    Always,
};

enum class BlendFactor : std::uint8_t {
    Zero,
    One,
    SourceColor,
    InverseSourceColor,
    SourceAlpha,
    InverseSourceAlpha,
    DestinationAlpha,
    InverseDestinationAlpha,
    DestinationColor,
    InverseDestinationColor,
    SourceAlphaSaturate,
    BlendFactor,
    InverseBlendFactor,
};

enum class BlendOperation : std::uint8_t {
    Add,
    Subtract,
    ReverseSubtract,
    Min,
    Max,
};

enum class StencilOperation : std::uint8_t {
    Keep,
    Zero,
    Replace,
    IncrementSaturate,
    DecrementSaturate,
    Invert,
    Increment,
    Decrement,
};

enum class CullMode : std::uint8_t {
    None,
    Clockwise,
    CounterClockwise,
};

enum class FillMode : std::uint8_t {
    Point,
    Wireframe,
    Solid,
};

struct BlendDescription {
    bool enabled;
    BlendFactor source_color;
    BlendFactor destination_color;
    BlendOperation color_operation;
    bool separate_alpha;
    BlendFactor source_alpha;
    BlendFactor destination_alpha;
    BlendOperation alpha_operation;
    std::uint32_t blend_factor_rgba;
};

struct StencilFaceDescription {
    CompareFunction compare;
    StencilOperation fail;
    StencilOperation depth_fail;
    StencilOperation pass;
};

struct DepthStencilDescription {
    bool depth_enable;
    bool depth_write;
    CompareFunction depth_compare;
    bool stencil_enable;
    std::uint8_t stencil_reference;
    std::uint8_t stencil_read_mask;
    std::uint8_t stencil_write_mask;
    StencilFaceDescription clockwise;
    StencilFaceDescription counter_clockwise;
    bool two_sided_stencil_enable = false;
};

struct AlphaTestDescription {
    bool enabled;
    CompareFunction compare;
    std::uint8_t reference;
};

struct ScissorRectangle {
    std::int32_t left;
    std::int32_t top;
    std::int32_t right;
    std::int32_t bottom;
};

struct RasterizerDescription {
    CullMode cull_mode;
    FillMode fill_mode;
    bool scissor_enable;
    bool multisample_antialias;
    bool antialiased_line_enable;
    bool srgb_write_enable;
    ScissorRectangle scissor_rectangle{};
};

struct RenderStateDescription {
    BlendDescription blend;
    DepthStencilDescription depth_stencil;
    AlphaTestDescription alpha_test;
    std::uint8_t color_write_mask;
    RasterizerDescription rasterizer;
};

struct Float4Constants {
    std::uint32_t first_register;
    const float* values;
    std::uint32_t vector_count;
};

struct Int4Constants {
    std::uint32_t first_register;
    const std::int32_t* values;
    std::uint32_t vector_count;
};

struct BoolConstants {
    std::uint32_t first_register;
    const std::int32_t* values;
    std::uint32_t value_count;
};

struct ShaderConstantBindings {
    std::vector<Float4Constants> vertex_float4;
    std::vector<Int4Constants> vertex_int4;
    std::vector<BoolConstants> vertex_bool;
    std::vector<Float4Constants> pixel_float4;
    std::vector<Int4Constants> pixel_int4;
    std::vector<BoolConstants> pixel_bool;
};

class D3d9VertexDeclaration;
class D3d9VertexBuffer;
class D3d9IndexBuffer;
class D3d9Texture;
class D3d9VertexShader;
class D3d9PixelShader;

struct VertexStreamBinding {
    std::uint32_t stream;
    const D3d9VertexBuffer* buffer;
    std::uint32_t offset;
};

struct TextureBinding {
    std::uint32_t sampler;
    const D3d9Texture* texture;
    SamplerDescription sampler_state;
};

enum class PrimitiveTopology : std::uint8_t {
    PointList,
    LineList,
    LineStrip,
    TriangleList,
    TriangleStrip,
    TriangleFan,
};

enum class DrawSubmissionMode : std::uint8_t {
    Indexed,
    NonIndexed,
};

struct DrawSubmission {
    const D3d9VertexDeclaration* declaration;
    std::vector<VertexStreamBinding> vertex_streams;
    const D3d9IndexBuffer* index_buffer;
    const D3d9VertexShader* vertex_shader;
    const D3d9PixelShader* pixel_shader;
    std::vector<TextureBinding> textures;
    ShaderConstantBindings constants;
    RenderStateDescription render_state;
    PrimitiveTopology topology;
    std::int32_t base_vertex_index;
    std::uint32_t minimum_vertex_index;
    std::uint32_t vertex_count;
    std::uint32_t start_index;
    std::uint32_t primitive_count;
    DrawSubmissionMode mode = DrawSubmissionMode::Indexed;
    std::uint32_t first_vertex = 0u;
};

enum class TransformSlot : std::uint8_t {
    World,
    View,
    Projection,
};

class D3d9VertexDeclaration final {
public:
    ~D3d9VertexDeclaration();

    D3d9VertexDeclaration(const D3d9VertexDeclaration&) = delete;
    D3d9VertexDeclaration& operator=(const D3d9VertexDeclaration&) = delete;
    D3d9VertexDeclaration(D3d9VertexDeclaration&&) noexcept;
    D3d9VertexDeclaration& operator=(D3d9VertexDeclaration&&) noexcept;

private:
    struct Implementation;
    explicit D3d9VertexDeclaration(std::unique_ptr<Implementation> implementation);

    std::unique_ptr<Implementation> implementation_;

    friend class D3d9Renderer;
};

class D3d9VertexBuffer final {
public:
    ~D3d9VertexBuffer();

    D3d9VertexBuffer(const D3d9VertexBuffer&) = delete;
    D3d9VertexBuffer& operator=(const D3d9VertexBuffer&) = delete;
    D3d9VertexBuffer(D3d9VertexBuffer&&) noexcept;
    D3d9VertexBuffer& operator=(D3d9VertexBuffer&&) noexcept;

private:
    struct Implementation;
    explicit D3d9VertexBuffer(std::unique_ptr<Implementation> implementation);

    std::unique_ptr<Implementation> implementation_;

    friend class D3d9Renderer;
};

class D3d9IndexBuffer final {
public:
    ~D3d9IndexBuffer();

    D3d9IndexBuffer(const D3d9IndexBuffer&) = delete;
    D3d9IndexBuffer& operator=(const D3d9IndexBuffer&) = delete;
    D3d9IndexBuffer(D3d9IndexBuffer&&) noexcept;
    D3d9IndexBuffer& operator=(D3d9IndexBuffer&&) noexcept;

private:
    struct Implementation;
    explicit D3d9IndexBuffer(std::unique_ptr<Implementation> implementation);

    std::unique_ptr<Implementation> implementation_;

    friend class D3d9Renderer;
};

class D3d9Texture final {
public:
    ~D3d9Texture();

    D3d9Texture(const D3d9Texture&) = delete;
    D3d9Texture& operator=(const D3d9Texture&) = delete;
    D3d9Texture(D3d9Texture&&) noexcept;
    D3d9Texture& operator=(D3d9Texture&&) noexcept;

private:
    struct Implementation;
    explicit D3d9Texture(std::unique_ptr<Implementation> implementation);

    std::unique_ptr<Implementation> implementation_;

    friend class D3d9Renderer;
};

class D3d9VertexShader final {
public:
    ~D3d9VertexShader();

    D3d9VertexShader(const D3d9VertexShader&) = delete;
    D3d9VertexShader& operator=(const D3d9VertexShader&) = delete;
    D3d9VertexShader(D3d9VertexShader&&) noexcept;
    D3d9VertexShader& operator=(D3d9VertexShader&&) noexcept;

private:
    struct Implementation;
    explicit D3d9VertexShader(std::unique_ptr<Implementation> implementation);

    std::unique_ptr<Implementation> implementation_;

    friend class D3d9Renderer;
};

class D3d9PixelShader final {
public:
    ~D3d9PixelShader();

    D3d9PixelShader(const D3d9PixelShader&) = delete;
    D3d9PixelShader& operator=(const D3d9PixelShader&) = delete;
    D3d9PixelShader(D3d9PixelShader&&) noexcept;
    D3d9PixelShader& operator=(D3d9PixelShader&&) noexcept;

private:
    struct Implementation;
    explicit D3d9PixelShader(std::unique_ptr<Implementation> implementation);

    std::unique_ptr<Implementation> implementation_;

    friend class D3d9Renderer;
};

class D3d9Renderer final {
public:
    explicit D3d9Renderer(D3d9Device& device);
    ~D3d9Renderer();

    D3d9Renderer(const D3d9Renderer&) = delete;
    D3d9Renderer& operator=(const D3d9Renderer&) = delete;
    D3d9Renderer(D3d9Renderer&&) noexcept;
    D3d9Renderer& operator=(D3d9Renderer&&) noexcept;

    std::unique_ptr<D3d9VertexDeclaration> create_vertex_declaration(
        const VertexDeclarationDescription& description) const;
    std::unique_ptr<D3d9VertexBuffer> create_vertex_buffer(
        const VertexBufferDescription& description) const;
    std::unique_ptr<D3d9IndexBuffer> create_index_buffer(
        const IndexBufferDescription& description) const;
    std::unique_ptr<D3d9Texture> create_texture(const TextureDescription& description) const;
    std::unique_ptr<D3d9Texture> create_dds_texture(ByteView bytes) const;
    std::unique_ptr<D3d9VertexShader> create_vertex_shader(ByteView bytecode) const;
    std::unique_ptr<D3d9PixelShader> create_pixel_shader(ByteView bytecode) const;

    void set_transform(TransformSlot slot, const std::array<float, 16u>& row_major_matrix) const;
    void draw(const DrawSubmission& submission) const;
    ReadbackImage readback_current_target() const;

private:
    struct Implementation;

    std::unique_ptr<Implementation> implementation_;
};

}
