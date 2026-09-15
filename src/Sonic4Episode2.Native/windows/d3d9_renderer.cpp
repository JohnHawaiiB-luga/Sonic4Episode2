#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <d3d9.h>

#include "d3d9_renderer.h"
#include "dds_texture.h"

#include <algorithm>
#include <array>
#include <cstring>
#include <limits>
#include <stdexcept>
#include <utility>

namespace sonic4ep2::d3d9 {
namespace {

constexpr std::uint32_t kMaxStreams = 16u;
constexpr std::uint32_t kMaxPixelSamplers = 16u;
constexpr std::uint32_t kShaderIntegerRegisters = 16u;
constexpr std::uint32_t kShaderBooleanRegisters = 16u;

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

void check_result(HRESULT result, const char* operation) {
    if (FAILED(result)) {
        throw D3d9Error(operation, static_cast<std::int32_t>(result));
    }
}

std::uint32_t rgba_to_argb(std::uint32_t rgba) noexcept {
    const std::uint32_t red = (rgba >> 24u) & 0xffu;
    const std::uint32_t green = (rgba >> 16u) & 0xffu;
    const std::uint32_t blue = (rgba >> 8u) & 0xffu;
    const std::uint32_t alpha = rgba & 0xffu;
    return (alpha << 24u) | (red << 16u) | (green << 8u) | blue;
}

std::size_t vertex_element_size(VertexElementType type) {
    switch (type) {
    case VertexElementType::Float1:
        return 4u;
    case VertexElementType::Float2:
        return 8u;
    case VertexElementType::Float3:
        return 12u;
    case VertexElementType::Float4:
        return 16u;
    case VertexElementType::Color:
    case VertexElementType::UByte4:
        return 4u;
    }
    throw std::invalid_argument("Unknown D3D9 vertex element type.");
}

D3DDECLTYPE to_declaration_type(VertexElementType type) {
    switch (type) {
    case VertexElementType::Float1:
        return D3DDECLTYPE_FLOAT1;
    case VertexElementType::Float2:
        return D3DDECLTYPE_FLOAT2;
    case VertexElementType::Float3:
        return D3DDECLTYPE_FLOAT3;
    case VertexElementType::Float4:
        return D3DDECLTYPE_FLOAT4;
    case VertexElementType::Color:
        return D3DDECLTYPE_UBYTE4N;
    case VertexElementType::UByte4:
        return D3DDECLTYPE_UBYTE4;
    }
    throw std::invalid_argument("Unknown D3D9 vertex element type.");
}

D3DDECLUSAGE to_declaration_usage(VertexElementUsage usage) {
    switch (usage) {
    case VertexElementUsage::Position:
        return D3DDECLUSAGE_POSITION;
    case VertexElementUsage::BlendWeight:
        return D3DDECLUSAGE_BLENDWEIGHT;
    case VertexElementUsage::BlendIndices:
        return D3DDECLUSAGE_BLENDINDICES;
    case VertexElementUsage::Normal:
        return D3DDECLUSAGE_NORMAL;
    case VertexElementUsage::PointSize:
        return D3DDECLUSAGE_PSIZE;
    case VertexElementUsage::TextureCoordinate:
        return D3DDECLUSAGE_TEXCOORD;
    case VertexElementUsage::Tangent:
        return D3DDECLUSAGE_TANGENT;
    case VertexElementUsage::Binormal:
        return D3DDECLUSAGE_BINORMAL;
    case VertexElementUsage::TessellateFactor:
        return D3DDECLUSAGE_TESSFACTOR;
    case VertexElementUsage::PositionTransformed:
        return D3DDECLUSAGE_POSITIONT;
    case VertexElementUsage::Color:
        return D3DDECLUSAGE_COLOR;
    case VertexElementUsage::Fog:
        return D3DDECLUSAGE_FOG;
    case VertexElementUsage::Depth:
        return D3DDECLUSAGE_DEPTH;
    case VertexElementUsage::Sample:
        return D3DDECLUSAGE_SAMPLE;
    }
    throw std::invalid_argument("Unknown D3D9 vertex element usage.");
}

D3DFORMAT to_index_format(IndexFormat format) {
    switch (format) {
    case IndexFormat::UInt16:
        return D3DFMT_INDEX16;
    case IndexFormat::UInt32:
        return D3DFMT_INDEX32;
    }
    throw std::invalid_argument("Unknown D3D9 index format.");
}

std::size_t index_size(IndexFormat format) {
    switch (format) {
    case IndexFormat::UInt16:
        return 2u;
    case IndexFormat::UInt32:
        return 4u;
    }
    throw std::invalid_argument("Unknown D3D9 index format.");
}

D3DFORMAT to_texture_format(TextureFormat format) {
    switch (format) {
    case TextureFormat::A8R8G8B8:
        return D3DFMT_A8R8G8B8;
    case TextureFormat::X8R8G8B8:
        return D3DFMT_X8R8G8B8;
    case TextureFormat::A8B8G8R8:
        return D3DFMT_A8B8G8R8;
    case TextureFormat::Dxt1:
        return D3DFMT_DXT1;
    case TextureFormat::Dxt3:
        return D3DFMT_DXT3;
    case TextureFormat::Dxt5:
        return D3DFMT_DXT5;
    }
    throw std::invalid_argument("Unknown D3D9 texture format.");
}

bool is_block_compressed(TextureFormat format) noexcept {
    return format == TextureFormat::Dxt1 || format == TextureFormat::Dxt3 || format == TextureFormat::Dxt5;
}

std::uint32_t block_size(TextureFormat format) {
    switch (format) {
    case TextureFormat::Dxt1:
        return 8u;
    case TextureFormat::Dxt3:
    case TextureFormat::Dxt5:
        return 16u;
    default:
        throw std::invalid_argument("Texture format is not block-compressed.");
    }
}

struct TextureMipLayout {
    std::uint32_t row_pitch;
    std::uint32_t row_count;
};

TextureMipLayout texture_mip_layout(TextureFormat format, std::uint32_t width, std::uint32_t height) {
    const std::uint64_t rows = is_block_compressed(format)
        ? (static_cast<std::uint64_t>(height) + 3u) / 4u
        : height;
    const std::uint64_t row_pitch = is_block_compressed(format)
        ? ((static_cast<std::uint64_t>(width) + 3u) / 4u) * block_size(format)
        : static_cast<std::uint64_t>(width) * 4u;
    if (rows == 0u
        || row_pitch == 0u
        || rows > std::numeric_limits<std::uint32_t>::max()
        || row_pitch > std::numeric_limits<std::uint32_t>::max()) {
        throw std::overflow_error("D3D9 texture mip dimensions overflow their layout.");
    }
    return TextureMipLayout{static_cast<std::uint32_t>(row_pitch), static_cast<std::uint32_t>(rows)};
}

D3DTEXTUREADDRESS to_texture_address(TextureAddressMode mode) {
    switch (mode) {
    case TextureAddressMode::Wrap:
        return D3DTADDRESS_WRAP;
    case TextureAddressMode::Mirror:
        return D3DTADDRESS_MIRROR;
    case TextureAddressMode::Clamp:
        return D3DTADDRESS_CLAMP;
    case TextureAddressMode::Border:
        return D3DTADDRESS_BORDER;
    case TextureAddressMode::MirrorOnce:
        return D3DTADDRESS_MIRRORONCE;
    }
    throw std::invalid_argument("Unknown D3D9 texture address mode.");
}

D3DTEXTUREFILTERTYPE to_texture_filter(TextureFilter filter) {
    switch (filter) {
    case TextureFilter::None:
        return D3DTEXF_NONE;
    case TextureFilter::Point:
        return D3DTEXF_POINT;
    case TextureFilter::Linear:
        return D3DTEXF_LINEAR;
    case TextureFilter::Anisotropic:
        return D3DTEXF_ANISOTROPIC;
    }
    throw std::invalid_argument("Unknown D3D9 texture filter.");
}

D3DCMPFUNC to_compare_function(CompareFunction function) {
    switch (function) {
    case CompareFunction::Never:
        return D3DCMP_NEVER;
    case CompareFunction::Less:
        return D3DCMP_LESS;
    case CompareFunction::Equal:
        return D3DCMP_EQUAL;
    case CompareFunction::LessEqual:
        return D3DCMP_LESSEQUAL;
    case CompareFunction::Greater:
        return D3DCMP_GREATER;
    case CompareFunction::NotEqual:
        return D3DCMP_NOTEQUAL;
    case CompareFunction::GreaterEqual:
        return D3DCMP_GREATEREQUAL;
    case CompareFunction::Always:
        return D3DCMP_ALWAYS;
    }
    throw std::invalid_argument("Unknown D3D9 compare function.");
}

D3DBLEND to_blend_factor(BlendFactor factor) {
    switch (factor) {
    case BlendFactor::Zero:
        return D3DBLEND_ZERO;
    case BlendFactor::One:
        return D3DBLEND_ONE;
    case BlendFactor::SourceColor:
        return D3DBLEND_SRCCOLOR;
    case BlendFactor::InverseSourceColor:
        return D3DBLEND_INVSRCCOLOR;
    case BlendFactor::SourceAlpha:
        return D3DBLEND_SRCALPHA;
    case BlendFactor::InverseSourceAlpha:
        return D3DBLEND_INVSRCALPHA;
    case BlendFactor::DestinationAlpha:
        return D3DBLEND_DESTALPHA;
    case BlendFactor::InverseDestinationAlpha:
        return D3DBLEND_INVDESTALPHA;
    case BlendFactor::DestinationColor:
        return D3DBLEND_DESTCOLOR;
    case BlendFactor::InverseDestinationColor:
        return D3DBLEND_INVDESTCOLOR;
    case BlendFactor::SourceAlphaSaturate:
        return D3DBLEND_SRCALPHASAT;
    case BlendFactor::BlendFactor:
        return D3DBLEND_BLENDFACTOR;
    case BlendFactor::InverseBlendFactor:
        return D3DBLEND_INVBLENDFACTOR;
    }
    throw std::invalid_argument("Unknown D3D9 blend factor.");
}

D3DBLENDOP to_blend_operation(BlendOperation operation) {
    switch (operation) {
    case BlendOperation::Add:
        return D3DBLENDOP_ADD;
    case BlendOperation::Subtract:
        return D3DBLENDOP_SUBTRACT;
    case BlendOperation::ReverseSubtract:
        return D3DBLENDOP_REVSUBTRACT;
    case BlendOperation::Min:
        return D3DBLENDOP_MIN;
    case BlendOperation::Max:
        return D3DBLENDOP_MAX;
    }
    throw std::invalid_argument("Unknown D3D9 blend operation.");
}

D3DSTENCILOP to_stencil_operation(StencilOperation operation) {
    switch (operation) {
    case StencilOperation::Keep:
        return D3DSTENCILOP_KEEP;
    case StencilOperation::Zero:
        return D3DSTENCILOP_ZERO;
    case StencilOperation::Replace:
        return D3DSTENCILOP_REPLACE;
    case StencilOperation::IncrementSaturate:
        return D3DSTENCILOP_INCRSAT;
    case StencilOperation::DecrementSaturate:
        return D3DSTENCILOP_DECRSAT;
    case StencilOperation::Invert:
        return D3DSTENCILOP_INVERT;
    case StencilOperation::Increment:
        return D3DSTENCILOP_INCR;
    case StencilOperation::Decrement:
        return D3DSTENCILOP_DECR;
    }
    throw std::invalid_argument("Unknown D3D9 stencil operation.");
}

D3DCULL to_cull_mode(CullMode mode) {
    switch (mode) {
    case CullMode::None:
        return D3DCULL_NONE;
    case CullMode::Clockwise:
        return D3DCULL_CW;
    case CullMode::CounterClockwise:
        return D3DCULL_CCW;
    }
    throw std::invalid_argument("Unknown D3D9 cull mode.");
}

D3DFILLMODE to_fill_mode(FillMode mode) {
    switch (mode) {
    case FillMode::Point:
        return D3DFILL_POINT;
    case FillMode::Wireframe:
        return D3DFILL_WIREFRAME;
    case FillMode::Solid:
        return D3DFILL_SOLID;
    }
    throw std::invalid_argument("Unknown D3D9 fill mode.");
}

D3DPRIMITIVETYPE to_primitive_type(PrimitiveTopology topology) {
    switch (topology) {
    case PrimitiveTopology::PointList:
        return D3DPT_POINTLIST;
    case PrimitiveTopology::LineList:
        return D3DPT_LINELIST;
    case PrimitiveTopology::LineStrip:
        return D3DPT_LINESTRIP;
    case PrimitiveTopology::TriangleList:
        return D3DPT_TRIANGLELIST;
    case PrimitiveTopology::TriangleStrip:
        return D3DPT_TRIANGLESTRIP;
    case PrimitiveTopology::TriangleFan:
        return D3DPT_TRIANGLEFAN;
    }
    throw std::invalid_argument("Unknown D3D9 primitive topology.");
}

std::uint32_t required_index_count(PrimitiveTopology topology, std::uint32_t primitive_count) {
    if (primitive_count == 0u) {
        throw std::invalid_argument("D3D9 draw submission must contain at least one primitive.");
    }
    const auto checked_add = [](std::uint32_t value, std::uint32_t amount) {
        if (value > std::numeric_limits<std::uint32_t>::max() - amount) {
            throw std::overflow_error("D3D9 primitive count overflows its index range.");
        }
        return value + amount;
    };
    const auto checked_multiply = [](std::uint32_t value, std::uint32_t amount) {
        if (value > std::numeric_limits<std::uint32_t>::max() / amount) {
            throw std::overflow_error("D3D9 primitive count overflows its index range.");
        }
        return value * amount;
    };

    switch (topology) {
    case PrimitiveTopology::PointList:
        return primitive_count;
    case PrimitiveTopology::LineList:
        return checked_multiply(primitive_count, 2u);
    case PrimitiveTopology::LineStrip:
        return checked_add(primitive_count, 1u);
    case PrimitiveTopology::TriangleList:
        return checked_multiply(primitive_count, 3u);
    case PrimitiveTopology::TriangleStrip:
    case PrimitiveTopology::TriangleFan:
        return checked_add(primitive_count, 2u);
    }
    throw std::invalid_argument("Unknown D3D9 primitive topology.");
}

void validate_byte_view(ByteView view, const char* description) {
    if (view.data == nullptr || view.size == 0u) {
        throw std::invalid_argument(description);
    }
}

void validate_register_range(
    std::uint32_t first_register,
    std::uint32_t count,
    std::uint32_t limit,
    const char* description) {
    if (count == 0u) {
        return;
    }
    if (first_register >= limit || count > limit - first_register) {
        throw std::out_of_range(description);
    }
}

std::uint32_t pixel_float_constant_limit(const D3DCAPS9& caps) noexcept {
    const std::uint32_t major_version = D3DSHADER_VERSION_MAJOR(caps.PixelShaderVersion);
    if (major_version >= 3u) {
        return 224u;
    }
    if (major_version >= 2u) {
        return 32u;
    }
    return 8u;
}

void set_render_state(IDirect3DDevice9* device, D3DRENDERSTATETYPE state, DWORD value) {
    check_result(device->SetRenderState(state, value), "IDirect3DDevice9::SetRenderState failed");
}

void apply_stencil_face(
    IDirect3DDevice9* device,
    const StencilFaceDescription& description,
    D3DRENDERSTATETYPE compare,
    D3DRENDERSTATETYPE fail,
    D3DRENDERSTATETYPE depth_fail,
    D3DRENDERSTATETYPE pass) {
    set_render_state(device, compare, to_compare_function(description.compare));
    set_render_state(device, fail, to_stencil_operation(description.fail));
    set_render_state(device, depth_fail, to_stencil_operation(description.depth_fail));
    set_render_state(device, pass, to_stencil_operation(description.pass));
}

void apply_render_state(
    IDirect3DDevice9* device,
    const D3DCAPS9& caps,
    const RenderStateDescription& description) {
    if ((description.color_write_mask & ~0x0fu) != 0u) {
        throw std::invalid_argument("D3D9 color write mask must contain only RGBA bits.");
    }
    if (description.depth_stencil.two_sided_stencil_enable) {
        if (description.rasterizer.cull_mode != CullMode::None) {
            throw std::invalid_argument("D3D9 two-sided stencil requires CullMode::None.");
        }
        if ((caps.StencilCaps & D3DSTENCILCAPS_TWOSIDED) == 0u) {
            throw std::runtime_error("D3D9 device does not support two-sided stencil.");
        }
    }
    if (description.rasterizer.scissor_enable) {
        const ScissorRectangle& rectangle = description.rasterizer.scissor_rectangle;
        if (rectangle.left < 0 || rectangle.top < 0
            || rectangle.right <= rectangle.left || rectangle.bottom <= rectangle.top) {
            throw std::invalid_argument("D3D9 scissor rectangle must have positive dimensions.");
        }
        if ((caps.RasterCaps & D3DPRASTERCAPS_SCISSORTEST) == 0u) {
            throw std::runtime_error("D3D9 device does not support scissor testing.");
        }
        const RECT native_rectangle{rectangle.left, rectangle.top, rectangle.right, rectangle.bottom};
        check_result(device->SetScissorRect(&native_rectangle), "IDirect3DDevice9::SetScissorRect failed");
    }

    set_render_state(device, D3DRS_ZENABLE, description.depth_stencil.depth_enable ? D3DZB_TRUE : D3DZB_FALSE);
    set_render_state(device, D3DRS_ZWRITEENABLE, description.depth_stencil.depth_write ? TRUE : FALSE);
    set_render_state(device, D3DRS_ZFUNC, to_compare_function(description.depth_stencil.depth_compare));
    set_render_state(device, D3DRS_STENCILENABLE, description.depth_stencil.stencil_enable ? TRUE : FALSE);
    set_render_state(device, D3DRS_STENCILREF, description.depth_stencil.stencil_reference);
    set_render_state(device, D3DRS_STENCILMASK, description.depth_stencil.stencil_read_mask);
    set_render_state(device, D3DRS_STENCILWRITEMASK, description.depth_stencil.stencil_write_mask);
    set_render_state(
        device,
        D3DRS_TWOSIDEDSTENCILMODE,
        description.depth_stencil.two_sided_stencil_enable ? TRUE : FALSE);
    apply_stencil_face(
        device,
        description.depth_stencil.clockwise,
        D3DRS_STENCILFUNC,
        D3DRS_STENCILFAIL,
        D3DRS_STENCILZFAIL,
        D3DRS_STENCILPASS);
    if (description.depth_stencil.two_sided_stencil_enable) {
        apply_stencil_face(
            device,
            description.depth_stencil.counter_clockwise,
            D3DRS_CCW_STENCILFUNC,
            D3DRS_CCW_STENCILFAIL,
            D3DRS_CCW_STENCILZFAIL,
            D3DRS_CCW_STENCILPASS);
    }

    set_render_state(device, D3DRS_ALPHABLENDENABLE, description.blend.enabled ? TRUE : FALSE);
    set_render_state(device, D3DRS_SRCBLEND, to_blend_factor(description.blend.source_color));
    set_render_state(device, D3DRS_DESTBLEND, to_blend_factor(description.blend.destination_color));
    set_render_state(device, D3DRS_BLENDOP, to_blend_operation(description.blend.color_operation));
    set_render_state(device, D3DRS_SEPARATEALPHABLENDENABLE, description.blend.separate_alpha ? TRUE : FALSE);
    set_render_state(device, D3DRS_SRCBLENDALPHA, to_blend_factor(description.blend.source_alpha));
    set_render_state(device, D3DRS_DESTBLENDALPHA, to_blend_factor(description.blend.destination_alpha));
    set_render_state(device, D3DRS_BLENDOPALPHA, to_blend_operation(description.blend.alpha_operation));
    set_render_state(device, D3DRS_BLENDFACTOR, rgba_to_argb(description.blend.blend_factor_rgba));

    set_render_state(device, D3DRS_ALPHATESTENABLE, description.alpha_test.enabled ? TRUE : FALSE);
    set_render_state(device, D3DRS_ALPHAFUNC, to_compare_function(description.alpha_test.compare));
    set_render_state(device, D3DRS_ALPHAREF, description.alpha_test.reference);
    set_render_state(device, D3DRS_COLORWRITEENABLE, description.color_write_mask);
    set_render_state(device, D3DRS_CULLMODE, to_cull_mode(description.rasterizer.cull_mode));
    set_render_state(device, D3DRS_FILLMODE, to_fill_mode(description.rasterizer.fill_mode));
    set_render_state(device, D3DRS_SCISSORTESTENABLE, description.rasterizer.scissor_enable ? TRUE : FALSE);
    set_render_state(device, D3DRS_MULTISAMPLEANTIALIAS, description.rasterizer.multisample_antialias ? TRUE : FALSE);
    set_render_state(device, D3DRS_ANTIALIASEDLINEENABLE, description.rasterizer.antialiased_line_enable ? TRUE : FALSE);
    set_render_state(device, D3DRS_SRGBWRITEENABLE, description.rasterizer.srgb_write_enable ? TRUE : FALSE);
    set_render_state(device, D3DRS_LIGHTING, FALSE);
    set_render_state(device, D3DRS_FOGENABLE, FALSE);
    set_render_state(device, D3DRS_POINTSPRITEENABLE, FALSE);
}

void apply_sampler_state(
    IDirect3DDevice9* device,
    std::uint32_t sampler,
    const SamplerDescription& description) {
    if (sampler >= kMaxPixelSamplers || description.max_anisotropy == 0u) {
        throw std::invalid_argument("D3D9 sampler description is out of range.");
    }
    const auto set = [&](D3DSAMPLERSTATETYPE state, DWORD value) {
        check_result(device->SetSamplerState(sampler, state, value), "IDirect3DDevice9::SetSamplerState failed");
    };
    DWORD mip_lod_bias = 0u;
    std::memcpy(&mip_lod_bias, &description.mip_lod_bias, sizeof(mip_lod_bias));
    set(D3DSAMP_ADDRESSU, to_texture_address(description.address_u));
    set(D3DSAMP_ADDRESSV, to_texture_address(description.address_v));
    set(D3DSAMP_ADDRESSW, to_texture_address(description.address_w));
    set(D3DSAMP_MAGFILTER, to_texture_filter(description.mag_filter));
    set(D3DSAMP_MINFILTER, to_texture_filter(description.min_filter));
    set(D3DSAMP_MIPFILTER, to_texture_filter(description.mip_filter));
    set(D3DSAMP_MIPMAPLODBIAS, mip_lod_bias);
    set(D3DSAMP_MAXMIPLEVEL, description.max_mip_level);
    set(D3DSAMP_MAXANISOTROPY, description.max_anisotropy);
    set(D3DSAMP_BORDERCOLOR, rgba_to_argb(description.border_color_rgba));
    set(D3DSAMP_SRGBTEXTURE, description.srgb_texture ? TRUE : FALSE);
}

} // namespace

void set_txb_filter_modes(
    SamplerDescription& description,
    std::uint16_t minimum,
    std::uint16_t magnification) {
    struct MinimumFilterMode {
        TextureFilter min_filter;
        TextureFilter mip_filter;
        std::uint32_t max_anisotropy;
    };
    static constexpr std::array<MinimumFilterMode, 15u> kMinimumFilterModes = {{
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
    static constexpr std::array<TextureFilter, 3u> kMagnificationFilterModes = {{
        TextureFilter::Point,
        TextureFilter::Linear,
        TextureFilter::Anisotropic,
    }};

    if (minimum >= kMinimumFilterModes.size()) {
        throw std::out_of_range("TXB minimum filter ordinal is out of range.");
    }
    if (magnification >= kMagnificationFilterModes.size()) {
        throw std::out_of_range("TXB magnification filter ordinal is out of range.");
    }

    const MinimumFilterMode& minimum_mode = kMinimumFilterModes[minimum];
    description.min_filter = minimum_mode.min_filter;
    description.mag_filter = kMagnificationFilterModes[magnification];
    description.mip_filter = minimum_mode.mip_filter;
    description.max_anisotropy = minimum_mode.max_anisotropy;
}

struct D3d9VertexDeclaration::Implementation final {
    ComPointer<IDirect3DVertexDeclaration9> declaration;
    std::shared_ptr<void> device_lifetime;
    IDirect3DDevice9* owner_device = nullptr;
    std::vector<VertexElementDescription> elements;
};

struct D3d9VertexBuffer::Implementation final {
    std::shared_ptr<void> default_pool_lease;
    std::shared_ptr<void> device_lifetime;
    ComPointer<IDirect3DVertexBuffer9> buffer;
    IDirect3DDevice9* owner_device = nullptr;
    std::size_t byte_count = 0u;
    std::uint32_t stride = 0u;
};

struct D3d9IndexBuffer::Implementation final {
    std::shared_ptr<void> default_pool_lease;
    std::shared_ptr<void> device_lifetime;
    ComPointer<IDirect3DIndexBuffer9> buffer;
    IDirect3DDevice9* owner_device = nullptr;
    std::vector<std::uint32_t> indices;
};

struct D3d9Texture::Implementation final {
    ComPointer<IDirect3DTexture9> texture;
    std::shared_ptr<void> device_lifetime;
    IDirect3DDevice9* owner_device = nullptr;
};

struct D3d9VertexShader::Implementation final {
    ComPointer<IDirect3DVertexShader9> shader;
    std::shared_ptr<void> device_lifetime;
    IDirect3DDevice9* owner_device = nullptr;
};

struct D3d9PixelShader::Implementation final {
    ComPointer<IDirect3DPixelShader9> shader;
    std::shared_ptr<void> device_lifetime;
    IDirect3DDevice9* owner_device = nullptr;
};

struct D3d9Renderer::Implementation final {
    std::shared_ptr<void> device_lifetime;
    IDirect3DDevice9* device = nullptr;
    D3DCAPS9 caps{};
};

D3d9VertexDeclaration::D3d9VertexDeclaration(std::unique_ptr<Implementation> implementation)
    : implementation_(std::move(implementation)) {
}

D3d9VertexDeclaration::~D3d9VertexDeclaration() = default;
D3d9VertexDeclaration::D3d9VertexDeclaration(D3d9VertexDeclaration&&) noexcept = default;
D3d9VertexDeclaration& D3d9VertexDeclaration::operator=(D3d9VertexDeclaration&&) noexcept = default;

D3d9VertexBuffer::D3d9VertexBuffer(std::unique_ptr<Implementation> implementation)
    : implementation_(std::move(implementation)) {
}

D3d9VertexBuffer::~D3d9VertexBuffer() = default;
D3d9VertexBuffer::D3d9VertexBuffer(D3d9VertexBuffer&&) noexcept = default;
D3d9VertexBuffer& D3d9VertexBuffer::operator=(D3d9VertexBuffer&&) noexcept = default;

D3d9IndexBuffer::D3d9IndexBuffer(std::unique_ptr<Implementation> implementation)
    : implementation_(std::move(implementation)) {
}

D3d9IndexBuffer::~D3d9IndexBuffer() = default;
D3d9IndexBuffer::D3d9IndexBuffer(D3d9IndexBuffer&&) noexcept = default;
D3d9IndexBuffer& D3d9IndexBuffer::operator=(D3d9IndexBuffer&&) noexcept = default;

D3d9Texture::D3d9Texture(std::unique_ptr<Implementation> implementation)
    : implementation_(std::move(implementation)) {
}

D3d9Texture::~D3d9Texture() = default;
D3d9Texture::D3d9Texture(D3d9Texture&&) noexcept = default;
D3d9Texture& D3d9Texture::operator=(D3d9Texture&&) noexcept = default;

D3d9VertexShader::D3d9VertexShader(std::unique_ptr<Implementation> implementation)
    : implementation_(std::move(implementation)) {
}

D3d9VertexShader::~D3d9VertexShader() = default;
D3d9VertexShader::D3d9VertexShader(D3d9VertexShader&&) noexcept = default;
D3d9VertexShader& D3d9VertexShader::operator=(D3d9VertexShader&&) noexcept = default;

D3d9PixelShader::D3d9PixelShader(std::unique_ptr<Implementation> implementation)
    : implementation_(std::move(implementation)) {
}

D3d9PixelShader::~D3d9PixelShader() = default;
D3d9PixelShader::D3d9PixelShader(D3d9PixelShader&&) noexcept = default;
D3d9PixelShader& D3d9PixelShader::operator=(D3d9PixelShader&&) noexcept = default;

D3d9Renderer::D3d9Renderer(D3d9Device& device)
    : implementation_(std::make_unique<Implementation>()) {
    if (device.implementation_ == nullptr) {
        throw std::logic_error("Cannot create a D3D9 renderer without a device.");
    }
    implementation_->device_lifetime = std::static_pointer_cast<void>(device.implementation_);
    implementation_->device = static_cast<IDirect3DDevice9*>(D3d9Device::native_device(device.implementation_));
    if (implementation_->device == nullptr) {
        throw std::logic_error("Cannot create a D3D9 renderer for an uninitialized device.");
    }
    check_result(implementation_->device->GetDeviceCaps(&implementation_->caps), "IDirect3DDevice9::GetDeviceCaps failed");
}

D3d9Renderer::~D3d9Renderer() = default;
D3d9Renderer::D3d9Renderer(D3d9Renderer&&) noexcept = default;
D3d9Renderer& D3d9Renderer::operator=(D3d9Renderer&&) noexcept = default;

std::unique_ptr<D3d9VertexDeclaration> D3d9Renderer::create_vertex_declaration(
    const VertexDeclarationDescription& description) const {
    if (implementation_ == nullptr || implementation_->device == nullptr) {
        throw std::logic_error("D3D9 renderer is not initialized.");
    }
    if (description.elements.empty()) {
        throw std::invalid_argument("D3D9 vertex declarations require at least one element.");
    }

    std::vector<D3DVERTEXELEMENT9> elements;
    elements.reserve(description.elements.size() + 1u);
    for (const VertexElementDescription& source : description.elements) {
        if (source.stream >= kMaxStreams) {
            throw std::out_of_range("D3D9 vertex declaration stream is out of range.");
        }
        const std::size_t size = vertex_element_size(source.type);
        if (source.offset > std::numeric_limits<std::uint16_t>::max() - size) {
            throw std::out_of_range("D3D9 vertex declaration element exceeds its stream offset range.");
        }
        for (const VertexElementDescription& previous : description.elements) {
            if (&previous == &source) {
                break;
            }
            if (previous.stream == source.stream && previous.offset == source.offset) {
                throw std::invalid_argument("D3D9 vertex declaration contains duplicate stream offsets.");
            }
        }
        elements.push_back(D3DVERTEXELEMENT9{
            source.stream,
            source.offset,
            static_cast<BYTE>(to_declaration_type(source.type)),
            D3DDECLMETHOD_DEFAULT,
            static_cast<BYTE>(to_declaration_usage(source.usage)),
            source.usage_index,
        });
    }
    elements.push_back(D3DDECL_END());

    auto result = std::make_unique<D3d9VertexDeclaration::Implementation>();
    check_result(
        implementation_->device->CreateVertexDeclaration(elements.data(), result->declaration.put()),
        "IDirect3DDevice9::CreateVertexDeclaration failed");
    result->device_lifetime = implementation_->device_lifetime;
    result->owner_device = implementation_->device;
    result->elements = description.elements;
    return std::unique_ptr<D3d9VertexDeclaration>(new D3d9VertexDeclaration(std::move(result)));
}

std::unique_ptr<D3d9VertexBuffer> D3d9Renderer::create_vertex_buffer(
    const VertexBufferDescription& description) const {
    if (implementation_ == nullptr || implementation_->device == nullptr) {
        throw std::logic_error("D3D9 renderer is not initialized.");
    }
    validate_byte_view(description.bytes, "D3D9 vertex buffer bytes must not be empty.");
    if (description.stride == 0u || description.bytes.size % description.stride != 0u
        || description.bytes.size > std::numeric_limits<DWORD>::max()) {
        throw std::invalid_argument("D3D9 vertex buffer description has an invalid stride or size.");
    }

    auto result = std::make_unique<D3d9VertexBuffer::Implementation>();
    result->device_lifetime = implementation_->device_lifetime;
    result->owner_device = implementation_->device;
    if (description.dynamic) {
        const auto device_implementation = std::static_pointer_cast<D3d9Device::Implementation>(implementation_->device_lifetime);
        D3d9Device::retain_default_pool_resource(device_implementation);
        result->default_pool_lease = std::shared_ptr<void>(
            nullptr,
            [device_implementation](void*) {
                D3d9Device::release_default_pool_resource(device_implementation);
            });
    }
    const DWORD usage = D3DUSAGE_WRITEONLY | (description.dynamic ? D3DUSAGE_DYNAMIC : 0u);
    const D3DPOOL pool = description.dynamic ? D3DPOOL_DEFAULT : D3DPOOL_MANAGED;
    check_result(
        implementation_->device->CreateVertexBuffer(
            static_cast<UINT>(description.bytes.size),
            usage,
            0u,
            pool,
            result->buffer.put(),
            nullptr),
        "IDirect3DDevice9::CreateVertexBuffer failed");

    void* mapped = nullptr;
    check_result(
        result->buffer.get()->Lock(
            0u,
            static_cast<UINT>(description.bytes.size),
            &mapped,
            description.dynamic ? D3DLOCK_DISCARD : 0u),
        "IDirect3DVertexBuffer9::Lock failed");
    struct UnlockGuard final {
        IDirect3DVertexBuffer9* buffer;
        ~UnlockGuard() {
            buffer->Unlock();
        }
    } unlock_guard{result->buffer.get()};
    if (mapped == nullptr) {
        throw std::runtime_error("D3D9 vertex buffer lock returned no memory.");
    }
    std::memcpy(mapped, description.bytes.data, description.bytes.size);

    result->byte_count = description.bytes.size;
    result->stride = description.stride;
    return std::unique_ptr<D3d9VertexBuffer>(new D3d9VertexBuffer(std::move(result)));
}

std::unique_ptr<D3d9IndexBuffer> D3d9Renderer::create_index_buffer(
    const IndexBufferDescription& description) const {
    if (implementation_ == nullptr || implementation_->device == nullptr) {
        throw std::logic_error("D3D9 renderer is not initialized.");
    }
    validate_byte_view(description.bytes, "D3D9 index buffer bytes must not be empty.");
    const std::size_t element_size = index_size(description.format);
    if (description.bytes.size % element_size != 0u
        || description.bytes.size > std::numeric_limits<DWORD>::max()) {
        throw std::invalid_argument("D3D9 index buffer description has an invalid size.");
    }

    auto result = std::make_unique<D3d9IndexBuffer::Implementation>();
    result->device_lifetime = implementation_->device_lifetime;
    result->owner_device = implementation_->device;
    if (description.dynamic) {
        const auto device_implementation = std::static_pointer_cast<D3d9Device::Implementation>(implementation_->device_lifetime);
        D3d9Device::retain_default_pool_resource(device_implementation);
        result->default_pool_lease = std::shared_ptr<void>(
            nullptr,
            [device_implementation](void*) {
                D3d9Device::release_default_pool_resource(device_implementation);
            });
    }
    const DWORD usage = D3DUSAGE_WRITEONLY | (description.dynamic ? D3DUSAGE_DYNAMIC : 0u);
    const D3DPOOL pool = description.dynamic ? D3DPOOL_DEFAULT : D3DPOOL_MANAGED;
    check_result(
        implementation_->device->CreateIndexBuffer(
            static_cast<UINT>(description.bytes.size),
            usage,
            to_index_format(description.format),
            pool,
            result->buffer.put(),
            nullptr),
        "IDirect3DDevice9::CreateIndexBuffer failed");

    void* mapped = nullptr;
    check_result(
        result->buffer.get()->Lock(
            0u,
            static_cast<UINT>(description.bytes.size),
            &mapped,
            description.dynamic ? D3DLOCK_DISCARD : 0u),
        "IDirect3DIndexBuffer9::Lock failed");
    struct UnlockGuard final {
        IDirect3DIndexBuffer9* buffer;
        ~UnlockGuard() {
            buffer->Unlock();
        }
    } unlock_guard{result->buffer.get()};
    if (mapped == nullptr) {
        throw std::runtime_error("D3D9 index buffer lock returned no memory.");
    }
    std::memcpy(mapped, description.bytes.data, description.bytes.size);

    result->indices.resize(description.bytes.size / element_size);
    for (std::size_t index = 0u; index < result->indices.size(); ++index) {
        if (description.format == IndexFormat::UInt16) {
            std::uint16_t value = 0u;
            std::memcpy(&value, description.bytes.data + index * element_size, sizeof(value));
            result->indices[index] = value;
        } else {
            std::uint32_t value = 0u;
            std::memcpy(&value, description.bytes.data + index * element_size, sizeof(value));
            result->indices[index] = value;
        }
    }

    return std::unique_ptr<D3d9IndexBuffer>(new D3d9IndexBuffer(std::move(result)));
}

std::unique_ptr<D3d9Texture> D3d9Renderer::create_texture(const TextureDescription& description) const {
    if (implementation_ == nullptr || implementation_->device == nullptr) {
        throw std::logic_error("D3D9 renderer is not initialized.");
    }
    if (description.width == 0u || description.height == 0u || description.mips.empty()) {
        throw std::invalid_argument("D3D9 textures require nonzero dimensions and at least one mip.");
    }
    if (description.mips.size() > 32u) {
        throw std::out_of_range("D3D9 texture contains too many mip levels.");
    }

    for (std::size_t level = 0u; level < description.mips.size(); ++level) {
        const std::uint32_t width = std::max(1u, description.width >> level);
        const std::uint32_t height = std::max(1u, description.height >> level);
        const TextureMipLayout layout = texture_mip_layout(description.format, width, height);
        const TextureMipDescription& mip = description.mips[level];
        validate_byte_view(mip.bytes, "D3D9 texture mip bytes must not be empty.");
        if (mip.row_pitch != layout.row_pitch
            || layout.row_count > std::numeric_limits<std::size_t>::max() / mip.row_pitch
            || mip.bytes.size != static_cast<std::size_t>(layout.row_count) * mip.row_pitch) {
            throw std::invalid_argument("D3D9 texture mip byte range does not match its explicit format and dimensions.");
        }
    }

    auto result = std::make_unique<D3d9Texture::Implementation>();
    check_result(
        implementation_->device->CreateTexture(
            description.width,
            description.height,
            static_cast<UINT>(description.mips.size()),
            0u,
            to_texture_format(description.format),
            D3DPOOL_MANAGED,
            result->texture.put(),
            nullptr),
        "IDirect3DDevice9::CreateTexture failed");

    for (UINT level = 0u; level < description.mips.size(); ++level) {
        const TextureMipDescription& mip = description.mips[level];
        const std::uint32_t width = std::max(1u, description.width >> level);
        const std::uint32_t height = std::max(1u, description.height >> level);
        const TextureMipLayout layout = texture_mip_layout(description.format, width, height);
        D3DLOCKED_RECT locked{};
        check_result(result->texture.get()->LockRect(level, &locked, nullptr, 0u), "IDirect3DTexture9::LockRect failed");
        struct UnlockGuard final {
            IDirect3DTexture9* texture;
            UINT level;
            ~UnlockGuard() {
                texture->UnlockRect(level);
            }
        } unlock_guard{result->texture.get(), level};
        if (locked.pBits == nullptr || locked.Pitch < static_cast<LONG>(mip.row_pitch)) {
            throw std::runtime_error("D3D9 texture lock returned an invalid row pitch.");
        }
        const auto* source = mip.bytes.data;
        auto* destination = static_cast<std::uint8_t*>(locked.pBits);
        for (std::uint32_t row = 0u; row < layout.row_count; ++row) {
            std::memcpy(
                destination + static_cast<std::size_t>(locked.Pitch) * row,
                source + static_cast<std::size_t>(mip.row_pitch) * row,
                mip.row_pitch);
        }
    }

    result->device_lifetime = implementation_->device_lifetime;
    result->owner_device = implementation_->device;
    return std::unique_ptr<D3d9Texture>(new D3d9Texture(std::move(result)));
}

std::unique_ptr<D3d9Texture> D3d9Renderer::create_dds_texture(ByteView bytes) const {
    const DdsTextureData data = parse_dds_texture(bytes.data, bytes.size);
    TextureDescription description{};
    description.width = data.width;
    description.height = data.height;
    switch (data.format) {
    case DdsTextureFormat::Dxt1:
        description.format = TextureFormat::Dxt1;
        break;
    case DdsTextureFormat::Dxt3:
        description.format = TextureFormat::Dxt3;
        break;
    case DdsTextureFormat::Dxt5:
        description.format = TextureFormat::Dxt5;
        break;
    default:
        throw std::invalid_argument("Unsupported DDS texture format.");
    }
    description.mips.reserve(data.mips.size());
    for (const DdsTextureMip& mip : data.mips) {
        description.mips.push_back(TextureMipDescription{
            ByteView{mip.bytes.data(), mip.bytes.size()}, mip.row_pitch});
    }
    return create_texture(description);
}

std::unique_ptr<D3d9VertexShader> D3d9Renderer::create_vertex_shader(ByteView bytecode) const {
    if (implementation_ == nullptr || implementation_->device == nullptr) {
        throw std::logic_error("D3D9 renderer is not initialized.");
    }
    validate_byte_view(bytecode, "D3D9 vertex shader bytecode must not be empty.");
    if (bytecode.size % sizeof(DWORD) != 0u) {
        throw std::invalid_argument("D3D9 vertex shader bytecode must be DWORD-aligned.");
    }
    std::vector<DWORD> aligned_bytecode(bytecode.size / sizeof(DWORD));
    std::memcpy(aligned_bytecode.data(), bytecode.data, bytecode.size);

    auto result = std::make_unique<D3d9VertexShader::Implementation>();
    check_result(
        implementation_->device->CreateVertexShader(aligned_bytecode.data(), result->shader.put()),
        "IDirect3DDevice9::CreateVertexShader failed");
    result->device_lifetime = implementation_->device_lifetime;
    result->owner_device = implementation_->device;
    return std::unique_ptr<D3d9VertexShader>(new D3d9VertexShader(std::move(result)));
}

std::unique_ptr<D3d9PixelShader> D3d9Renderer::create_pixel_shader(ByteView bytecode) const {
    if (implementation_ == nullptr || implementation_->device == nullptr) {
        throw std::logic_error("D3D9 renderer is not initialized.");
    }
    validate_byte_view(bytecode, "D3D9 pixel shader bytecode must not be empty.");
    if (bytecode.size % sizeof(DWORD) != 0u) {
        throw std::invalid_argument("D3D9 pixel shader bytecode must be DWORD-aligned.");
    }
    std::vector<DWORD> aligned_bytecode(bytecode.size / sizeof(DWORD));
    std::memcpy(aligned_bytecode.data(), bytecode.data, bytecode.size);

    auto result = std::make_unique<D3d9PixelShader::Implementation>();
    check_result(
        implementation_->device->CreatePixelShader(aligned_bytecode.data(), result->shader.put()),
        "IDirect3DDevice9::CreatePixelShader failed");
    result->device_lifetime = implementation_->device_lifetime;
    result->owner_device = implementation_->device;
    return std::unique_ptr<D3d9PixelShader>(new D3d9PixelShader(std::move(result)));
}

void D3d9Renderer::set_transform(
    TransformSlot slot,
    const std::array<float, 16u>& row_major_matrix) const {
    if (implementation_ == nullptr || implementation_->device == nullptr) {
        throw std::logic_error("D3D9 renderer is not initialized.");
    }
    D3DTRANSFORMSTATETYPE transform = D3DTS_WORLD;
    switch (slot) {
    case TransformSlot::World:
        transform = D3DTS_WORLD;
        break;
    case TransformSlot::View:
        transform = D3DTS_VIEW;
        break;
    case TransformSlot::Projection:
        transform = D3DTS_PROJECTION;
        break;
    }
    D3DMATRIX matrix{};
    static_assert(sizeof(matrix) == sizeof(float) * 16u);
    std::memcpy(&matrix, row_major_matrix.data(), sizeof(matrix));
    check_result(implementation_->device->SetTransform(transform, &matrix), "IDirect3DDevice9::SetTransform failed");
}

void D3d9Renderer::draw(const DrawSubmission& submission) const {
    if (implementation_ == nullptr || implementation_->device == nullptr) {
        throw std::logic_error("D3D9 renderer is not initialized.");
    }
    bool indexed = false;
    switch (submission.mode) {
    case DrawSubmissionMode::Indexed:
        indexed = true;
        break;
    case DrawSubmissionMode::NonIndexed:
        break;
    default:
        throw std::invalid_argument("Unknown D3D9 draw submission mode.");
    }
    const D3DPRIMITIVETYPE primitive_type = to_primitive_type(submission.topology);
    if (submission.declaration == nullptr || submission.declaration->implementation_ == nullptr
        || submission.vertex_shader == nullptr || submission.vertex_shader->implementation_ == nullptr
        || submission.pixel_shader == nullptr || submission.pixel_shader->implementation_ == nullptr
        || (indexed && (submission.index_buffer == nullptr || submission.index_buffer->implementation_ == nullptr))) {
        throw std::invalid_argument("D3D9 draw submission is missing a required resource.");
    }
    if (!indexed && submission.index_buffer != nullptr) {
        throw std::invalid_argument("D3D9 nonindexed draw submission must not provide an index buffer.");
    }
    if (submission.declaration->implementation_->owner_device != implementation_->device
        || submission.vertex_shader->implementation_->owner_device != implementation_->device
        || submission.pixel_shader->implementation_->owner_device != implementation_->device
        || (indexed && submission.index_buffer->implementation_->owner_device != implementation_->device)) {
        throw std::invalid_argument("D3D9 draw submission combines resources from different devices.");
    }

    const std::uint32_t required_indices = required_index_count(submission.topology, submission.primitive_count);
    std::uint64_t maximum_actual_vertex = 0u;
    if (indexed) {
        if (submission.start_index > submission.index_buffer->implementation_->indices.size()
            || required_indices > submission.index_buffer->implementation_->indices.size() - submission.start_index) {
            throw std::out_of_range("D3D9 draw submission exceeds the supplied index buffer.");
        }
        if (submission.vertex_count == 0u
            || submission.minimum_vertex_index > std::numeric_limits<std::uint32_t>::max() - submission.vertex_count) {
            throw std::invalid_argument("D3D9 draw submission has an invalid vertex optimization range.");
        }
        const std::uint64_t optimization_end = static_cast<std::uint64_t>(submission.minimum_vertex_index) + submission.vertex_count;

        for (std::uint32_t offset = 0u; offset < required_indices; ++offset) {
            const std::uint32_t index = submission.index_buffer->implementation_->indices[submission.start_index + offset];
            if (index < submission.minimum_vertex_index || static_cast<std::uint64_t>(index) >= optimization_end) {
                throw std::out_of_range("D3D9 index lies outside the declared vertex optimization range.");
            }
            const std::int64_t actual = static_cast<std::int64_t>(submission.base_vertex_index) + index;
            if (actual < 0 || actual > std::numeric_limits<std::int32_t>::max()) {
                throw std::out_of_range("D3D9 base vertex index addresses a vertex outside the buffer range.");
            }
            maximum_actual_vertex = std::max(maximum_actual_vertex, static_cast<std::uint64_t>(actual));
        }
    } else {
        const std::uint64_t vertex_end = static_cast<std::uint64_t>(submission.first_vertex) + required_indices;
        if (vertex_end > static_cast<std::uint64_t>(std::numeric_limits<std::uint32_t>::max()) + 1u) {
            throw std::out_of_range("D3D9 nonindexed draw first vertex range overflows.");
        }
        maximum_actual_vertex = vertex_end - 1u;
    }

    std::array<const VertexStreamBinding*, kMaxStreams> streams{};
    for (const VertexStreamBinding& stream : submission.vertex_streams) {
        if (stream.stream >= kMaxStreams || stream.buffer == nullptr || stream.buffer->implementation_ == nullptr) {
            throw std::invalid_argument("D3D9 draw submission has an invalid vertex stream.");
        }
        if (stream.buffer->implementation_->owner_device != implementation_->device) {
            throw std::invalid_argument("D3D9 draw submission uses a vertex buffer from another device.");
        }
        if (streams[stream.stream] != nullptr) {
            throw std::invalid_argument("D3D9 draw submission binds a vertex stream more than once.");
        }
        if (stream.offset > stream.buffer->implementation_->byte_count) {
            throw std::out_of_range("D3D9 vertex stream offset exceeds its buffer.");
        }
        streams[stream.stream] = &stream;
    }
    if (submission.vertex_streams.empty()) {
        throw std::invalid_argument("D3D9 draw submission has no vertex streams.");
    }

    for (const VertexElementDescription& element : submission.declaration->implementation_->elements) {
        const VertexStreamBinding* binding = streams[element.stream];
        if (binding == nullptr) {
            throw std::invalid_argument("D3D9 vertex declaration references an unbound stream.");
        }
        const D3d9VertexBuffer::Implementation& buffer = *binding->buffer->implementation_;
        const std::size_t element_end = static_cast<std::size_t>(element.offset) + vertex_element_size(element.type);
        if (element_end > buffer.stride) {
            throw std::invalid_argument("D3D9 vertex declaration element exceeds its vertex stride.");
        }
        const std::uint64_t base_offset = static_cast<std::uint64_t>(binding->offset) + element_end;
        if (maximum_actual_vertex > (std::numeric_limits<std::uint64_t>::max() - base_offset) / buffer.stride) {
            throw std::overflow_error("D3D9 draw submission vertex addressing overflows its buffer range.");
        }
        const std::uint64_t required_end = base_offset + maximum_actual_vertex * buffer.stride;
        if (required_end > buffer.byte_count) {
            throw std::out_of_range("D3D9 draw submission exceeds a vertex buffer range.");
        }
    }

    std::array<bool, kMaxPixelSamplers> samplers{};
    for (const TextureBinding& binding : submission.textures) {
        if (binding.sampler >= kMaxPixelSamplers || binding.texture == nullptr || binding.texture->implementation_ == nullptr) {
            throw std::invalid_argument("D3D9 draw submission has an invalid texture binding.");
        }
        if (binding.texture->implementation_->owner_device != implementation_->device) {
            throw std::invalid_argument("D3D9 draw submission uses a texture from another device.");
        }
        if (samplers[binding.sampler]) {
            throw std::invalid_argument("D3D9 draw submission binds a sampler more than once.");
        }
        samplers[binding.sampler] = true;
    }

    const auto apply_float_constants = [&](const std::vector<Float4Constants>& bindings, bool vertex_shader) {
        const std::uint32_t limit = vertex_shader
            ? implementation_->caps.MaxVertexShaderConst
            : pixel_float_constant_limit(implementation_->caps);
        for (const Float4Constants& binding : bindings) {
            if (binding.vector_count == 0u) {
                continue;
            }
            if (binding.values == nullptr) {
                throw std::invalid_argument("D3D9 float shader constants require values.");
            }
            validate_register_range(binding.first_register, binding.vector_count, limit, "D3D9 float shader constant range is out of bounds.");
            const HRESULT result = vertex_shader
                ? implementation_->device->SetVertexShaderConstantF(binding.first_register, binding.values, binding.vector_count)
                : implementation_->device->SetPixelShaderConstantF(binding.first_register, binding.values, binding.vector_count);
            check_result(result, "IDirect3DDevice9::Set*ShaderConstantF failed");
        }
    };
    const auto apply_int_constants = [&](const std::vector<Int4Constants>& bindings, bool vertex_shader) {
        for (const Int4Constants& binding : bindings) {
            if (binding.vector_count == 0u) {
                continue;
            }
            if (binding.values == nullptr) {
                throw std::invalid_argument("D3D9 integer shader constants require values.");
            }
            validate_register_range(binding.first_register, binding.vector_count, kShaderIntegerRegisters, "D3D9 integer shader constant range is out of bounds.");
            std::vector<int> values(static_cast<std::size_t>(binding.vector_count) * 4u);
            std::memcpy(values.data(), binding.values, values.size() * sizeof(int));
            const HRESULT result = vertex_shader
                ? implementation_->device->SetVertexShaderConstantI(binding.first_register, values.data(), binding.vector_count)
                : implementation_->device->SetPixelShaderConstantI(binding.first_register, values.data(), binding.vector_count);
            check_result(result, "IDirect3DDevice9::Set*ShaderConstantI failed");
        }
    };
    const auto apply_bool_constants = [&](const std::vector<BoolConstants>& bindings, bool vertex_shader) {
        for (const BoolConstants& binding : bindings) {
            if (binding.value_count == 0u) {
                continue;
            }
            if (binding.values == nullptr) {
                throw std::invalid_argument("D3D9 boolean shader constants require values.");
            }
            validate_register_range(binding.first_register, binding.value_count, kShaderBooleanRegisters, "D3D9 boolean shader constant range is out of bounds.");
            std::vector<BOOL> values(binding.value_count);
            for (std::uint32_t index = 0u; index < binding.value_count; ++index) {
                values[index] = binding.values[index] == 0 ? FALSE : TRUE;
            }
            const HRESULT result = vertex_shader
                ? implementation_->device->SetVertexShaderConstantB(binding.first_register, values.data(), binding.value_count)
                : implementation_->device->SetPixelShaderConstantB(binding.first_register, values.data(), binding.value_count);
            check_result(result, "IDirect3DDevice9::Set*ShaderConstantB failed");
        }
    };

    check_result(
        implementation_->device->SetVertexDeclaration(submission.declaration->implementation_->declaration.get()),
        "IDirect3DDevice9::SetVertexDeclaration failed");
    for (const VertexStreamBinding& stream : submission.vertex_streams) {
        check_result(
            implementation_->device->SetStreamSource(
                stream.stream,
                stream.buffer->implementation_->buffer.get(),
                stream.offset,
                stream.buffer->implementation_->stride),
            "IDirect3DDevice9::SetStreamSource failed");
    }
    if (indexed) {
        check_result(
            implementation_->device->SetIndices(submission.index_buffer->implementation_->buffer.get()),
            "IDirect3DDevice9::SetIndices failed");
    } else {
        check_result(
            implementation_->device->SetIndices(nullptr),
            "IDirect3DDevice9::SetIndices failed");
    }
    check_result(
        implementation_->device->SetVertexShader(submission.vertex_shader->implementation_->shader.get()),
        "IDirect3DDevice9::SetVertexShader failed");
    check_result(
        implementation_->device->SetPixelShader(submission.pixel_shader->implementation_->shader.get()),
        "IDirect3DDevice9::SetPixelShader failed");
    apply_render_state(implementation_->device, implementation_->caps, submission.render_state);
    for (std::uint32_t sampler = 0u; sampler < kMaxPixelSamplers; ++sampler) {
        check_result(implementation_->device->SetTexture(sampler, nullptr), "IDirect3DDevice9::SetTexture failed");
    }
    for (const TextureBinding& binding : submission.textures) {
        apply_sampler_state(implementation_->device, binding.sampler, binding.sampler_state);
        check_result(
            implementation_->device->SetTexture(binding.sampler, binding.texture->implementation_->texture.get()),
            "IDirect3DDevice9::SetTexture failed");
    }
    apply_float_constants(submission.constants.vertex_float4, true);
    apply_int_constants(submission.constants.vertex_int4, true);
    apply_bool_constants(submission.constants.vertex_bool, true);
    apply_float_constants(submission.constants.pixel_float4, false);
    apply_int_constants(submission.constants.pixel_int4, false);
    apply_bool_constants(submission.constants.pixel_bool, false);

    if (indexed) {
        check_result(
            implementation_->device->DrawIndexedPrimitive(
                primitive_type,
                submission.base_vertex_index,
                submission.minimum_vertex_index,
                submission.vertex_count,
                submission.start_index,
                submission.primitive_count),
            "IDirect3DDevice9::DrawIndexedPrimitive failed");
    } else {
        check_result(
            implementation_->device->DrawPrimitive(
                primitive_type,
                submission.first_vertex,
                submission.primitive_count),
            "IDirect3DDevice9::DrawPrimitive failed");
    }
}

ReadbackImage D3d9Renderer::readback_current_target() const {
    if (implementation_ == nullptr || implementation_->device == nullptr) {
        throw std::logic_error("D3D9 renderer is not initialized.");
    }
    const auto device_implementation = std::static_pointer_cast<D3d9Device::Implementation>(implementation_->device_lifetime);
    if (D3d9Device::scene_active(device_implementation)) {
        throw std::logic_error("End the D3D9 scene before reading the render target.");
    }

    ComPointer<IDirect3DSurface9> render_target;
    check_result(
        implementation_->device->GetRenderTarget(0u, render_target.put()),
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
        implementation_->device->CreateOffscreenPlainSurface(
            description.Width,
            description.Height,
            description.Format,
            D3DPOOL_SYSTEMMEM,
            system_memory_surface.put(),
            nullptr),
        "IDirect3DDevice9::CreateOffscreenPlainSurface failed");
    check_result(
        implementation_->device->GetRenderTargetData(render_target.get(), system_memory_surface.get()),
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

} // namespace sonic4ep2::d3d9
