#include "native_stage_renderer.h"

#include <string_view>

#include "ame_sprite.h"
#include "ame_line.h"
#include "ame_draw.h"
#include "ambient_field.h"
#include "camera_matrix_d3dx.h"
#include "d3d9_renderer.h"
#include "nn_fog.h"
#include "nn_material_state.h"
#include "nn_lit_material_constants.h"
#include "nn_motion_palette.h"
#include "nn_motion_pose.h"
#include "nn_normal_matrix.h"
#include "nn_parallel_lighting.h"
#include "nn_shader_constants.h"
#include "nn_shader_name.h"
#include "nn_static_palette.h"
#include "nn_weighted_matrix_subset.h"
#include "render_scene.h"
#include "stage_data.h"
#include "stage_scene.h"
#include "txb_texture.h"

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <unknwn.h>
#include <wrl/client.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <iomanip>
#include <limits>
#include <map>
#include <optional>
#include <set>
#include <sstream>
#include <stdexcept>
#include <utility>
#include <vector>

namespace sonic4ep2::app {
namespace {

using namespace d3d9;
using Matrix = CameraMatrix;
constexpr CameraPrecision kPrecision = CameraPrecision::Single;
constexpr Matrix kIdentity{{1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1}};
constexpr StageMapRange kInspectionRegion{62, 102, 36, 52};
constexpr StageMapRange kPlayerSceneRegion{58, 102, 36, 52};
constexpr std::uint32_t kStageLightMask = 0x30u;
constexpr std::uint32_t kStageLightDrawFlags = 0x20000u;
constexpr std::uint32_t kCharacterSubobjectFlags = 0x201u;
constexpr std::uint32_t kCharacterLightMask = 0x0cu;
constexpr std::uint32_t kCharacterNonindexedVertexFormat = 0x17003u;
constexpr std::uint32_t kCharacterIndexedVertexFormat = 0x17403u;
constexpr std::uint32_t kCharacterNonindexedFvf = 0x11au;
constexpr std::uint32_t kCharacterIndexedFvf = 0x111au;
constexpr std::uint32_t kCharacterNonindexedStride = 44u;
constexpr std::uint32_t kCharacterIndexedStride = 48u;
constexpr std::uint32_t kCharacterDrawFlagsHigh = 8u;
constexpr std::uint32_t kRingVertexFormat = 3u;
constexpr std::uint32_t kRingFvf = 0x12u;
constexpr std::uint32_t kRingVertexStride = 24u;
constexpr std::uint32_t kRingDrawFlagsLow = 0x20000u;
constexpr std::uint32_t kRingDrawFlagsHigh = 2u;
constexpr std::uint32_t kRingLightMask = 0x03u;
constexpr std::size_t kRingNodeCount = 1u;
constexpr std::size_t kRingVertexCount = 200u;
constexpr std::size_t kRingStripIndexCount = 413u;
constexpr std::size_t kRingTriangleCount = 400u;
constexpr std::string_view kRingModelName = "RING.ZNO";
constexpr std::string_view kRingTextureName = "CMN_METAL_MS_RINGSKY_REF.DDS";
constexpr std::string_view kRingShaderName = "0000000000000RDMR8000022C4";
constexpr std::string_view kRingEffectShaderName = "0000000000000RDMRC00002081";
constexpr std::string_view kRingEffectTextureOneName = "KIRA1.DDS";
constexpr std::string_view kRingEffectTextureTwoName = "KIRA2.DDS";
constexpr std::size_t kRingEffectTextureArchiveIndex = 165u;
constexpr std::size_t kRingEffectTextureListIndex = 0u;
constexpr std::size_t kRingEffectTextureOneArchiveIndex = 13u;
constexpr std::size_t kRingEffectTextureTwoArchiveIndex = 14u;
constexpr std::size_t kRingEffectTextureOneIndex = 12u;
constexpr std::size_t kRingEffectTextureTwoIndex = 13u;
constexpr std::size_t kRingEffectVertexShaderIndex = 117u;
constexpr std::size_t kRingEffectPixelShaderIndex = 1053u;
constexpr std::uint16_t kRingEffectSimpleSpriteType = 0x0200u;
constexpr std::uint16_t kRingEffectSpriteType = 0x0201u;
constexpr std::uint32_t kRingEffectPCTFlag = 0x1000u;
constexpr std::uint32_t kRingEffectSimpleFlags = 0x0b003001u;
constexpr std::uint32_t kRingEffectSpriteFlags = 0x0b001001u;
constexpr std::int32_t kRingEffectBlend = 0xa2;
constexpr std::array<std::uint32_t, 7u> kRingMaterialDescriptor{{
    0u, 0u, 0x100u, 0x14cu, 0x2u, 0x1u, 0x168u,
}};
constexpr std::array<std::uint32_t, 16u> kRingMaterialStage{{
    0x60000002u, 0u, 0xffffffffu, 0x3f800000u,
    0x80000000u, 0u, 0x3f800000u, 0x3f800000u,
    0x00010001u, 0u, 0u, 0u,
    0u, 0u, 0u, 0u,
}};
constexpr std::size_t kCharacterNodeCount = 109u;
constexpr std::size_t kCharacterVertexListCount = 16u;
constexpr std::size_t kCharacterMeshPacketCount = 18u;
constexpr std::size_t kCharacterTextureCount = 5u;
constexpr std::size_t kSpinCharacterNodeCount = 21u;
constexpr std::size_t kSpinCharacterMatrixPaletteCount = 17u;
constexpr std::size_t kSpinCharacterVertexListCount = 2u;
constexpr std::size_t kSpinCharacterMeshPacketCount = 2u;
constexpr std::size_t kSpinCharacterTextureCount = 1u;
constexpr std::size_t kSpinCharacterTriangleCount = 792u;
constexpr std::size_t kSpinCharacterMotionChannelCount = 111u;
constexpr std::string_view kSpinCharacterTextureName = "CHR_SONIC_SPIN_DEF.DDS";
constexpr std::uint64_t kDigestOffset = 1469598103934665603ull;
constexpr std::uint64_t kDigestPrime = 1099511628211ull;
constexpr std::array<float, 28u> kCharacterUserUniform{};
enum class PlayerModelKind : std::uint8_t {
    Standard = 0u,
    Spin = 1u,
};

struct PlayerMotionSpec {
    std::string_view name;
    float start_frame;
    float end_frame;
    PlayerModelKind model;
};

constexpr std::array<PlayerMotionSpec, 28u> kPlayerMotionSpecs{{
    {"SON_FW.ZNM", 0.0f, 60.0f, PlayerModelKind::Standard},
    {"SON_FW_L.ZNM", 0.0f, 60.0f, PlayerModelKind::Standard},
    {"SON_FWWAIT0_01.ZNM", 0.0f, 30.0f, PlayerModelKind::Standard},
    {"SON_FWWAIT0_02.ZNM", 30.0f, 150.0f, PlayerModelKind::Standard},
    {"SON_FWWAIT1_01.ZNM", 0.0f, 60.0f, PlayerModelKind::Standard},
    {"SON_FWWAIT1_02.ZNM", 60.0f, 440.0f, PlayerModelKind::Standard},
    {"SON_FWWAIT2_01.ZNM", 130.0f, 200.0f, PlayerModelKind::Standard},
    {"SON_FWWAIT2_02.ZNM", 200.0f, 360.0f, PlayerModelKind::Standard},
    {"SON_WALK.ZNM", 0.0f, 60.0f, PlayerModelKind::Standard},
    {"SON_RUN.ZNM", 0.0f, 60.0f, PlayerModelKind::Standard},
    {"SON_DASH1.ZNM", 0.0f, 60.0f, PlayerModelKind::Standard},
    {"SON_DASH2.ZNM", 0.0f, 20.0f, PlayerModelKind::Standard},
    {"SON_DASH2_L.ZNM", 0.0f, 20.0f, PlayerModelKind::Standard},
    {"SON_SPIN_N.ZNM", 0.0f, 20.0f, PlayerModelKind::Standard},
    {"SON_SPIN_B.ZNM", 0.0f, 20.0f, PlayerModelKind::Standard},
    {"SON_FALL.ZNM", 50.0f, 70.0f, PlayerModelKind::Standard},
    {"SON_FALL_L.ZNM", 50.0f, 70.0f, PlayerModelKind::Standard},
    {"SON_FALL_R.ZNM", 50.0f, 70.0f, PlayerModelKind::Standard},
    {"SON_FALL_R_L.ZNM", 50.0f, 70.0f, PlayerModelKind::Standard},
    {"SON_SQUAT_02.ZNM", 15.0f, 35.0f, PlayerModelKind::Standard},
    {"SON_SQUAT_L_02.ZNM", 15.0f, 35.0f, PlayerModelKind::Standard},
    {"SON_SQUAT_03.ZNM", 35.0f, 60.0f, PlayerModelKind::Standard},
    {"SON_SQUAT_L_03.ZNM", 35.0f, 60.0f, PlayerModelKind::Standard},
    {"SON_SPIN01.ZNM", 0.0f, 60.0f, PlayerModelKind::Spin},
    {"SON_FALL_TURN_L.ZNM", 60.0f, 70.0f, PlayerModelKind::Standard},
    {"SON_FALL_TURN.ZNM", 50.0f, 60.0f, PlayerModelKind::Standard},
    {"SON_FALL_R_TURN_L.ZNM", 60.0f, 70.0f, PlayerModelKind::Standard},
    {"SON_FALL_R_TURN.ZNM", 50.0f, 60.0f, PlayerModelKind::Standard},
}};

const PlayerMotionSpec* find_player_motion_spec(std::string_view name) {
    const auto found = std::find_if(
        kPlayerMotionSpecs.begin(),
        kPlayerMotionSpecs.end(),
        [name](const PlayerMotionSpec& candidate) { return name == candidate.name; });
    return found == kPlayerMotionSpecs.end() ? nullptr : &*found;
}

void require(bool condition, const char* message) {
    if (!condition) throw std::invalid_argument(message);
}

float decode_float(std::uint32_t bits) {
    float value = 0.0f;
    static_assert(sizeof(value) == sizeof(bits));
    std::memcpy(&value, &bits, sizeof(value));
    return value;
}

std::uint32_t encode_float(float value) {
    std::uint32_t bits = 0u;
    static_assert(sizeof(value) == sizeof(bits));
    std::memcpy(&bits, &value, sizeof(bits));
    return bits;
}

void mix_digest(std::uint64_t& digest, std::uint32_t value) {
    digest ^= value;
    digest *= kDigestPrime;
}

std::string digest_string(std::uint64_t digest) {
    std::ostringstream output;
    output << std::hex << std::setfill('0') << std::setw(16) << digest;
    return output.str();
}

std::string basename(std::string name) {
    const auto separator = name.find_last_of("/\\");
    if (separator != std::string::npos) name.erase(0, separator + 1u);
    for (auto& character : name) {
        if (character >= 'a' && character <= 'z') character = static_cast<char>(character - 'a' + 'A');
    }
    return name;
}

std::string json_string(const std::string& value) {
    std::ostringstream output;
    output << '"';
    for (const unsigned char character : value) {
        if (character == '"' || character == '\\') output << '\\' << character;
        else if (character < 32u) output << '?';
        else output << character;
    }
    output << '"';
    return output.str();
}

struct Archive {
    std::vector<std::uint8_t> bytes;
    std::vector<StageArchiveEntry> entries;

    explicit Archive(const std::filesystem::path& path) {
        std::ifstream input(path, std::ios::binary | std::ios::ate);
        if (!input) throw std::runtime_error("Cannot open required original archive: " + path.u8string());
        const auto length = input.tellg();
        require(length > 0 && length <= 512 * 1024 * 1024, "Original archive size is unsupported.");
        bytes.resize(static_cast<std::size_t>(length));
        input.seekg(0);
        input.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
        if (!input) throw std::runtime_error("Cannot read required original archive: " + path.u8string());
        entries = parse_pc_stage_archive(bytes.data(), bytes.size());
    }

    explicit Archive(ByteView source) {
        require(source.data != nullptr && source.size != 0u && source.size <= 512u * 1024u * 1024u,
                "Nested original archive size is unsupported.");
        bytes.assign(source.data, source.data + source.size);
        entries = parse_pc_stage_archive(bytes.data(), bytes.size());
    }

    ByteView at(const StageArchiveEntry& entry) const {
        return {bytes.data() + entry.offset, entry.length};
    }

    ByteView named(const std::string& name) const {
        const std::string target = basename(name);
        const StageArchiveEntry* found = nullptr;
        for (const auto& entry : entries) {
            if (basename(entry.name) != target) continue;
            if (found) throw std::runtime_error("Ambiguous original archive entry: " + name);
            found = &entry;
        }
        if (!found) throw std::runtime_error("Missing original archive entry: " + name);
        return at(*found);
    }
};

struct ConstantDescription {
    const char* name;
    DWORD register_set;
    UINT first_register;
    UINT register_count;
    DWORD parameter_class;
    DWORD parameter_type;
    UINT rows;
    UINT columns;
    UINT elements;
    UINT members;
    UINT bytes;
    const void* default_value;
};

template<class Function> Function table_method(IUnknown* table, std::size_t index) {
    const auto* functions = *reinterpret_cast<void***>(table);
    Function function{};
    static_assert(sizeof(function) == sizeof(functions[index]));
    std::memcpy(&function, &functions[index], sizeof(function));
    return function;
}

struct ShaderConstants {
    Microsoft::WRL::ComPtr<IUnknown> table;
    mutable std::map<std::string, std::optional<ConstantDescription>> descriptions;

    explicit ShaderConstants(ByteView shader) {
        require(shader.size % 4u == 0u, "Original shader byte count is unaligned.");
        std::vector<DWORD> aligned(shader.size / 4u);
        std::memcpy(aligned.data(), shader.data, shader.size);
        using GetTable = HRESULT (WINAPI*)(const DWORD*, IUnknown**);
        const auto address = GetProcAddress(GetModuleHandleW(L"d3dx9_43.dll"), "D3DXGetShaderConstantTable");
        require(address != nullptr, "D3DX shader constant reflection is unavailable.");
        GetTable function{};
        static_assert(sizeof(function) == sizeof(address));
        std::memcpy(&function, &address, sizeof(function));
        if (FAILED(function(aligned.data(), table.GetAddressOf()))) {
            throw std::runtime_error("Cannot reflect the original shader constant table.");
        }
    }

    std::optional<ConstantDescription> find(const char* name) const {
        const auto cached = descriptions.find(name);
        if (cached != descriptions.end()) return cached->second;
        using GetName = const char* (WINAPI*)(IUnknown*, const char*, const char*);
        using GetDescription = HRESULT (WINAPI*)(IUnknown*, const char*, ConstantDescription*, UINT*);
        const char* handle = table_method<GetName>(table.Get(), 9u)(table.Get(), nullptr, name);
        if (!handle) {
            descriptions.emplace(name, std::nullopt);
            return std::nullopt;
        }
        ConstantDescription description{};
        UINT count = 1u;
        if (FAILED(table_method<GetDescription>(table.Get(), 6u)(table.Get(), handle, &description, &count)) || count != 1u) {
            throw std::runtime_error("Cannot read the original shader constant description.");
        }
        if (description.register_count == 0u) {
            descriptions.emplace(name, std::nullopt);
            return std::nullopt;
        }
        descriptions.emplace(name, description);
        return description;
    }

    void bind(std::vector<Float4Constants>& target, const char* name, const float* values,
              std::uint32_t available_vectors, bool required = false) const {
        const auto description = find(name);
        require(description.has_value() || !required, "Required original shader constant is missing.");
        if (!description) return;
        require(description->register_set == 2u && description->register_count <= available_vectors,
                "Original shader constant shape is unsupported.");
        target.push_back({description->first_register, values, description->register_count});
    }

    void bind_matrix(
        std::vector<Float4Constants>& target,
        const char* name,
        const float* values,
        std::uint32_t available_vectors,
        std::uint32_t rows,
        std::uint32_t columns,
        bool required = false) const {
        const auto description = find(name);
        require(description.has_value() || !required, "Required original shader matrix is missing.");
        if (!description) return;
        const std::uint64_t expected_bytes = static_cast<std::uint64_t>(rows) * columns * sizeof(float);
        require(
            values != nullptr && description->register_set == 2u && description->parameter_class == 3u &&
                description->parameter_type == 3u && description->rows == rows &&
                description->columns == columns && description->elements == 1u && description->members == 0u &&
                description->register_count == columns && description->bytes == expected_bytes &&
                description->register_count <= available_vectors,
            "Original shader matrix shape is unsupported.");
        target.push_back({description->first_register, values, description->register_count});
    }

    void bind_partial_matrix_array(
        std::vector<Float4Constants>& target,
        const char* name,
        const float* values,
        std::uint32_t available_vectors,
        std::uint32_t element_count,
        std::uint32_t declared_elements,
        std::uint32_t rows,
        std::uint32_t columns,
        bool required = false) const {
        const auto description = find(name);
        require(description.has_value() || !required, "Required original shader matrix array is missing.");
        if (!description) return;
        const std::uint64_t expected_registers = static_cast<std::uint64_t>(declared_elements) * columns;
        const std::uint64_t active_registers = static_cast<std::uint64_t>(element_count) * columns;
        const std::uint64_t expected_bytes = static_cast<std::uint64_t>(declared_elements) * rows * columns * sizeof(float);
        require(
            values != nullptr && element_count != 0u && element_count <= declared_elements &&
                expected_registers <= std::numeric_limits<UINT>::max() &&
                active_registers <= std::numeric_limits<UINT>::max() &&
                description->register_set == 2u && description->parameter_class == 3u &&
                description->parameter_type == 3u && description->rows == rows &&
                description->columns == columns && description->elements == declared_elements &&
                description->members == 0u && description->register_count == expected_registers &&
                description->bytes == expected_bytes && active_registers <= available_vectors,
            "Original shader matrix-array shape is unsupported.");
        target.push_back({description->first_register, values, static_cast<UINT>(active_registers)});
    }
};

struct Pipeline {
    std::unique_ptr<D3d9VertexShader> vertex_shader;
    std::unique_ptr<D3d9PixelShader> pixel_shader;
    ShaderConstants vertex_constants;
    ShaderConstants pixel_constants;

    Pipeline(D3d9Renderer& renderer, ByteView vertex, ByteView pixel)
        : vertex_shader(renderer.create_vertex_shader(vertex)), pixel_shader(renderer.create_pixel_shader(pixel)),
          vertex_constants(vertex), pixel_constants(pixel) {
        const auto sampler = pixel_constants.find("s_texBase");
        require(sampler && sampler->register_set == 3u && sampler->first_register == 0u && sampler->register_count == 1u,
                "Original base-map sampler binding is unsupported.");
    }
};

struct Vertex {
    std::array<float, 3u> position;
    std::array<std::uint8_t, 4u> color;
    std::array<float, 2u> uv;
    std::array<float, 3u> normal;
};
static_assert(sizeof(Vertex) == 36u);

struct AmeSpriteGpuVertex {
    std::array<float, 3u> position;
    std::array<std::uint8_t, 4u> color;
    std::array<float, 2u> texcoord;
};
static_assert(sizeof(AmeSpriteGpuVertex) == 24u);

AmeSpriteGpuVertex make_ame_sprite_gpu_vertex(const AmeSpriteVertex& source) {
    const std::uint32_t color = ame_sprite_d3d_color(source.color_rgba);
    return {
        source.position,
        {{
            static_cast<std::uint8_t>(color >> 16u),
            static_cast<std::uint8_t>(color >> 8u),
            static_cast<std::uint8_t>(color),
            static_cast<std::uint8_t>(color >> 24u),
        }},
        source.texcoord,
    };
}

RenderStateDescription initial_state() {
    RenderStateDescription state{};
    state.blend = {false, BlendFactor::SourceAlpha, BlendFactor::InverseSourceAlpha, BlendOperation::Add,
                   false, BlendFactor::One, BlendFactor::Zero, BlendOperation::Add, 0u};
    state.depth_stencil.depth_compare = CompareFunction::Always;
    state.depth_stencil.stencil_read_mask = 255u;
    state.depth_stencil.stencil_write_mask = 255u;
    state.depth_stencil.clockwise = {CompareFunction::Always, StencilOperation::Keep, StencilOperation::Keep, StencilOperation::Keep};
    state.depth_stencil.counter_clockwise = state.depth_stencil.clockwise;
    state.alpha_test = {false, CompareFunction::Always, 0u};
    state.color_write_mask = 15u;
    state.rasterizer.cull_mode = CullMode::None;
    state.rasterizer.fill_mode = FillMode::Solid;
    return state;
}

struct Mesh {
    std::unique_ptr<D3d9VertexDeclaration> declaration;
    std::unique_ptr<D3d9VertexBuffer> vertices;
    std::unique_ptr<D3d9IndexBuffer> indices;
    Pipeline* pipeline = nullptr;
    D3d9Texture* texture = nullptr;
    SamplerDescription sampler{};
    RenderStateDescription state{};
    NnLitMaterialConstants material{};
    bool lit = false;
    Matrix texture_columns{};
    std::size_t palette_index = 0u;
    std::vector<std::uint32_t> matrix_subset;
    std::uint32_t matrix_capacity = 0u;
    std::uint32_t vertex_count = 0u;
    std::uint32_t triangle_count = 0u;
    bool shadow = false;
};

struct Model {
    NnModelData data;
    std::vector<Mesh> meshes;
    std::size_t packet_count = 0u;
};

struct Layer {
    const char* name;
    float depth;
    std::size_t instances = 0u;
    std::size_t requested_draws = 0u;
    std::size_t supported_draws = 0u;
};

}

struct NativeStageRenderer::Implementation {
    D3dxCameraMatrixBackend backend;
    D3d9Device& device;
    D3d9Renderer renderer;
    struct MotionTiming {
        float start = 0.0f;
        float end = 0.0f;
        float rate = 0.0f;
    };
    struct PlayerMotion {
        NnMotionData data{};
        MotionTiming timing{};
        PlayerModelKind model = PlayerModelKind::Standard;
    };
    struct CharacterPoseEvaluation {
        std::vector<NnMotionLocalPose> poses;
        std::size_t final_channel_cursor = 0u;
        std::uint64_t digest = kDigestOffset;
    };
    std::map<std::string, std::unique_ptr<Pipeline>> pipelines;
    std::map<std::string, std::unique_ptr<D3d9Texture>> textures;
    bool character_inspection = false;
    bool player_scene = false;
    bool lighting_loaded = false;
    std::optional<float> fixed_character_frame;
    NnModelData character_model{};
    NnModelData spin_character_model{};
    Model ring_model{};
    bool ring_resources_prepared = false;
    bool ring_effect_resources_prepared = false;
    std::vector<Matrix> ring_instances;
    std::vector<AmeRuntimeSprite> ring_effect_sprites;
    std::vector<NativePlayerEffectFrame> player_effect_frames;
    std::unique_ptr<D3d9VertexDeclaration> ring_effect_declaration;
    std::unique_ptr<D3d9VertexBuffer> ring_effect_vertices;
    Pipeline* ring_effect_pipeline = nullptr;
    std::array<D3d9Texture*, 3u> ring_effect_textures{{nullptr, nullptr, nullptr}};
    SamplerDescription ring_effect_sampler{};
    RenderStateDescription ring_effect_state{};
    std::vector<AmbientFieldPoint> ring_ambient_points;
    AmbientFieldPalettes ring_ambient_palettes{};
    NnMotionData character_motion{};
    std::map<std::string, PlayerMotion> player_motion_cache;
    std::unique_ptr<Archive> player_motion_archive;
    const PlayerMotion* selected_player_motion = nullptr;
    std::string player_motion_name;
    float player_motion_start = 0.0f;
    float player_motion_end = 0.0f;
    float player_motion_rate = 0.0f;
    std::string player_secondary_motion_name;
    float player_secondary_motion_start = 0.0f;
    float player_secondary_motion_end = 0.0f;
    float player_secondary_motion_rate = 0.0f;
    float player_secondary_motion_frame = 0.0f;
    std::uint32_t player_secondary_motion_channel_count = 0u;
    float player_blend_weight = 0.0f;
    bool player_blend_active = false;
    std::vector<Mesh> character_meshes;
    std::vector<Mesh> spin_character_meshes;
    float character_frame = 0.0f;
    float character_motion_start = 0.0f;
    float character_motion_end = 0.0f;
    float character_motion_rate = 0.0f;
    std::size_t character_pose_nodes = 0u;
    std::size_t character_final_channel_cursor = 0u;
    std::size_t character_secondary_final_channel_cursor = 0u;
    std::size_t character_triangle_count = 0u;
    std::size_t character_texture_cache_delta = 0u;
    std::size_t character_pipeline_cache_delta = 0u;
    std::size_t character_pipeline_count = 0u;
    std::size_t character_frame_draws = 0u;
    std::size_t character_frame_triangles = 0u;
    std::size_t character_frame_non_shadow = 0u;
    std::size_t character_frame_lit = 0u;
    std::size_t ring_frame_draws = 0u;
    std::size_t ring_frame_triangles = 0u;
    std::size_t ring_frame_non_shadow = 0u;
    std::size_t ring_frame_lit = 0u;
    std::size_t ring_frame_tinted = 0u;
    std::uint64_t ring_tint_digest = kDigestOffset;
    std::uint64_t dash_effect_frame_draws = 0u;
    std::uint64_t dash_effect_frame_sprites = 0u;
    std::uint64_t dash_effect_frame_lines = 0u;
    std::uint64_t dash_effect_submitted_sprites = 0u;
    std::uint64_t dash_effect_submitted_lines = 0u;
    std::uint64_t dash_effect_submitted_triangles = 0u;
    std::uint64_t ring_effect_frame_draws = 0u;
    std::uint64_t ring_effect_frame_sprites = 0u;
    std::uint64_t ring_effect_frame_triangles = 0u;
    std::uint64_t ring_effect_submitted_sprites = 0u;
    std::uint64_t ring_effect_submitted_triangles = 0u;
    std::uint64_t character_pose_digest = kDigestOffset;
    std::uint64_t character_palette_digest = kDigestOffset;
    std::vector<Model> models;
    std::vector<StageSceneInstance> instances;
    std::array<Layer, 7u> layers{{{"ZONE11_B", 0.0f}, {"ZONE11_A", 32.0f},
        {"ZONE11_N", 48.0f}, {"ZONE11_M", -32.0f}, {"ZONE11_M1", -300.0f},
        {"ZONE11_M2", -400.0f}, {"ZONE11_M3", -500.0f}}};
    std::map<std::string, std::size_t> skipped;
    std::size_t all_packets = 0u;
    std::size_t accepted_packets = 0u;
    std::size_t non_shadow_packets = 0u;
    std::size_t lit_packets = 0u;
    std::size_t frame_lit = 0u;
    StageLighting lighting{};
    std::array<float, 4u> global_ambient{};
    std::size_t scene_unique_meshes = 0u;
    std::size_t frame_draws = 0u;
    std::size_t frame_triangles = 0u;
    std::size_t frame_non_shadow = 0u;
    StageMapRange loaded_region = kInspectionRegion;
    CameraViewParameters player_scene_camera{};
    Matrix player_scene_world = kIdentity;
    bool player_scene_rendered = false;
    float camera_x = 0.0f;
    float camera_y = 0.0f;
    float camera_distance = 0.0f;

    struct FrameLighting {
        NnFogState fog{};
        std::array<float, 4u> fog_density{};
        std::array<float, 4u> fog_start{};
        std::array<float, 4u> fog_end{};
        std::array<float, 4u> fog_scale{};
        NnParallelLightingConstants lights{};
        std::array<float, 4u> light_count{};
    };

    const NnModelData& player_character_model(PlayerModelKind model) const {
        switch (model) {
        case PlayerModelKind::Standard:
            return character_model;
        case PlayerModelKind::Spin:
            require(!spin_character_model.nodes.empty(), "Spin character model resources are unavailable.");
            return spin_character_model;
        }
        throw std::invalid_argument("Player-scene character model is unsupported.");
    }

    const std::vector<Mesh>& player_character_meshes(PlayerModelKind model) const {
        switch (model) {
        case PlayerModelKind::Standard:
            return character_meshes;
        case PlayerModelKind::Spin:
            require(!spin_character_meshes.empty(), "Spin character mesh resources are unavailable.");
            return spin_character_meshes;
        }
        throw std::invalid_argument("Player-scene character model is unsupported.");
    }

    void load_lighting(const std::filesystem::path& data_root) {
        if (lighting_loaded) return;
        const Archive lighting_archive(data_root / "G_COM/SETTING/GM_SETTING_LIGHT.AMB");
        const auto lighting_bytes = lighting_archive.named("LIGHT_SETTING_Z11.LTS");
        lighting = normalize_stage_lighting(
            parse_pc_stage_lighting(lighting_bytes.data, lighting_bytes.size),
            kPrecision);
        for (std::size_t channel = 0u; channel < 3u; ++channel) {
            std::memcpy(&global_ambient[channel], &lighting.ambient_bits[channel], sizeof(float));
        }
        lighting_loaded = true;
    }

    Implementation(
        D3d9Device& target,
        const std::filesystem::path& data_root,
        bool use_character_inspection,
        std::optional<float> requested_character_frame,
        bool use_player_scene)
        : device(target),
          renderer(target),
          character_inspection(use_character_inspection),
          player_scene(use_player_scene),
          fixed_character_frame(requested_character_frame) {
        require(!(character_inspection && player_scene), "Character inspection and player scene cannot be combined.");
        require(!player_scene || !fixed_character_frame.has_value(),
                "Player-scene motion frame must be supplied at render time.");
        if (character_inspection) {
            require(
                !fixed_character_frame.has_value() ||
                    (std::isfinite(*fixed_character_frame) && *fixed_character_frame >= 0.0f &&
                     *fixed_character_frame <= 60.0f),
                "Character inspection frame is invalid.");
            load_lighting(data_root);
            prepare_character(data_root);
            return;
        }
        load_lighting(data_root);
        const Archive model_archive(data_root / "G_ZONE1/MAP/ZONE1_M.AMB");
        const Archive map_archive(data_root / "G_ZONE1/MAP/ZONE11_MAP.AMB");
        const Archive texture_archive(data_root / "G_ZONE1/MAP/ZONE1_T.AMB");
        const Archive shader_archive(data_root / "NNSTDSHADER/SHADER.AMB");
        require(model_archive.entries.size() <= 4096u, "Stage model count is unsupported.");
        std::vector<StageSceneModel> placement_models(model_archive.entries.size());
        models.resize(model_archive.entries.size());
        for (std::size_t index = 0u; index < models.size(); ++index) {
            const auto& entry = model_archive.entries[index];
            if (index == 0u && entry.length == 0u) continue;
            const auto bytes = model_archive.at(entry);
            auto& model = models[index];
            model.data = parse_pc_nn_model(bytes.data, bytes.size);
            placement_models[index] = {entry.flags, model.data.first_node_translation_bits};
            const auto scene = build_render_scene(model.data);
            model.packet_count = scene.draw_packets.size();
            all_packets += model.packet_count;
            try {
                (void)nn_static_node_matrices(model.data, kIdentity, kPrecision, backend);
            } catch (const std::invalid_argument& error) {
                skipped[error.what()] += model.packet_count;
                continue;
            }
            for (const auto& packet : scene.draw_packets) {
                try {
                    Mesh mesh = prepare_mesh(model.data, entry.flags, packet, texture_archive, shader_archive);
                    if (!mesh.shadow) ++non_shadow_packets;
                    if (mesh.lit) ++lit_packets;
                    model.meshes.push_back(std::move(mesh));
                    ++accepted_packets;
                } catch (const std::invalid_argument& error) {
                    ++skipped[error.what()];
                }
            }
        }
        std::set<std::uint16_t> used_models;
        loaded_region = player_scene ? kPlayerSceneRegion : kInspectionRegion;
        for (auto& layer : layers) {
            const auto mp_bytes = map_archive.named(std::string(layer.name) + ".MP");
            const auto md_bytes = map_archive.named(std::string(layer.name) + ".MD");
            const auto mp = parse_stage_grid(mp_bytes.data, mp_bytes.size, 2u);
            const auto md = parse_stage_grid(md_bytes.data, md_bytes.size, 1u);
            const auto placed = assemble_pc_stage_instances(mp, md, placement_models, loaded_region,
                {{0.0f, 0.0f, layer.depth}}, StageTransformPrecision::Single);
            layer.instances = placed.size();
            for (const auto& instance : placed) {
                const auto& model = models.at(instance.placement.model_id);
                layer.requested_draws += model.packet_count;
                layer.supported_draws += model.meshes.size();
                if (!model.meshes.empty()) used_models.insert(instance.placement.model_id);
            }
            instances.insert(instances.end(), placed.begin(), placed.end());
        }
        for (const auto index : used_models) scene_unique_meshes += models[index].meshes.size();
        require(scene_unique_meshes > 1u && non_shadow_packets > 0u, "No multi-mesh original stage subset is supported.");
        if (player_scene) {
            prepare_character(data_root);
            prepare_ring(data_root);
            prepare_ring_effects(data_root);
        }
    }

    void validate_player_motion_evaluation(
        const NnModelData& model,
        const NnMotionData& motion,
        float frame) const {
        std::size_t channel_cursor = 0u;
        for (std::size_t node_index = 0u; node_index < model.nodes.size(); ++node_index) {
            require(
                node_index <= static_cast<std::size_t>(std::numeric_limits<std::int32_t>::max()),
                "Player-scene motion node index is unsupported.");
            const NnMotionLocalPose pose = nn_motion_local_pose(
                model.nodes[node_index],
                static_cast<std::int32_t>(node_index),
                motion,
                channel_cursor,
                frame,
                kPrecision);
            require(
                pose.next_channel <= motion.channels.size() &&
                    pose.next_channel <= static_cast<std::size_t>(std::numeric_limits<std::uint32_t>::max()),
                "Player-scene motion channel cursor is unsupported.");
            channel_cursor = pose.next_channel;
        }
    }

    MotionTiming validate_player_motion(
        const NnModelData& model,
        const NnMotionData& motion,
        const PlayerMotionSpec& specification) const {
        require(
            motion.channel_count == motion.channels.size() && !motion.channels.empty(),
            "Player-scene motion channel records are unsupported.");
        const MotionTiming timing{
            decode_float(motion.start_bits),
            decode_float(motion.end_bits),
            decode_float(motion.frame_rate_bits),
        };
        require(
            std::isfinite(timing.start) && std::isfinite(timing.end) && std::isfinite(timing.rate) &&
                timing.rate > 0.0f && timing.start == specification.start_frame &&
                timing.end == specification.end_frame,
            "Player-scene motion range is unsupported.");

        std::vector<float> evaluation_frames{timing.start};
        evaluation_frames.reserve(motion.channels.size() * 2u + 1u);
        for (const NnMotionChannel& channel : motion.channels) {
            if (channel.flags == 0u) continue;
            const float channel_start = decode_float(channel.start_bits);
            const float channel_end = decode_float(channel.end_bits);
            require(
                std::isfinite(channel_start) && std::isfinite(channel_end) && channel_start <= channel_end,
                "Player-scene motion channel range is unsupported.");
            if (timing.start <= channel_start && channel_start < timing.end) {
                evaluation_frames.push_back(channel_start);
            }
            if (timing.start <= channel_end && channel_end < timing.end) {
                evaluation_frames.push_back(channel_end);
            }
        }
        std::sort(evaluation_frames.begin(), evaluation_frames.end());
        evaluation_frames.erase(
            std::unique(evaluation_frames.begin(), evaluation_frames.end()),
            evaluation_frames.end());
        for (const float frame : evaluation_frames) {
            validate_player_motion_evaluation(model, motion, frame);
        }
        return timing;
    }

    void select_player_motion(
        std::map<std::string, PlayerMotion>::const_iterator selected) {
        std::string name(selected->first);
        player_motion_name.swap(name);
        player_motion_start = selected->second.timing.start;
        player_motion_end = selected->second.timing.end;
        player_motion_rate = selected->second.timing.rate;
        selected_player_motion = &selected->second;
    }

    void clear_player_blend_report() {
        player_secondary_motion_name.clear();
        player_secondary_motion_start = 0.0f;
        player_secondary_motion_end = 0.0f;
        player_secondary_motion_rate = 0.0f;
        player_secondary_motion_frame = 0.0f;
        player_secondary_motion_channel_count = 0u;
        player_blend_weight = 0.0f;
        player_blend_active = false;
    }

    std::map<std::string, PlayerMotion>::const_iterator load_player_motion(
        const std::string& motion_name) {
        require(motion_name == basename(motion_name), "Player-scene motion name is unsupported.");
        const PlayerMotionSpec* specification = find_player_motion_spec(motion_name);
        require(specification != nullptr, "Player-scene motion name is unsupported.");

        const auto cached = player_motion_cache.find(motion_name);
        if (cached != player_motion_cache.end()) {
            return cached;
        }

        require(player_motion_archive != nullptr, "Player-scene motion archive is unavailable.");
        const ByteView bytes = player_motion_archive->named(motion_name);
        PlayerMotion candidate{};
        candidate.data = parse_pc_nn_motion(bytes.data, bytes.size);
        candidate.model = specification->model;
        candidate.timing = validate_player_motion(
            player_character_model(candidate.model),
            candidate.data,
            *specification);
        const auto inserted = player_motion_cache.emplace(motion_name, std::move(candidate));
        require(inserted.second, "Player-scene motion cache is inconsistent.");
        return inserted.first;
    }

    std::map<std::string, PlayerMotion>::const_iterator load_player_motion_layer(
        const SonicGroundMotionLayer& layer,
        float& sample_frame) {
        const std::string motion_name(sonic_ground_motion_name(layer.motion));
        const auto selected = load_player_motion(motion_name);
        require(
            std::isfinite(layer.start_frame) && std::isfinite(layer.end_frame) &&
                layer.start_frame == selected->second.timing.start &&
                layer.end_frame == selected->second.timing.end,
            "Player-scene animation layer range is unsupported.");
        sample_frame = sonic_ground_motion_sample_frame(layer, kPrecision);
        require(
            std::isfinite(sample_frame) && sample_frame >= selected->second.timing.start &&
                sample_frame < selected->second.timing.end,
            "Player-scene animation sample frame is unsupported.");
        return selected;
    }

    void set_player_motion(const std::string& motion_name) {
        require(player_scene && !character_inspection, "Player-scene renderer was not selected at construction.");
        if (motion_name == player_motion_name) return;
        select_player_motion(load_player_motion(motion_name));
    }

    void set_ring_instances(const std::vector<Matrix>& worlds) {
        require(
            player_scene && !character_inspection && ring_resources_prepared,
            "Player-scene renderer was not selected at construction.");
        for (const Matrix& world : worlds) {
            for (float value : world) {
                require(std::isfinite(value), "Ring instance matrix contains a nonfinite value.");
            }
        }
        std::vector<Matrix> replacement(worlds);
        ring_instances.swap(replacement);
    }

    void set_ring_effect_sprites(const std::vector<AmeRuntimeSprite>& sprites) {
        require(
            player_scene && !character_inspection && ring_effect_resources_prepared,
            "Player-scene ring effect renderer was not selected at construction.");
        for (const AmeRuntimeSprite& sprite : sprites) {
            const bool is_simple = sprite.type == kRingEffectSimpleSpriteType;
            const bool is_twisted = sprite.type == kRingEffectSpriteType;
            require(is_simple || is_twisted, "Ring effect sprite type is unsupported.");
            require((sprite.node_flags & kRingEffectPCTFlag) != 0u, "Ring effect sprite vertex format is unsupported.");
            require(
                (is_simple && sprite.node_flags == kRingEffectSimpleFlags) ||
                    (is_twisted && (sprite.node_flags == kRingEffectSimpleFlags ||
                                    sprite.node_flags == kRingEffectSpriteFlags)),
                "Ring effect sprite flags are unsupported.");
            require(
                sprite.texture_slot == 0 &&
                    ((is_simple && sprite.texture_id == static_cast<std::int16_t>(kRingEffectTextureOneIndex)) ||
                     (is_twisted && sprite.texture_id == static_cast<std::int16_t>(kRingEffectTextureTwoIndex))),
                "Ring effect sprite texture is unsupported.");
            require(sprite.blend == kRingEffectBlend, "Ring effect sprite blend mode is unsupported.");
        }
        std::vector<AmeRuntimeSprite> replacement(sprites);
        std::vector<NativePlayerEffectFrame> frames;
        if (!sprites.empty()) frames.push_back({NativePlayerEffectKind::Ring, 0u, sprites, {}});
        ring_effect_sprites.swap(replacement);
        player_effect_frames.swap(frames);
    }

    void set_player_effects(const std::vector<NativePlayerEffectFrame>& effects) {
        std::vector<NativePlayerEffectFrame> replacement(effects);
        std::vector<AmeRuntimeSprite> rings;
        for (const auto& effect : replacement) {
            if (effect.kind == NativePlayerEffectKind::Ring) {
                require(effect.lines.empty(), "Ring effect line particles are unsupported.");
                rings.insert(rings.end(), effect.sprites.begin(), effect.sprites.end());
                continue;
            }
            require(effect.kind == NativePlayerEffectKind::JumpDash, "Player effect kind is unsupported.");
            for (const auto& sprite : effect.sprites) {
                require(sprite.type == 0x200u &&
                    (sprite.node_flags == 0x0b003001u || sprite.node_flags == 0x0b001001u) &&
                    sprite.texture_slot == 0 && sprite.texture_id == 13 && sprite.blend == 0xa2,
                    "Jump-dash sprite profile is unsupported.");
            }
            for (const auto& line : effect.lines) {
                require(line.node_flags == 0x0b001001u && line.texture_slot == 0 &&
                    line.texture_id == 26 && line.blend == 0xa2,
                    "Jump-dash line profile is unsupported.");
            }
        }
        set_ring_effect_sprites(rings);
        player_effect_frames.swap(replacement);
    }

    void prepare_character(const std::filesystem::path& data_root) {
        const Archive model_archive(data_root / "G_COM/PLY/SON_MDL.AMB");
        const Archive texture_archive(data_root / "G_COM/PLY/SON_TEX.AMB");
        Archive motion_archive(data_root / "G_COM/PLY/SON_MTN.AMB");
        const Archive shader_archive(data_root / "NNSTDSHADER/SHADER.AMB");
        const std::size_t textures_before = textures.size();
        const std::size_t pipelines_before = pipelines.size();

        const auto model_bytes = model_archive.named("SON_MODEL.ZNO");
        const auto motion_bytes = motion_archive.named("SON_RUN.ZNM");
        character_model = parse_pc_nn_model(model_bytes.data, model_bytes.size);
        character_motion = parse_pc_nn_motion(motion_bytes.data, motion_bytes.size);
        require(
            character_model.node_count == kCharacterNodeCount &&
                character_model.nodes.size() == kCharacterNodeCount,
            "Character model node count is unsupported.");
        require(
            character_model.vertices.size() == kCharacterVertexListCount,
            "Character model vertex-list count is unsupported.");
        require(
            character_model.texture_count == kCharacterTextureCount &&
                character_model.textures.size() == kCharacterTextureCount,
            "Character model texture count is unsupported.");
        require(
            character_model.material_count == character_model.materials.size() &&
                character_model.matrix_palette_count != 0u,
            "Character model records are unsupported.");
        require(
            character_motion.channel_count == character_motion.channels.size() &&
                !character_motion.channels.empty(),
            "Character RUN motion channel records are unsupported.");
        character_motion_start = decode_float(character_motion.start_bits);
        character_motion_end = decode_float(character_motion.end_bits);
        character_motion_rate = decode_float(character_motion.frame_rate_bits);
        require(
            std::isfinite(character_motion_start) && std::isfinite(character_motion_end) &&
                std::isfinite(character_motion_rate) && character_motion_rate > 0.0f &&
                character_motion_start >= 0.0f && character_motion_start <= character_motion_end &&
                character_motion_end <= 60.0f,
            "Character RUN motion range is unsupported.");
        character_frame = fixed_character_frame.value_or(character_motion_start);

        for (const NnTextureData& texture : character_model.textures) {
            const std::string texture_name = basename(texture.name);
            require(
                !texture_name.empty() && texture.raw_words[0u] == 0u && texture.raw_words[3u] == 0u &&
                    texture.raw_words[4u] == 0u && textures.find(texture_name) == textures.end(),
                "Character texture metadata is unsupported.");
            auto resource = renderer.create_dds_texture(texture_archive.named(texture_name));
            textures.emplace(texture_name, std::move(resource));
        }

        const RenderSceneData scene = build_render_scene(character_model);
        require(scene.draw_packets.size() == kCharacterMeshPacketCount, "Character mesh packet count is unsupported.");
        std::vector<bool> used_vertex_lists(kCharacterVertexListCount, false);
        character_meshes.reserve(scene.draw_packets.size());
        for (const RenderDrawPacket& packet : scene.draw_packets) {
            require(
                packet.source_vertex_list_index < used_vertex_lists.size(),
                "Character packet vertex-list index is unsupported.");
            Mesh mesh = prepare_character_mesh(character_model, packet, shader_archive, PlayerModelKind::Standard);
            require(
                character_triangle_count <= std::numeric_limits<std::size_t>::max() - mesh.triangle_count,
                "Character triangle count overflows.");
            used_vertex_lists[packet.source_vertex_list_index] = true;
            character_triangle_count += mesh.triangle_count;
            character_meshes.push_back(std::move(mesh));
        }
        for (bool used : used_vertex_lists) {
            require(used, "Character packet set does not use every weighted vertex list.");
        }
        std::set<const Pipeline*> character_pipelines;
        for (const Mesh& mesh : character_meshes) {
            character_pipelines.insert(mesh.pipeline);
        }
        character_texture_cache_delta = textures.size() - textures_before;
        character_pipeline_cache_delta = pipelines.size() - pipelines_before;
        character_pipeline_count = character_pipelines.size();
        require(
            character_meshes.size() == kCharacterMeshPacketCount &&
                character_texture_cache_delta == kCharacterTextureCount &&
                character_pipeline_count == 2u,
            "Character inspection resource set is unsupported.");
        if (player_scene) {
            prepare_spin_character(model_archive, texture_archive, shader_archive);
            const PlayerMotionSpec* specification = find_player_motion_spec("SON_RUN.ZNM");
            require(specification != nullptr, "Player-scene RUN motion specification is unavailable.");
            PlayerMotion initial_motion{};
            initial_motion.data = character_motion;
            initial_motion.model = specification->model;
            initial_motion.timing = validate_player_motion(
                player_character_model(initial_motion.model),
                initial_motion.data,
                *specification);
            const auto inserted = player_motion_cache.emplace("SON_RUN.ZNM", std::move(initial_motion));
            require(inserted.second, "Player-scene RUN motion cache is inconsistent.");
            select_player_motion(inserted.first);
            player_motion_archive = std::make_unique<Archive>(std::move(motion_archive));
            const auto spin_motion = load_player_motion("SON_SPIN01.ZNM");
            require(
                spin_motion->second.model == PlayerModelKind::Spin &&
                    spin_motion->second.data.channel_count == kSpinCharacterMotionChannelCount,
                "Spin character motion is unsupported.");
        }
    }

    void prepare_spin_character(
        const Archive& model_archive,
        const Archive& texture_archive,
        const Archive& shader_archive) {
        require(
            spin_character_model.nodes.empty() && spin_character_meshes.empty(),
            "Spin character resources were prepared more than once.");
        const ByteView model_bytes = model_archive.named("SON_SPINMODEL.ZNO");
        spin_character_model = parse_pc_nn_model(model_bytes.data, model_bytes.size);
        require(
            spin_character_model.node_count == kSpinCharacterNodeCount &&
                spin_character_model.nodes.size() == kSpinCharacterNodeCount &&
                spin_character_model.matrix_palette_count == kSpinCharacterMatrixPaletteCount &&
                spin_character_model.vertices.size() == kSpinCharacterVertexListCount &&
                spin_character_model.material_count == 1u && spin_character_model.materials.size() == 1u &&
                spin_character_model.texture_count == kSpinCharacterTextureCount &&
                spin_character_model.textures.size() == kSpinCharacterTextureCount &&
                spin_character_model.primitives.size() == kSpinCharacterMeshPacketCount &&
                spin_character_model.sub_objects.size() == 1u &&
                spin_character_model.sub_objects[0u].meshes.size() == kSpinCharacterMeshPacketCount,
            "Spin character model records are unsupported.");
        const NnTextureData& texture = spin_character_model.textures[0u];
        const std::string texture_name = basename(texture.name);
        require(
            texture_name == kSpinCharacterTextureName && texture.raw_words[0u] == 0u &&
                texture.raw_words[3u] == 0u && texture.raw_words[4u] == 0u &&
                textures.find(texture_name) == textures.end(),
            "Spin character texture metadata is unsupported.");
        auto texture_resource = renderer.create_dds_texture(texture_archive.named(texture_name));
        const auto texture_inserted = textures.emplace(texture_name, std::move(texture_resource));
        require(texture_inserted.second, "Spin character texture cache is inconsistent.");

        const RenderSceneData scene = build_render_scene(spin_character_model);
        require(
            scene.draw_packets.size() == kSpinCharacterMeshPacketCount,
            "Spin character mesh packet count is unsupported.");
        std::vector<bool> used_vertex_lists(kSpinCharacterVertexListCount, false);
        std::size_t triangle_count = 0u;
        spin_character_meshes.reserve(scene.draw_packets.size());
        for (const RenderDrawPacket& packet : scene.draw_packets) {
            require(
                packet.source_vertex_list_index < used_vertex_lists.size(),
                "Spin character packet vertex-list index is unsupported.");
            Mesh mesh = prepare_character_mesh(spin_character_model, packet, shader_archive, PlayerModelKind::Spin);
            require(
                triangle_count <= std::numeric_limits<std::size_t>::max() - mesh.triangle_count,
                "Spin character triangle count overflows.");
            used_vertex_lists[packet.source_vertex_list_index] = true;
            triangle_count += mesh.triangle_count;
            spin_character_meshes.push_back(std::move(mesh));
        }
        for (bool used : used_vertex_lists) {
            require(used, "Spin character packet set does not use every weighted vertex list.");
        }
        require(
            spin_character_meshes.size() == kSpinCharacterMeshPacketCount &&
                triangle_count == kSpinCharacterTriangleCount,
            "Spin character mesh geometry is unsupported.");
    }

    Mesh prepare_character_mesh(
        const NnModelData& model,
        const RenderDrawPacket& packet,
        const Archive& shader_archive,
        PlayerModelKind model_kind) {
        const NnSubObjectData& subobject = model.sub_objects.at(packet.source_sub_object_index);
        const NnMaterialData& material = model.materials.at(packet.material_index);
        const NnVertexData& vertex = model.vertices.at(packet.source_vertex_list_index);
        const bool spin_model = model_kind == PlayerModelKind::Spin;
        require(
            model_kind == PlayerModelKind::Standard || spin_model,
            "Character model kind is unsupported.");
        require(
            subobject.flags == kCharacterSubobjectFlags && packet.matrix_index == -1,
            "Character subobject or mesh palette mode is unsupported.");
        const bool indexed_layout = vertex.format == kCharacterIndexedVertexFormat;
        require(
            indexed_layout || vertex.format == kCharacterNonindexedVertexFormat,
            "Character vertex format is unsupported.");
        require(!spin_model || indexed_layout, "Spin character vertex format is unsupported.");
        const std::uint32_t expected_fvf = indexed_layout ? kCharacterIndexedFvf : kCharacterNonindexedFvf;
        const std::uint32_t expected_stride = indexed_layout ? kCharacterIndexedStride : kCharacterNonindexedStride;
        const std::uint32_t matrix_capacity = indexed_layout ? 16u : 4u;
        require(
            vertex.fvf == expected_fvf && vertex.stride == expected_stride && vertex.count != 0u &&
                vertex.attributes.size() == static_cast<std::size_t>(vertex.count) &&
                static_cast<std::size_t>(vertex.count) <=
                    std::numeric_limits<std::size_t>::max() / static_cast<std::size_t>(vertex.stride) &&
                vertex.bytes.size() == static_cast<std::size_t>(vertex.count) * vertex.stride,
            "Character raw vertex storage is unsupported.");
        require(
            packet.vertices.size() == static_cast<std::size_t>(vertex.count) &&
                packet.matrix_subset == vertex.matrix_indices && !packet.matrix_subset.empty() &&
                packet.matrix_subset.size() <= matrix_capacity && !packet.indices.empty() &&
                packet.indices.size() % 3u == 0u &&
                packet.indices.size() <= std::numeric_limits<std::uint32_t>::max() &&
                packet.indices.size() <= std::numeric_limits<std::size_t>::max() / sizeof(std::uint32_t),
            "Character packet geometry or matrix subset is unsupported.");
        for (const NnVertexAttributes& attributes : vertex.attributes) {
            require(
                attributes.weight_bits.size() == 3u && attributes.normal_bits.has_value() &&
                    attributes.texcoord_bits.has_value() &&
                    attributes.blend_indices.has_value() == indexed_layout,
                "Character weighted vertex attributes are unsupported.");
        }
        for (std::uint32_t index : packet.indices) {
            require(index < vertex.count, "Character triangle index is outside its raw vertex list.");
        }

        if (spin_model) {
            require(
                material.pointer_flags == 0x10000000u && !material.user_profile.has_value() &&
                    material.stages.size() == 1u,
                "Spin character lit material is unsupported.");
        } else {
            require(
                material.pointer_flags == 0x30000000u && material.user_profile.has_value() &&
                    *material.user_profile == 1u && material.stages.size() == 1u,
                "Character lit material is unsupported.");
        }
        const NnMaterialProfileContext context{
            subobject.flags,
            vertex.format,
            kStageLightDrawFlags,
            kCharacterDrawFlagsHigh,
            1u,
        };
        const NnShaderProfileKeyInput profile = nn_lit_material_profile(material, context);
        if (spin_model) {
            require(
                profile.transform_mode == 2u && profile.lighting_features == 1u && profile.parallel_lights == 2u &&
                    profile.drawobject_profile == 2u && profile.user_profile == 0u && profile.base_map &&
                    profile.texture_coordinates[0u] == 0u,
                "Spin character lit shader profile is unsupported.");
        } else {
            require(
                profile.transform_mode == (indexed_layout ? 2u : 1u) && profile.lighting_features == 1u &&
                    profile.parallel_lights == 2u && profile.drawobject_profile == 2u && profile.user_profile == 1u,
                "Character lit shader profile is unsupported.");
        }

        Mesh result;
        result.lit = true;
        result.material = nn_lit_material_constants(material, context, global_ambient, kPrecision);
        result.state = nn_lit_material_state(material, context, initial_state(), false).render_state;
        auto texture_context = context;
        texture_context.draw_flags_low = 0u;
        texture_context.draw_flags_high = 0u;
        result.texture_columns = nn_shader_matrix_columns(
            nn_unlit_texture_matrix(material.stages.at(0u), texture_context));
        const NnMaterialStageData& stage = material.stages.at(0u);
        require(stage.texture_index >= 0, "Character material texture index is negative.");
        const std::size_t texture_index = static_cast<std::size_t>(stage.texture_index);
        require(texture_index < model.textures.size(), "Character material texture index is out of range.");
        const NnTextureData& texture = model.textures.at(texture_index);
        const std::string texture_name = basename(texture.name);
        const auto texture_resource = textures.find(texture_name);
        require(texture_resource != textures.end(), "Character material texture was not loaded.");
        result.texture = texture_resource->second.get();
        result.sampler = {TextureAddressMode::Wrap, TextureAddressMode::Wrap, TextureAddressMode::Wrap,
            TextureFilter::Point, TextureFilter::Point, TextureFilter::None, 1u, 0u, 0.0f, 0u, false};
        set_txb_filter_modes(
            result.sampler,
            static_cast<std::uint16_t>(texture.raw_words[2u] & 0xffffu),
            static_cast<std::uint16_t>(texture.raw_words[2u] >> 16u));

        VertexDeclarationDescription declaration{{
            {0u, 0u, VertexElementType::Float3, VertexElementUsage::Position, 0u},
            {0u, 12u, VertexElementType::Float3, VertexElementUsage::BlendWeight, 0u},
        }};
        if (indexed_layout) {
            declaration.elements.push_back({0u, 24u, VertexElementType::UByte4, VertexElementUsage::BlendIndices, 0u});
        }
        declaration.elements.push_back({
            0u,
            static_cast<std::uint16_t>(indexed_layout ? 28u : 24u),
            VertexElementType::Float3,
            VertexElementUsage::Normal,
            0u,
        });
        declaration.elements.push_back({
            0u,
            static_cast<std::uint16_t>(indexed_layout ? 40u : 36u),
            VertexElementType::Float2,
            VertexElementUsage::TextureCoordinate,
            0u,
        });
        const std::string shader_name = nn_shader_archive_basename(nn_shader_profile_key(profile));
        require(
            !spin_model || shader_name == "0000000000080RDMRC00002264",
            "Spin character lit shader selection is unsupported.");
        auto& pipeline = pipelines[shader_name];
        if (!pipeline) {
            pipeline = std::make_unique<Pipeline>(
                renderer,
                shader_archive.named(shader_name + ".VSH"),
                shader_archive.named(shader_name + ".PSH"));
        }
        result.declaration = renderer.create_vertex_declaration(declaration);
        result.vertices = renderer.create_vertex_buffer({
            {vertex.bytes.data(), vertex.bytes.size()},
            vertex.stride,
            false,
        });
        result.indices = renderer.create_index_buffer({
            {reinterpret_cast<const std::uint8_t*>(packet.indices.data()),
             packet.indices.size() * sizeof(std::uint32_t)},
            IndexFormat::UInt32,
            false,
        });
        result.pipeline = pipeline.get();
        result.matrix_subset = packet.matrix_subset;
        result.matrix_capacity = matrix_capacity;
        result.vertex_count = vertex.count;
        result.triangle_count = static_cast<std::uint32_t>(packet.indices.size() / 3u);
        return result;
    }

    Mesh prepare_mesh(const NnModelData& model, std::uint32_t archive_flags, const RenderDrawPacket& packet,
                      const Archive& texture_archive, const Archive& shader_archive) {
        const auto& subobject = model.sub_objects.at(packet.source_sub_object_index);
        const auto& material = model.materials.at(packet.material_index);
        const auto& vertex = model.vertices.at(packet.source_vertex_list_index);
        const bool lit = material.pointer_flags == 0x30000000u;
        const auto light_draw_flags = (archive_flags & 0x80u) != 0u ? 0u : kStageLightDrawFlags;
        const NnMaterialProfileContext context{subobject.flags, vertex.format, lit ? light_draw_flags : 0u, 0u, 1u};
        const auto profile = lit ? nn_lit_material_profile(material, context) : nn_unlit_material_profile(material, context);
        Mesh result;
        result.lit = lit;
        if (lit) {
            result.material = nn_lit_material_constants(material, context, global_ambient, kPrecision);
            result.state = nn_lit_material_state(material, context, initial_state(), false).render_state;
        } else {
            const auto unlit = nn_unlit_material_constants(material, context);
            result.material.diffuse = unlit.diffuse;
            result.material.base_alpha = unlit.base_alpha;
            result.state = nn_unlit_material_state(material, context, initial_state(), false).render_state;
        }
        result.state.rasterizer.cull_mode = CullMode::None;
        auto texture_context = context;
        texture_context.draw_flags_low = 0u;
        result.texture_columns = nn_shader_matrix_columns(nn_unlit_texture_matrix(material.stages.at(0u), texture_context));
        require(packet.matrix_index >= 0 && static_cast<std::uint32_t>(packet.matrix_index) < model.matrix_palette_count,
                "Static mesh palette binding is unsupported.");
        result.palette_index = static_cast<std::size_t>(packet.matrix_index);
        require(!packet.vertices.empty() && !packet.indices.empty() && packet.indices.size() % 3u == 0u,
                "Static mesh geometry is empty or not triangular.");
        require(packet.vertices.size() <= std::numeric_limits<std::uint32_t>::max() &&
                packet.indices.size() <= std::numeric_limits<std::uint32_t>::max(), "Static mesh geometry is too large.");
        require(!lit || packet.vertices.front().normal.has_value(), "Lit static mesh normals are missing.");
        const std::string shader_name = nn_shader_archive_basename(nn_shader_profile_key(profile));
        auto& pipeline = pipelines[shader_name];
        if (!pipeline) pipeline = std::make_unique<Pipeline>(renderer,
            shader_archive.named(shader_name + ".VSH"), shader_archive.named(shader_name + ".PSH"));
        result.pipeline = pipeline.get();
        const auto& texture = model.textures.at(static_cast<std::size_t>(material.stages.at(0u).texture_index));
        const auto texture_name = basename(texture.name);
        require(texture.raw_words[0u] == 0u && texture.raw_words[3u] == 0u && texture.raw_words[4u] == 0u,
                "Embedded texture preparation fields are unsupported.");
        auto& texture_resource = textures[texture_name];
        if (!texture_resource) texture_resource = renderer.create_dds_texture(texture_archive.named(texture_name));
        result.texture = texture_resource.get();
        result.shadow = texture_name.find("SHADOW") != std::string::npos;
        result.sampler = {TextureAddressMode::Wrap, TextureAddressMode::Wrap, TextureAddressMode::Wrap,
            TextureFilter::Point, TextureFilter::Point, TextureFilter::None, 1u, 0u, 0.0f, 0u, false};
        set_txb_filter_modes(result.sampler, static_cast<std::uint16_t>(texture.raw_words[2u] & 0xffffu),
                            static_cast<std::uint16_t>(texture.raw_words[2u] >> 16u));
        const auto& first = packet.vertices.front();
        VertexDeclarationDescription declaration{{{0u, 0u, VertexElementType::Float3, VertexElementUsage::Position, 0u}}};
        if (first.color_rgba) declaration.elements.push_back({0u, 12u, VertexElementType::Color, VertexElementUsage::Color, 0u});
        if (first.texcoord) declaration.elements.push_back({0u, 16u, VertexElementType::Float2, VertexElementUsage::TextureCoordinate, 0u});
        if (first.normal) declaration.elements.push_back({0u, 24u, VertexElementType::Float3, VertexElementUsage::Normal, 0u});
        require(first.texcoord.has_value() && material.stages[0u].raw_words[2u] == 0u,
                "Static mesh texture coordinates are unsupported.");
        require(!first.blend_indices && first.stored_weights.empty(),
                "Static mesh skinning attributes are unsupported.");
        std::vector<Vertex> vertices;
        vertices.reserve(packet.vertices.size());
        for (const auto& input : packet.vertices) {
            require(input.color_rgba.has_value() == first.color_rgba.has_value() &&
                    input.texcoord.has_value() == first.texcoord.has_value() &&
                    input.normal.has_value() == first.normal.has_value(), "Static mesh vertex layouts differ.");
            vertices.push_back({input.position, input.color_rgba.value_or(std::array<std::uint8_t, 4u>{}),
                               *input.texcoord, input.normal.value_or(std::array<float, 3u>{})});
        }
        result.declaration = renderer.create_vertex_declaration(declaration);
        result.vertices = renderer.create_vertex_buffer({{reinterpret_cast<const std::uint8_t*>(vertices.data()),
            vertices.size() * sizeof(Vertex)}, sizeof(Vertex), false});
        result.indices = renderer.create_index_buffer({{reinterpret_cast<const std::uint8_t*>(packet.indices.data()),
            packet.indices.size() * sizeof(std::uint32_t)}, IndexFormat::UInt32, false});
        result.vertex_count = static_cast<std::uint32_t>(vertices.size());
        result.triangle_count = static_cast<std::uint32_t>(packet.indices.size() / 3u);
        return result;
    }

    Mesh prepare_ring_mesh(
        const NnModelData& model,
        const RenderDrawPacket& packet,
        const Archive& texture_archive,
        const Archive& shader_archive) {
        const NnSubObjectData& subobject = model.sub_objects.at(packet.source_sub_object_index);
        const NnMaterialData& material = model.materials.at(packet.material_index);
        const NnVertexData& vertex = model.vertices.at(packet.source_vertex_list_index);
        require(
            packet.source_sub_object_index == 0u && packet.source_mesh_index == 0u &&
                packet.source_vertex_list_index == 0u && packet.source_primitive_list_index == 0u &&
                packet.material_index == 0u && packet.node_index == 0 && packet.matrix_index == 0 &&
                packet.matrix_subset.empty(),
            "Ring mesh references are unsupported.");
        require(
            subobject.meshes.size() == 1u && vertex.format == kRingVertexFormat && vertex.fvf == kRingFvf &&
                vertex.stride == kRingVertexStride && vertex.count == kRingVertexCount &&
                vertex.attributes.size() == kRingVertexCount && vertex.matrix_indices.empty() &&
                vertex.bytes.size() == kRingVertexCount * kRingVertexStride,
            "Ring vertex layout is unsupported.");
        require(
            packet.vertices.size() == kRingVertexCount && packet.indices.size() == kRingTriangleCount * 3u,
            "Ring packet geometry is unsupported.");
        for (const RenderVertexData& input : packet.vertices) {
            require(
                input.normal.has_value() && !input.texcoord.has_value() && !input.color_rgba.has_value() &&
                    !input.specular_rgba.has_value() && input.stored_weights.empty() && !input.blend_indices.has_value(),
                "Ring vertex attributes are unsupported.");
        }
        for (const std::uint32_t index : packet.indices) {
            require(index < kRingVertexCount, "Ring triangle index is outside its vertex list.");
        }
        require(
            material.pointer_flags == 0x10000000u && !material.user_profile.has_value() &&
                material.descriptor_bits == kRingMaterialDescriptor && material.stages.size() == 1u,
            "Ring material metadata is unsupported.");
        const NnMaterialStageData& stage = material.stages.at(0u);
        require(
            stage.texture_index == 0 && stage.raw_words.size() == kRingMaterialStage.size() &&
                std::equal(stage.raw_words.begin(), stage.raw_words.end(), kRingMaterialStage.begin()),
            "Ring material stage is unsupported.");

        const NnMaterialProfileContext context{
            subobject.flags,
            vertex.format,
            kRingDrawFlagsLow,
            kRingDrawFlagsHigh,
            1u,
        };
        const NnShaderProfileKeyInput profile = nn_lit_material_profile(material, context);
        require(
            profile.vertex_features == 0x4u && profile.transform_mode == 0u && profile.lighting_features == 3u &&
                profile.parallel_lights == 2u && profile.drawobject_profile == 0u && profile.user_profile == 0u &&
                profile.base_map && profile.texture_coordinates[0u] == -1,
            "Ring lit shader profile is unsupported.");
        const std::string shader_name = nn_shader_archive_basename(nn_shader_profile_key(profile));
        require(shader_name == kRingShaderName, "Ring lit shader selection is unsupported.");

        Mesh result;
        result.lit = true;
        result.material = nn_lit_material_constants(material, context, global_ambient, kPrecision);
        result.state = nn_lit_material_state(material, context, initial_state(), false).render_state;
        result.texture_columns = nn_shader_matrix_columns(kIdentity);
        result.palette_index = 0u;
        result.sampler = {TextureAddressMode::Wrap, TextureAddressMode::Wrap, TextureAddressMode::Wrap,
            TextureFilter::Point, TextureFilter::Point, TextureFilter::None, 1u, 0u, 0.0f, 0u, false};
        const NnTextureData& texture = model.textures.at(0u);
        const std::string texture_name = basename(texture.name);
        require(
            texture_name == kRingTextureName && texture.raw_words[0u] == 0u && texture.raw_words[3u] == 0u &&
                texture.raw_words[4u] == 0u,
            "Ring texture metadata is unsupported.");
        set_txb_filter_modes(
            result.sampler,
            static_cast<std::uint16_t>(texture.raw_words[2u] & 0xffffu),
            static_cast<std::uint16_t>(texture.raw_words[2u] >> 16u));
        auto& texture_resource = textures[texture_name];
        if (!texture_resource) {
            texture_resource = renderer.create_dds_texture(texture_archive.named(texture_name));
        }
        auto& pipeline = pipelines[shader_name];
        if (!pipeline) {
            pipeline = std::make_unique<Pipeline>(
                renderer,
                shader_archive.named(shader_name + ".VSH"),
                shader_archive.named(shader_name + ".PSH"));
        }
        const VertexDeclarationDescription declaration{{
            {0u, 0u, VertexElementType::Float3, VertexElementUsage::Position, 0u},
            {0u, 12u, VertexElementType::Float3, VertexElementUsage::Normal, 0u},
        }};
        result.declaration = renderer.create_vertex_declaration(declaration);
        result.vertices = renderer.create_vertex_buffer({
            {vertex.bytes.data(), vertex.bytes.size()},
            vertex.stride,
            false,
        });
        result.indices = renderer.create_index_buffer({
            {reinterpret_cast<const std::uint8_t*>(packet.indices.data()), packet.indices.size() * sizeof(std::uint32_t)},
            IndexFormat::UInt32,
            false,
        });
        result.pipeline = pipeline.get();
        result.texture = texture_resource.get();
        result.vertex_count = static_cast<std::uint32_t>(kRingVertexCount);
        result.triangle_count = static_cast<std::uint32_t>(kRingTriangleCount);
        return result;
    }

    void prepare_ring(const std::filesystem::path& data_root) {
        require(
            !ring_resources_prepared && ring_model.data.nodes.empty() && ring_model.meshes.empty(),
            "Ring resources were prepared more than once.");
        const Archive model_archive(data_root / "G_COM/RING/RING_MDL.AMB");
        const Archive texture_archive(data_root / "G_COM/RING/RING_TEX.AMB");
        const Archive shader_archive(data_root / "NNSTDSHADER/SHADER.AMB");
        const ByteView model_bytes = model_archive.named(std::string(kRingModelName));
        const Archive map_archive(data_root / "G_ZONE1/MAP/ZONE11_MAP.AMB");
        require(map_archive.entries.size() > 7u, "Ring ambient event resource is unavailable.");
        const ByteView event_bytes = map_archive.at(map_archive.entries[7u]);
        const StageEventPlacements events = parse_stage_event_placements(event_bytes.data, event_bytes.size);
        require(std::any_of(events.events.begin(), events.events.end(), [](const StageEventPlacement& event) {
            return event.object_id == 475u && (event.flags & 0x8000u) != 0u;
        }), "Ring ambient field manager is unavailable.");
        ring_ambient_points = make_ambient_field_points(events);
        const auto environment_path = data_root / "G_ZONE1/STENV/STAGE_ENV_ZONE1.GPB";
        std::ifstream environment_file(environment_path, std::ios::binary | std::ios::ate);
        if (!environment_file) throw std::runtime_error("Cannot open original stage environment: " + environment_path.u8string());
        const auto environment_size = environment_file.tellg();
        require(environment_size > 0 && environment_size <= 1024 * 1024, "Stage environment size is unsupported.");
        std::vector<std::uint8_t> environment_bytes(static_cast<std::size_t>(environment_size));
        environment_file.seekg(0);
        environment_file.read(reinterpret_cast<char*>(environment_bytes.data()), static_cast<std::streamsize>(environment_bytes.size()));
        if (!environment_file) throw std::runtime_error("Cannot read original stage environment: " + environment_path.u8string());
        ring_ambient_palettes = parse_pc_stage_ambient_palettes(environment_bytes.data(), environment_bytes.size(), 0u);
        ring_model.data = parse_pc_nn_model(model_bytes.data, model_bytes.size);
        require(
            ring_model.data.node_count == kRingNodeCount && ring_model.data.nodes.size() == kRingNodeCount &&
                ring_model.data.matrix_palette_count == 1u && ring_model.data.material_count == 1u &&
                ring_model.data.materials.size() == 1u && ring_model.data.texture_count == 1u &&
                ring_model.data.textures.size() == 1u && ring_model.data.vertices.size() == 1u &&
                ring_model.data.primitives.size() == 1u && ring_model.data.sub_objects.size() == 1u &&
                ring_model.data.sub_objects[0u].meshes.size() == 1u,
            "Ring model records are unsupported.");
        const NnNodeData& node = ring_model.data.nodes[0u];
        require(
            node.matrix_index == 0 && node.parent_index == -1 && node.child_index == -1 && node.sibling_index == -1,
            "Ring node layout is unsupported.");
        const NnPrimitiveData& primitive = ring_model.data.primitives[0u];
        require(
            primitive.mode == 0x4810u && primitive.strip_lengths.size() == 1u &&
                primitive.strip_lengths[0u] == kRingStripIndexCount &&
                primitive.indices.size() == kRingStripIndexCount,
            "Ring primitive layout is unsupported.");
        (void)nn_static_node_matrices(ring_model.data, kIdentity, kPrecision, backend);
        const RenderSceneData scene = build_render_scene(ring_model.data);
        require(scene.draw_packets.size() == 1u, "Ring draw packet count is unsupported.");
        ring_model.packet_count = scene.draw_packets.size();
        ring_model.meshes.push_back(prepare_ring_mesh(ring_model.data, scene.draw_packets[0u], texture_archive, shader_archive));
        require(
            ring_model.meshes.size() == 1u && ring_model.meshes[0u].vertex_count == kRingVertexCount &&
                ring_model.meshes[0u].triangle_count == kRingTriangleCount,
            "Ring mesh preparation is unsupported.");
        ring_resources_prepared = true;
    }

    void prepare_ring_effects(const std::filesystem::path& data_root) {
        require(
            !ring_effect_resources_prepared && ring_effect_declaration == nullptr && ring_effect_pipeline == nullptr &&
                ring_effect_textures[0u] == nullptr && ring_effect_textures[1u] == nullptr,
            "Ring effect resources were prepared more than once.");
        const Archive effect_archive(data_root / "G_COM/EFF/EP2_EFF_CMN.AMB");
        require(
            effect_archive.entries.size() > kRingEffectTextureArchiveIndex,
            "Ring effect texture archive is unavailable.");
        const Archive texture_archive(effect_archive.at(effect_archive.entries[kRingEffectTextureArchiveIndex]));
        require(
            texture_archive.entries.size() > 27u,
            "Ring effect texture records are unavailable.");
        const ByteView texture_list_bytes = texture_archive.at(texture_archive.entries[kRingEffectTextureListIndex]);
        const std::vector<TxbTextureData> texture_list = parse_pc_txb_texture_list(
            texture_list_bytes.data,
            texture_list_bytes.size);
        require(
            texture_list.size() > 26u,
            "Ring effect texture list is unavailable.");
        const TxbTextureData& texture_one = texture_list[kRingEffectTextureOneIndex];
        const TxbTextureData& texture_two = texture_list[kRingEffectTextureTwoIndex];
        const TxbTextureData& dash_line_texture = texture_list[26u];
        require(
            basename(texture_one.name) == kRingEffectTextureOneName && texture_one.min_filter == 1u &&
                texture_one.mag_filter == 1u && basename(texture_two.name) == kRingEffectTextureTwoName &&
                texture_two.min_filter == 1u && texture_two.mag_filter == 1u &&
                basename(dash_line_texture.name) == "HIT3.DDS" &&
                dash_line_texture.min_filter == 1u && dash_line_texture.mag_filter == 1u &&
                basename(texture_archive.entries[27u].name) == "HIT3.DDS",
            "Ring effect texture metadata is unsupported.");

        auto load_texture = [&](const TxbTextureData& texture, std::size_t archive_index) {
            auto& resource = textures[basename(texture.name)];
            if (!resource) {
                resource = renderer.create_dds_texture(texture_archive.at(texture_archive.entries[archive_index]));
            }
            return resource.get();
        };
        ring_effect_textures[0u] = load_texture(texture_one, kRingEffectTextureOneArchiveIndex);
        ring_effect_textures[1u] = load_texture(texture_two, kRingEffectTextureTwoArchiveIndex);
        ring_effect_textures[2u] = load_texture(dash_line_texture, 27u);

        const Archive shader_archive(data_root / "NNSTDSHADER/SHADER.AMB");
        require(
            shader_archive.entries.size() > kRingEffectPixelShaderIndex &&
                basename(shader_archive.entries[kRingEffectVertexShaderIndex].name) ==
                    std::string(kRingEffectShaderName) + ".VSH" &&
                basename(shader_archive.entries[kRingEffectPixelShaderIndex].name) ==
                    std::string(kRingEffectShaderName) + ".PSH",
            "Ring effect shader records are unavailable.");
        auto& pipeline = pipelines[std::string(kRingEffectShaderName)];
        if (!pipeline) {
            pipeline = std::make_unique<Pipeline>(
                renderer,
                shader_archive.at(shader_archive.entries[kRingEffectVertexShaderIndex]),
                shader_archive.at(shader_archive.entries[kRingEffectPixelShaderIndex]));
        }

        ring_effect_declaration = renderer.create_vertex_declaration({{
            {0u, 0u, VertexElementType::Float3, VertexElementUsage::Position, 0u},
            {0u, 12u, VertexElementType::Color, VertexElementUsage::Color, 0u},
            {0u, 16u, VertexElementType::Float2, VertexElementUsage::TextureCoordinate, 0u},
        }});
        ring_effect_sampler = {TextureAddressMode::Wrap, TextureAddressMode::Wrap, TextureAddressMode::Wrap,
            TextureFilter::Point, TextureFilter::Point, TextureFilter::None, 1u, 0u, 0.0f, 0u, false};
        set_txb_filter_modes(ring_effect_sampler, texture_one.min_filter, texture_one.mag_filter);
        ring_effect_state = initial_state();
        ring_effect_state.blend = {true, BlendFactor::SourceAlpha, BlendFactor::One, BlendOperation::Add,
            false, BlendFactor::One, BlendFactor::Zero, BlendOperation::Add, 0u};
        ring_effect_state.depth_stencil.depth_enable = true;
        ring_effect_state.depth_stencil.depth_write = false;
        ring_effect_state.depth_stencil.depth_compare = CompareFunction::LessEqual;
        ring_effect_pipeline = pipeline.get();
        ring_effect_resources_prepared = true;
    }

    void advance_character_frame(float delta_seconds) {
        require(
            std::isfinite(delta_seconds) && delta_seconds >= 0.0f,
            "Character inspection delta is invalid.");
        if (fixed_character_frame.has_value()) {
            return;
        }
        const float span = character_motion_end - character_motion_start;
        if (span == 0.0f) {
            character_frame = character_motion_start;
            return;
        }
        const float advanced = character_frame + delta_seconds * character_motion_rate;
        require(std::isfinite(advanced), "Character inspection frame advance is invalid.");
        float wrapped = std::fmod(advanced - character_motion_start, span);
        if (wrapped < 0.0f) {
            wrapped += span;
        }
        character_frame = character_motion_start + wrapped;
    }

    CharacterPoseEvaluation evaluate_character_poses(
        const NnModelData& model,
        const NnMotionData& motion,
        float motion_frame) {
        CharacterPoseEvaluation result{};
        result.poses.reserve(model.nodes.size());
        std::size_t channel_cursor = 0u;
        std::uint64_t digest = kDigestOffset;
        for (std::size_t node_index = 0u; node_index < model.nodes.size(); ++node_index) {
            require(
                node_index <= static_cast<std::size_t>(std::numeric_limits<std::int32_t>::max()),
                "Character motion node index is unsupported.");
            NnMotionLocalPose pose = nn_motion_local_pose(
                model.nodes[node_index],
                static_cast<std::int32_t>(node_index),
                motion,
                channel_cursor,
                motion_frame,
                kPrecision);
            require(
                pose.next_channel <= motion.channels.size() &&
                    pose.next_channel <= static_cast<std::size_t>(std::numeric_limits<std::uint32_t>::max()),
                "Character motion channel cursor is unsupported.");
            channel_cursor = pose.next_channel;
            for (float value : pose.translation) {
                mix_digest(digest, encode_float(value));
            }
            for (float value : pose.rotation) {
                mix_digest(digest, encode_float(value));
            }
            for (float value : pose.scale) {
                mix_digest(digest, encode_float(value));
            }
            mix_digest(digest, pose.translation_flags);
            mix_digest(digest, pose.rotation_flags);
            mix_digest(digest, pose.scale_flags);
            mix_digest(digest, static_cast<std::uint32_t>(channel_cursor));
            result.poses.push_back(std::move(pose));
        }
        result.final_channel_cursor = channel_cursor;
        result.digest = digest;
        return result;
    }

    void record_character_pose_evaluation(const CharacterPoseEvaluation& evaluation) {
        character_pose_nodes = evaluation.poses.size();
        character_final_channel_cursor = evaluation.final_channel_cursor;
        character_pose_digest = evaluation.digest;
    }

    FrameLighting make_frame_lighting(
        const Matrix& view, float far_plane, std::uint32_t light_mask = kStageLightMask) const {
        require(std::isfinite(far_plane) && far_plane > 0.0f, "Frame far plane is invalid.");
        FrameLighting result;
        result.fog.color[3u] = 1.0f;
        nn_apply_fog(result.fog, false, far_plane, NnFogPrecision::Single);
        result.fog_density = {{result.fog.active.density, 0, 0, 0}};
        result.fog_start = {{result.fog.active.start, 0, 0, 0}};
        result.fog_end = {{result.fog.active.end, 0, 0, 0}};
        result.fog_scale = {{result.fog.active.scale, 0, 0, 0}};
        result.lights = nn_parallel_lighting_constants(lighting.lights, light_mask, view, kPrecision);
        require(result.lights.count == 2u, "Frame parallel-light count is unsupported.");
        result.light_count = {{static_cast<float>(result.lights.count), 0, 0, 0}};
        return result;
    }

    void begin_render_frame() {
        frame_draws = frame_triangles = frame_non_shadow = frame_lit = 0u;
        character_frame_draws = character_frame_triangles = character_frame_non_shadow = character_frame_lit = 0u;
        ring_frame_draws = ring_frame_triangles = ring_frame_non_shadow = ring_frame_lit = 0u;
        ring_effect_frame_draws = ring_effect_frame_sprites = ring_effect_frame_triangles = 0u;
        dash_effect_frame_draws = dash_effect_frame_sprites = dash_effect_frame_lines = 0u;
        device.begin_frame({true, 0x31577fffu, true, 1.0f, true, 0u});
    }

    void draw_character_poses(
        const NnModelData& model,
        const std::vector<Mesh>& meshes,
        const Matrix& base,
        const std::vector<NnMotionLocalPose>& poses,
        const Matrix& projection,
        const FrameLighting& frame) {
        const NnMotionNodeMatrices matrices = nn_motion_node_matrices(
            model,
            poses,
            base,
            kPrecision,
            backend);
        require(
            matrices.node_world.size() == model.nodes.size() &&
                matrices.palette.size() == model.matrix_palette_count,
            "Character pose palette count is unsupported.");
        std::uint64_t palette_digest = kDigestOffset;
        for (const NnShaderMatrix& matrix : matrices.palette) {
            for (float value : matrix) {
                mix_digest(palette_digest, encode_float(value));
            }
        }
        character_palette_digest = palette_digest;

        const NnFogState& fog = frame.fog;
        const std::array<float, 4u>& fog_density = frame.fog_density;
        const std::array<float, 4u>& fog_start = frame.fog_start;
        const std::array<float, 4u>& fog_end = frame.fog_end;
        const std::array<float, 4u>& fog_scale = frame.fog_scale;
        const NnParallelLightingConstants& lights = frame.lights;
        const std::array<float, 4u>& light_count = frame.light_count;
        const Matrix projection_columns = nn_shader_matrix_columns(projection);
        for (const Mesh& mesh : meshes) {
                require(
                    mesh.lit && mesh.pipeline != nullptr && mesh.declaration != nullptr && mesh.vertices != nullptr &&
                        mesh.indices != nullptr && mesh.texture != nullptr && !mesh.matrix_subset.empty() &&
                        (mesh.matrix_capacity == 4u || mesh.matrix_capacity == 16u),
                    "Character mesh state is unsupported.");
                const NnWeightedMatrixSubset weighted = nn_weighted_matrix_subset(
                    matrices.palette,
                    mesh.matrix_subset,
                    mesh.matrix_capacity == 16u ? kCharacterIndexedVertexFormat : kCharacterNonindexedVertexFormat,
                    kPrecision);
                require(
                    weighted.model_view.size() == mesh.matrix_subset.size() &&
                        weighted.normal.size() == mesh.matrix_subset.size(),
                    "Character weighted matrix subset count is unsupported.");
                std::vector<float> bone_columns;
                std::vector<float> normal_columns;
                bone_columns.reserve(weighted.model_view.size() * 16u);
                normal_columns.reserve(weighted.normal.size() * 12u);
                for (const NnShaderMatrix& matrix : weighted.model_view) {
                    const Matrix columns = nn_shader_matrix_columns(matrix);
                    bone_columns.insert(bone_columns.end(), columns.begin(), columns.end());
                }
                for (const NnNormalMatrix& normal : weighted.normal) {
                    for (std::size_t column = 0u; column < 3u; ++column) {
                        for (std::size_t row = 0u; row < 3u; ++row) {
                            normal_columns.push_back(normal[row * 3u + column]);
                        }
                        normal_columns.push_back(0.0f);
                    }
                }
                require(
                    bone_columns.size() == weighted.model_view.size() * 16u &&
                        normal_columns.size() == weighted.normal.size() * 12u &&
                        bone_columns.size() % 4u == 0u && normal_columns.size() % 4u == 0u,
                    "Character weighted shader constants are unsupported.");
                const std::uint32_t matrix_count = static_cast<std::uint32_t>(weighted.model_view.size());
                const std::uint32_t bone_vector_count = static_cast<std::uint32_t>(bone_columns.size() / 4u);
                const std::uint32_t normal_vector_count = static_cast<std::uint32_t>(normal_columns.size() / 4u);
                DrawSubmission draw{};
                draw.declaration = mesh.declaration.get();
                draw.vertex_streams = {{0u, mesh.vertices.get(), 0u}};
                draw.index_buffer = mesh.indices.get();
                draw.vertex_shader = mesh.pipeline->vertex_shader.get();
                draw.pixel_shader = mesh.pipeline->pixel_shader.get();
                draw.textures = {{0u, mesh.texture, mesh.sampler}};
                draw.render_state = mesh.state;
                draw.topology = PrimitiveTopology::TriangleList;
                draw.vertex_count = mesh.vertex_count;
                draw.primitive_count = mesh.triangle_count;
                const std::array<float, 4u> alpha{{mesh.material.base_alpha, 0, 0, 0}};
                const std::array<float, 4u> shininess{{mesh.material.shininess, 0, 0, 0}};
                const ShaderConstants& vertex = mesh.pipeline->vertex_constants;
                const ShaderConstants& pixel = mesh.pipeline->pixel_constants;
                vertex.bind_partial_matrix_array(
                    draw.constants.vertex_float4,
                    "u_ModelViewBoneMatrix",
                    bone_columns.data(),
                    bone_vector_count,
                    matrix_count,
                    mesh.matrix_capacity,
                    4u,
                    4u,
                    true);
                vertex.bind_partial_matrix_array(
                    draw.constants.vertex_float4,
                    "u_ModelViewBoneNormalMatrix",
                    normal_columns.data(),
                    normal_vector_count,
                    matrix_count,
                    mesh.matrix_capacity,
                    3u,
                    3u,
                    true);
                vertex.bind_matrix(
                    draw.constants.vertex_float4,
                    "u_ProjectionMatrix",
                    projection_columns.data(),
                    4u,
                    4u,
                    4u,
                    true);
                vertex.bind(
                    draw.constants.vertex_float4,
                    "u_TextureMatrix",
                    mesh.texture_columns.data(),
                    4u,
                    true);
                vertex.bind(
                    draw.constants.vertex_float4,
                    "u_FrontMaterial.diffuse",
                    mesh.material.diffuse.data(),
                    1u);
                for (const auto& binding : std::array<std::pair<const char*, const float*>, 7u>{{
                         {"u_Fog.color", fog.color.data()},
                         {"u_Fog.density", fog_density.data()},
                         {"u_Fog.start", fog_start.data()},
                         {"u_Fog.end", fog_end.data()},
                         {"u_Fog.scale", fog_scale.data()},
                         {"u_TexBaseAlpha", alpha.data()},
                         {"u_FrontMaterial.diffuse", mesh.material.diffuse.data()},
                     }}) {
                    if (std::string(binding.first) != "u_FrontMaterial.diffuse") {
                        vertex.bind(draw.constants.vertex_float4, binding.first, binding.second, 1u);
                    }
                    pixel.bind(draw.constants.pixel_float4, binding.first, binding.second, 1u);
                }
                for (const auto& binding : std::array<std::pair<const char*, const float*>, 6u>{{
                         {"u_FrontMaterial.ambient", mesh.material.ambient.data()},
                         {"u_FrontMaterial.specular", mesh.material.specular.data()},
                         {"u_FrontMaterial.emission", mesh.material.emission.data()},
                         {"u_FrontMaterial.shininess", shininess.data()},
                         {"u_FrontLightModelProduct.sceneColor", mesh.material.scene_color.data()},
                         {"u_NumParallelLight", light_count.data()},
                     }}) {
                    vertex.bind(draw.constants.vertex_float4, binding.first, binding.second, 1u);
                    pixel.bind(draw.constants.pixel_float4, binding.first, binding.second, 1u);
                }
                for (std::size_t index = 0u; index < lights.count; ++index) {
                    const std::string prefix = "u_LightSource[" + std::to_string(index) + "].";
                    for (const auto& binding : std::array<std::pair<const char*, const float*>, 3u>{{
                             {"diffuse", lights.lights[index].diffuse.data()},
                             {"specular", lights.lights[index].specular.data()},
                             {"position", lights.lights[index].position.data()},
                         }}) {
                        const std::string name = prefix + binding.first;
                        vertex.bind(draw.constants.vertex_float4, name.c_str(), binding.second, 1u);
                        pixel.bind(draw.constants.pixel_float4, name.c_str(), binding.second, 1u);
                    }
                }
                vertex.bind(
                    draw.constants.vertex_float4,
                    "u_UserUniform",
                    kCharacterUserUniform.data(),
                    7u,
                    true);
                pixel.bind(
                    draw.constants.pixel_float4,
                    "u_UserUniform",
                    kCharacterUserUniform.data(),
                    7u,
                    true);
                require(
                    frame_triangles <= std::numeric_limits<std::size_t>::max() - mesh.triangle_count &&
                        character_frame_triangles <= std::numeric_limits<std::size_t>::max() - mesh.triangle_count,
                    "Character frame triangle count overflows.");
                renderer.draw(draw);
                ++frame_lit;
                ++character_frame_lit;
                ++frame_draws;
                ++character_frame_draws;
                frame_triangles += mesh.triangle_count;
                character_frame_triangles += mesh.triangle_count;
                ++frame_non_shadow;
                ++character_frame_non_shadow;
        }
    }

    void draw_character(
        const NnModelData& model,
        const std::vector<Mesh>& meshes,
        const Matrix& base,
        const NnMotionData& motion,
        float motion_frame,
        const Matrix& projection,
        const FrameLighting& frame) {
        require(
            std::isfinite(motion_frame),
            "Character motion frame is invalid.");
        character_frame = motion_frame;
        const CharacterPoseEvaluation evaluation = evaluate_character_poses(model, motion, motion_frame);
        record_character_pose_evaluation(evaluation);
        character_secondary_final_channel_cursor = 0u;
        draw_character_poses(model, meshes, base, evaluation.poses, projection, frame);
    }

    void draw_blended_character(
        const NnModelData& model,
        const std::vector<Mesh>& meshes,
        const Matrix& base,
        const PlayerMotion& primary_motion,
        float primary_frame,
        const PlayerMotion& secondary_motion,
        float secondary_frame,
        float blend_weight,
        const Matrix& projection,
        const FrameLighting& frame) {
        require(
            std::isfinite(primary_frame) && std::isfinite(secondary_frame) &&
                std::isfinite(blend_weight) && blend_weight >= 0.0f && blend_weight <= 1.0f,
            "Player-scene blend state is invalid.");
        character_frame = primary_frame;
        require(
            primary_motion.model == secondary_motion.model,
            "Player-scene cross-model pose blending is unsupported.");
        const CharacterPoseEvaluation primary = evaluate_character_poses(model, primary_motion.data, primary_frame);
        const CharacterPoseEvaluation secondary = evaluate_character_poses(model, secondary_motion.data, secondary_frame);
        require(
            primary.poses.size() == model.nodes.size() &&
                secondary.poses.size() == model.nodes.size(),
            "Player-scene blend pose count is unsupported.");

        CharacterPoseEvaluation linked{};
        linked.poses.reserve(primary.poses.size());
        std::uint64_t digest = kDigestOffset;
        for (std::size_t index = 0u; index < primary.poses.size(); ++index) {
            NnMotionLocalPose pose = nn_link_motion_pose(
                primary.poses[index],
                secondary.poses[index],
                blend_weight,
                kPrecision);
            for (float value : pose.translation) {
                mix_digest(digest, encode_float(value));
            }
            for (float value : pose.rotation) {
                mix_digest(digest, encode_float(value));
            }
            for (float value : pose.scale) {
                mix_digest(digest, encode_float(value));
            }
            mix_digest(digest, pose.translation_flags);
            mix_digest(digest, pose.rotation_flags);
            mix_digest(digest, pose.scale_flags);
            mix_digest(digest, static_cast<std::uint32_t>(pose.next_channel));
            linked.poses.push_back(std::move(pose));
        }
        linked.final_channel_cursor = primary.final_channel_cursor;
        linked.digest = digest;
        record_character_pose_evaluation(linked);
        character_secondary_final_channel_cursor = secondary.final_channel_cursor;
        draw_character_poses(model, meshes, base, linked.poses, projection, frame);
    }

    void render_character(float horizontal, float vertical, float distance_factor, float delta_seconds) {
        require(
            std::isfinite(horizontal) && std::isfinite(vertical) && std::isfinite(distance_factor) &&
                distance_factor > 0.0f,
            "Character inspection camera values are invalid.");
        advance_character_frame(delta_seconds);
        camera_x = horizontal;
        camera_y = 6.0f + vertical;
        camera_distance = 28.0f * distance_factor;
        const float aspect = static_cast<float>(device.width()) / static_cast<float>(device.height());
        const auto defaults = make_normal_main_camera_projection_defaults(aspect);
        const CameraViewInput input{
            defaults.fov_angle,
            aspect,
            defaults.near_plane,
            defaults.far_plane,
            {{camera_x, camera_y, camera_distance}},
            {{camera_x, camera_y, 0.0f}},
            std::nullopt,
            0,
        };
        const Matrix view = make_camera_view_matrix(make_camera_view_parameters(input), kPrecision, backend);
        const Matrix projection = make_camera_projection_matrix(
            make_camera_projection(defaults.fov_angle, aspect, defaults.near_plane, defaults.far_plane, kPrecision),
            kPrecision,
            backend);
        const FrameLighting frame = make_frame_lighting(view, defaults.far_plane, kCharacterLightMask);
        begin_render_frame();
        try {
            draw_character(
                character_model,
                character_meshes,
                view,
                character_motion,
                character_frame,
                projection,
                frame);
        } catch (...) {
            device.end_frame();
            throw;
        }
        device.end_frame();
    }

    void draw_static_mesh(
        const Mesh& mesh,
        const Matrix& model_view,
        const Matrix& projection,
        const FrameLighting& frame,
        std::size_t& draw_count,
        std::size_t& triangle_count,
        std::size_t& non_shadow_count,
        std::size_t& lit_count) {
        const NnFogState& fog = frame.fog;
        const std::array<float, 4u>& fog_density = frame.fog_density;
        const std::array<float, 4u>& fog_start = frame.fog_start;
        const std::array<float, 4u>& fog_end = frame.fog_end;
        const std::array<float, 4u>& fog_scale = frame.fog_scale;
        const NnParallelLightingConstants& lights = frame.lights;
        const std::array<float, 4u>& light_count = frame.light_count;

        const auto constants = nn_unlit_model_matrices(model_view, kIdentity, projection, kPrecision, backend);
        const auto mv_columns = nn_shader_matrix_columns(constants.model_view);
        const auto mvp_columns = nn_shader_matrix_columns(constants.model_view_projection);
        const std::array<float, 4u> alpha{{mesh.material.base_alpha, 0, 0, 0}};
        const std::array<float, 4u> shininess{{mesh.material.shininess, 0, 0, 0}};
        std::array<float, 12u> normal_columns{};
        if (mesh.lit) {
            const auto normal = nn_normal_matrix(constants.model_view, kPrecision);
            for (std::size_t column = 0u; column < 3u; ++column)
                for (std::size_t row = 0u; row < 3u; ++row)
                    normal_columns[column * 4u + row] = normal[row * 3u + column];
        }
        DrawSubmission draw{};
        draw.declaration = mesh.declaration.get();
        draw.vertex_streams = {{0u, mesh.vertices.get(), 0u}};
        draw.index_buffer = mesh.indices.get();
        draw.vertex_shader = mesh.pipeline->vertex_shader.get();
        draw.pixel_shader = mesh.pipeline->pixel_shader.get();
        draw.textures = {{0u, mesh.texture, mesh.sampler}};
        draw.render_state = mesh.state;
        draw.topology = PrimitiveTopology::TriangleList;
        draw.vertex_count = mesh.vertex_count;
        draw.primitive_count = mesh.triangle_count;
        const auto& vertex = mesh.pipeline->vertex_constants;
        const auto& pixel = mesh.pipeline->pixel_constants;
        vertex.bind(draw.constants.vertex_float4, "u_ModelViewProjectionMatrix", mvp_columns.data(), 4u, true);
        vertex.bind(draw.constants.vertex_float4, "u_ModelViewMatrix", mv_columns.data(), 4u);
        vertex.bind(draw.constants.vertex_float4, "u_TextureMatrix", mesh.texture_columns.data(), 4u, true);
        vertex.bind(draw.constants.vertex_float4, "u_FrontMaterial.diffuse", mesh.material.diffuse.data(), 1u);
        for (const auto& binding : std::array<std::pair<const char*, const float*>, 7u>{{
            {"u_Fog.color", fog.color.data()}, {"u_Fog.density", fog_density.data()},
            {"u_Fog.start", fog_start.data()}, {"u_Fog.end", fog_end.data()}, {"u_Fog.scale", fog_scale.data()},
            {"u_TexBaseAlpha", alpha.data()}, {"u_FrontMaterial.diffuse", mesh.material.diffuse.data()}}}) {
            if (std::string(binding.first) != "u_FrontMaterial.diffuse")
                vertex.bind(draw.constants.vertex_float4, binding.first, binding.second, 1u);
            pixel.bind(draw.constants.pixel_float4, binding.first, binding.second, 1u);
        }
        if (mesh.lit) {
            vertex.bind(draw.constants.vertex_float4, "u_NormalMatrix", normal_columns.data(), 3u, true);
            for (const auto& binding : std::array<std::pair<const char*, const float*>, 6u>{{
                {"u_FrontMaterial.ambient", mesh.material.ambient.data()},
                {"u_FrontMaterial.specular", mesh.material.specular.data()},
                {"u_FrontMaterial.emission", mesh.material.emission.data()},
                {"u_FrontMaterial.shininess", shininess.data()},
                {"u_FrontLightModelProduct.sceneColor", mesh.material.scene_color.data()},
                {"u_NumParallelLight", light_count.data()}}}) {
                vertex.bind(draw.constants.vertex_float4, binding.first, binding.second, 1u);
                pixel.bind(draw.constants.pixel_float4, binding.first, binding.second, 1u);
            }
            for (std::size_t index = 0u; index < lights.count; ++index) {
                const std::string prefix = "u_LightSource[" + std::to_string(index) + "].";
                for (const auto& binding : std::array<std::pair<const char*, const float*>, 3u>{{
                    {"diffuse", lights.lights[index].diffuse.data()},
                    {"specular", lights.lights[index].specular.data()},
                    {"position", lights.lights[index].position.data()}}}) {
                    const std::string name = prefix + binding.first;
                    vertex.bind(draw.constants.vertex_float4, name.c_str(), binding.second, 1u);
                    pixel.bind(draw.constants.pixel_float4, name.c_str(), binding.second, 1u);
                }
            }
        }
        renderer.draw(draw);
        if (mesh.lit) ++lit_count;
        ++draw_count;
        triangle_count += mesh.triangle_count;
        if (!mesh.shadow) ++non_shadow_count;
    }

    void draw_stage(const Matrix& view, const Matrix& projection, const FrameLighting& frame) {
        for (const auto& instance : instances) {
            const auto& model = models.at(instance.placement.model_id);
            if (model.meshes.empty()) continue;
            const auto base = backend.multiply(instance.transform, view, kPrecision);
            const auto matrices = nn_static_node_matrices(model.data, base, kPrecision, backend);
            for (const auto& mesh : model.meshes) {
                draw_static_mesh(
                    mesh,
                    matrices.palette.at(mesh.palette_index),
                    projection,
                    frame,
                    frame_draws,
                    frame_triangles,
                    frame_non_shadow,
                    frame_lit);
            }
        }
    }

    void draw_rings(const Matrix& view, const Matrix& projection, const FrameLighting& frame) {
        require(
            ring_resources_prepared && ring_model.meshes.size() == 1u && ring_model.packet_count == 1u,
            "Ring renderer resources are unavailable.");
        FrameLighting ring_frame = frame;
        ring_frame.light_count = {{2.0f, 0.0f, 0.0f, 0.0f}};
        ring_frame_tinted = 0u;
        ring_tint_digest = kDigestOffset;
        for (const Matrix& world : ring_instances) {
            const auto tint = ambient_field_ring_color(
                ring_ambient_points, ring_ambient_palettes, {{world[12u], -world[13u]}}, kPrecision);
            if (tint[0u] != 1.0f || tint[1u] != 1.0f || tint[2u] != 1.0f) ++ring_frame_tinted;
            for (float channel : tint) mix_digest(ring_tint_digest, encode_float(channel));
            ring_frame.lights = nn_parallel_lighting_constants(lighting.lights, kRingLightMask, view, kPrecision, tint);
            require(ring_frame.lights.count == 2u, "Ring shader requires two parallel lights.");
            const Matrix base = backend.multiply(world, view, kPrecision);
            const NnStaticNodeMatrices matrices = nn_static_node_matrices(ring_model.data, base, kPrecision, backend);
            for (const Mesh& mesh : ring_model.meshes) {
                draw_static_mesh(
                    mesh,
                    matrices.palette.at(mesh.palette_index),
                    projection,
                    ring_frame,
                    ring_frame_draws,
                    ring_frame_triangles,
                    ring_frame_non_shadow,
                    ring_frame_lit);
            }
        }
    }

    void draw_player_effects(const Matrix& view, const Matrix& projection,
        const FrameLighting& frame, const std::array<float, 3u>& camera_position) {
        require(ring_effect_resources_prepared && ring_effect_declaration != nullptr &&
            ring_effect_pipeline != nullptr && ring_effect_textures[0u] != nullptr &&
            ring_effect_textures[1u] != nullptr && ring_effect_textures[2u] != nullptr,
            "Player effect renderer resources are unavailable.");
        struct Batch {
            NativePlayerEffectKind kind;
            std::uint64_t effect_id;
            std::size_t node_index;
            bool line;
            std::uint32_t texture_index;
            std::uint32_t first_vertex;
            std::uint32_t vertex_count;
            float sort_z;
        };
        std::vector<AmeSpriteGpuVertex> vertices;
        std::vector<Batch> batches;
        auto append = [&](const NativePlayerEffectFrame& effect, std::size_t node, bool line,
                          std::uint32_t texture, const std::array<AmeSpriteVertex, 6u>& quad, float sort_z) {
            require(vertices.size() <= std::numeric_limits<std::uint32_t>::max() - 6u,
                "Player effect vertex count overflows.");
            if (batches.empty() || batches.back().effect_id != effect.id ||
                batches.back().kind != effect.kind || batches.back().node_index != node ||
                batches.back().line != line || batches.back().texture_index != texture) {
                batches.push_back({effect.kind, effect.id, node, line, texture,
                    static_cast<std::uint32_t>(vertices.size()), 0u, sort_z});
            }
            for (const auto& vertex : quad) vertices.push_back(make_ame_sprite_gpu_vertex(vertex));
            batches.back().vertex_count += 6u;
            batches.back().sort_z = sort_z;
        };
        for (const auto& effect : player_effect_frames) {
            for (const auto& sprite : effect.sprites) {
                const std::uint32_t texture = sprite.texture_id == 12 ? 0u : 1u;
                const auto quad = ame_sprite_quad(sprite, view, kPrecision);
                const float z_offset = view[10u] * sprite.z_bias;
                const float center_z = sprite.position.z + z_offset;
                const float sort_z = std::fabs(center_z - camera_position[2u]);
                append(effect, sprite.node_index, false, texture, quad.vertices, sort_z);
            }
            for (const auto& line : effect.lines) {
                const auto quad = ame_line_quad(line, view, camera_position, kPrecision);
                append(effect, line.node_index, true, 2u, quad.vertices, quad.sort_z);
            }
        }
        if (vertices.empty()) return;
        std::vector<float> depths;
        depths.reserve(batches.size());
        for (const auto& batch : batches) depths.push_back(batch.sort_z);
        const auto order = ame_primitive_sort_order(depths, kPrecision);
        ring_effect_vertices = renderer.create_vertex_buffer({
            {reinterpret_cast<const std::uint8_t*>(vertices.data()), vertices.size() * sizeof(AmeSpriteGpuVertex)},
            static_cast<std::uint32_t>(sizeof(AmeSpriteGpuVertex)), false});

        const auto matrices = nn_unlit_model_matrices(kIdentity, view, projection, kPrecision, backend);
        const auto model_view_columns = nn_shader_matrix_columns(matrices.model_view);
        const auto model_view_projection_columns = nn_shader_matrix_columns(matrices.model_view_projection);
        const std::array<float, 4u> diffuse{{1.0f, 1.0f, 1.0f, 1.0f}};
        const std::array<float, 4u> alpha{{1.0f, 0.0f, 0.0f, 0.0f}};
        const std::array<std::pair<const char*, const float*>, 6u> fog_bindings{{
            {"u_Fog.color", frame.fog.color.data()},
            {"u_Fog.density", frame.fog_density.data()},
            {"u_Fog.start", frame.fog_start.data()},
            {"u_Fog.end", frame.fog_end.data()},
            {"u_Fog.scale", frame.fog_scale.data()},
            {"u_TexBaseAlpha", alpha.data()},
        }};

        for (const auto batch_index : order) {
            const Batch& batch = batches[batch_index];
            DrawSubmission draw{};
            draw.declaration = ring_effect_declaration.get();
            draw.vertex_streams = {{0u, ring_effect_vertices.get(), 0u}};
            draw.index_buffer = nullptr;
            draw.vertex_shader = ring_effect_pipeline->vertex_shader.get();
            draw.pixel_shader = ring_effect_pipeline->pixel_shader.get();
            draw.textures = {{0u, ring_effect_textures[batch.texture_index], ring_effect_sampler}};
            draw.render_state = ring_effect_state;
            draw.topology = PrimitiveTopology::TriangleList;
            draw.base_vertex_index = 0;
            draw.minimum_vertex_index = 0u;
            draw.vertex_count = batch.vertex_count;
            draw.start_index = 0u;
            draw.primitive_count = batch.vertex_count / 3u;
            draw.mode = DrawSubmissionMode::NonIndexed;
            draw.first_vertex = batch.first_vertex;
            const auto& vertex = ring_effect_pipeline->vertex_constants;
            const auto& pixel = ring_effect_pipeline->pixel_constants;
            vertex.bind(
                draw.constants.vertex_float4,
                "u_ModelViewProjectionMatrix",
                model_view_projection_columns.data(),
                4u,
                true);
            vertex.bind(
                draw.constants.vertex_float4,
                "u_ModelViewMatrix",
                model_view_columns.data(),
                4u,
                true);
            vertex.bind(draw.constants.vertex_float4, "u_TextureMatrix", kIdentity.data(), 4u, true);
            vertex.bind(draw.constants.vertex_float4, "u_FrontMaterial.diffuse", diffuse.data(), 1u, true);
            for (const auto& binding : fog_bindings) {
                vertex.bind(draw.constants.vertex_float4, binding.first, binding.second, 1u);
                pixel.bind(draw.constants.pixel_float4, binding.first, binding.second, 1u);
            }
            pixel.bind(draw.constants.pixel_float4, "u_FrontMaterial.diffuse", diffuse.data(), 1u);
            renderer.draw(draw);

            if (batch.kind == NativePlayerEffectKind::JumpDash) {
                ++dash_effect_frame_draws;
                const std::uint64_t count = batch.vertex_count / 6u;
                if (batch.line) {
                    dash_effect_frame_lines += count;
                    dash_effect_submitted_lines += count;
                } else {
                    dash_effect_frame_sprites += count;
                    dash_effect_submitted_sprites += count;
                }
                dash_effect_submitted_triangles += batch.vertex_count / 3u;
                continue;
            }
            const std::uint64_t submitted_sprites = batch.vertex_count / 6u;
            const std::uint64_t submitted_triangles = batch.vertex_count / 3u;
            require(ring_effect_frame_draws != std::numeric_limits<std::uint64_t>::max(),
                    "Ring effect draw count overflows.");
            require(
                ring_effect_frame_sprites <= std::numeric_limits<std::uint64_t>::max() - submitted_sprites &&
                    ring_effect_frame_triangles <= std::numeric_limits<std::uint64_t>::max() - submitted_triangles &&
                    ring_effect_submitted_sprites <= std::numeric_limits<std::uint64_t>::max() - submitted_sprites &&
                    ring_effect_submitted_triangles <= std::numeric_limits<std::uint64_t>::max() - submitted_triangles,
                "Ring effect submission count overflows.");
            ++ring_effect_frame_draws;
            ring_effect_frame_sprites += submitted_sprites;
            ring_effect_frame_triangles += submitted_triangles;
            ring_effect_submitted_sprites += submitted_sprites;
            ring_effect_submitted_triangles += submitted_triangles;
        }
    }

    void render(float horizontal, float vertical, float distance_factor, float delta_seconds) {
        if (character_inspection) {
            render_character(horizontal, vertical, distance_factor, delta_seconds);
            return;
        }
        require(!player_scene, "Player-scene rendering requires caller-supplied world, camera, and motion frame.");
        require(
            std::isfinite(horizontal) && std::isfinite(vertical) && std::isfinite(distance_factor) &&
                distance_factor > 0.0f,
            "Inspection camera values are invalid.");
        camera_x = 5280.0f + horizontal;
        camera_y = -2912.0f + vertical;
        camera_distance = 3000.0f * distance_factor;
        const float aspect = static_cast<float>(device.width()) / static_cast<float>(device.height());
        const auto defaults = make_normal_main_camera_projection_defaults(aspect);
        const CameraViewInput input{
            defaults.fov_angle,
            aspect,
            defaults.near_plane,
            defaults.far_plane,
            {{camera_x, camera_y, -400.0f + camera_distance}},
            {{camera_x, camera_y, -400.0f}},
            std::nullopt,
            0,
        };
        const Matrix view = make_camera_view_matrix(make_camera_view_parameters(input), kPrecision, backend);
        const Matrix projection = make_camera_projection_matrix(
            make_camera_projection(defaults.fov_angle, aspect, defaults.near_plane, defaults.far_plane, kPrecision),
            kPrecision,
            backend);
        const FrameLighting frame = make_frame_lighting(view, defaults.far_plane);
        begin_render_frame();
        try {
            draw_stage(view, projection, frame);
        } catch (...) {
            device.end_frame();
            throw;
        }
        device.end_frame();
    }

    void render_player_scene(const Matrix& world, const CameraViewInput& camera, float motion_frame) {
        require(player_scene && !character_inspection, "Player-scene renderer was not selected at construction.");
        require(selected_player_motion != nullptr, "Player-scene motion is unavailable.");
        require(
            std::isfinite(motion_frame) && motion_frame >= player_motion_start &&
                motion_frame < player_motion_end,
            "Player-scene motion frame is outside the selected motion range.");
        clear_player_blend_report();
        const CameraViewParameters parameters = make_camera_view_parameters(camera);
        const Matrix view = make_camera_view_matrix(parameters, kPrecision, backend);
        const Matrix projection = make_camera_projection_matrix(
            make_camera_projection(
                parameters.fov_angle,
                parameters.aspect,
                parameters.near_plane,
                parameters.far_plane,
                kPrecision),
            kPrecision,
            backend);
        const Matrix character_base = backend.multiply(world, view, kPrecision);
        const FrameLighting frame = make_frame_lighting(view, parameters.far_plane);
        const FrameLighting character_lighting = make_frame_lighting(view, parameters.far_plane, kCharacterLightMask);
        begin_render_frame();
        try {
            draw_stage(view, projection, frame);
            draw_rings(view, projection, frame);
            draw_player_effects(view, projection, frame, parameters.eye);
            draw_character(
                player_character_model(selected_player_motion->model),
                player_character_meshes(selected_player_motion->model),
                character_base,
                selected_player_motion->data,
                motion_frame,
                projection,
                character_lighting);
        } catch (...) {
            device.end_frame();
            throw;
        }
        device.end_frame();
        player_scene_camera = parameters;
        player_scene_world = world;
        player_scene_rendered = true;
    }

    void render_player_scene(
        const Matrix& world,
        const CameraViewInput& camera,
        const SonicGroundAnimationState& animation) {
        require(player_scene && !character_inspection, "Player-scene renderer was not selected at construction.");
        require(animation.initialized, "Player-scene animation state is uninitialized.");

        float primary_frame = 0.0f;
        const auto primary = load_player_motion_layer(animation.primary, primary_frame);
        select_player_motion(primary);

        const PlayerMotion* secondary_motion = nullptr;
        float secondary_frame = 0.0f;
        if (animation.blend_active) {
            require(
                std::isfinite(animation.blend_weight) &&
                    animation.blend_weight >= 0.0f && animation.blend_weight <= 1.0f,
                "Player-scene blend weight is invalid.");
            const auto secondary = load_player_motion_layer(animation.secondary, secondary_frame);
            secondary_motion = &secondary->second;
            require(
                primary->second.model == secondary_motion->model,
                "Player-scene cross-model pose blending is unsupported.");
            player_secondary_motion_name = secondary->first;
            player_secondary_motion_start = secondary_motion->timing.start;
            player_secondary_motion_end = secondary_motion->timing.end;
            player_secondary_motion_rate = secondary_motion->timing.rate;
            player_secondary_motion_frame = secondary_frame;
            player_secondary_motion_channel_count = secondary_motion->data.channel_count;
            player_blend_weight = animation.blend_weight;
            player_blend_active = true;
        } else {
            clear_player_blend_report();
        }

        const CameraViewParameters parameters = make_camera_view_parameters(camera);
        const Matrix view = make_camera_view_matrix(parameters, kPrecision, backend);
        const Matrix projection = make_camera_projection_matrix(
            make_camera_projection(
                parameters.fov_angle,
                parameters.aspect,
                parameters.near_plane,
                parameters.far_plane,
                kPrecision),
            kPrecision,
            backend);
        const Matrix character_base = backend.multiply(world, view, kPrecision);
        const FrameLighting frame = make_frame_lighting(view, parameters.far_plane);
        const FrameLighting character_lighting = make_frame_lighting(view, parameters.far_plane, kCharacterLightMask);
        begin_render_frame();
        try {
            draw_stage(view, projection, frame);
            draw_rings(view, projection, frame);
            draw_player_effects(view, projection, frame, parameters.eye);
            if (secondary_motion == nullptr) {
                draw_character(
                    player_character_model(primary->second.model),
                    player_character_meshes(primary->second.model),
                    character_base,
                    primary->second.data,
                    primary_frame,
                    projection,
                    character_lighting);
            } else {
                draw_blended_character(
                    player_character_model(primary->second.model),
                    player_character_meshes(primary->second.model),
                    character_base,
                    primary->second,
                    primary_frame,
                    *secondary_motion,
                    secondary_frame,
                    animation.blend_weight,
                    projection,
                    character_lighting);
            }
        } catch (...) {
            device.end_frame();
            throw;
        }
        device.end_frame();
        player_scene_camera = parameters;
        player_scene_world = world;
        player_scene_rendered = true;
    }

    std::string report_json() const {
        if (player_scene) {
            require(selected_player_motion != nullptr, "Player-scene motion is unavailable.");
            const NnModelData& selected_model = player_character_model(selected_player_motion->model);
            const std::vector<Mesh>& selected_meshes = player_character_meshes(selected_player_motion->model);
            std::size_t selected_triangle_count = 0u;
            for (const Mesh& mesh : selected_meshes) {
                require(
                    selected_triangle_count <= std::numeric_limits<std::size_t>::max() - mesh.triangle_count,
                    "Player-scene selected triangle count overflows.");
                selected_triangle_count += mesh.triangle_count;
            }
            require(
                ring_resources_prepared && ring_model.meshes.size() == 1u && ring_effect_resources_prepared,
                "Player-scene ring resources are unavailable.");
            const Mesh& ring_mesh = ring_model.meshes[0u];
            std::ostringstream output;
            output << std::setprecision(9);
            output << "{\"mode\":\"combined original first-act stage and Sonic development scene\""
                   << ",\"rendered\":" << (player_scene_rendered ? "true" : "false")
                   << ",\"region_cells\":[" << loaded_region.min_x << ',' << loaded_region.max_x << ','
                   << loaded_region.min_y << ',' << loaded_region.max_y << ']'
                   << ",\"clear_rgba\":" << 0x31577fffu
                   << ",\"model_count\":" << models.size()
                   << ",\"placed_instances\":" << instances.size()
                   << ",\"shader_pairs\":" << pipelines.size() << ",\"textures\":" << textures.size()
                   << ",\"frame_draw_submissions\":" << frame_draws
                   << ",\"frame_triangles\":" << frame_triangles
                   << ",\"frame_non_shadow_submissions\":" << frame_non_shadow
                   << ",\"frame_lit_submissions\":" << frame_lit
                   << ",\"camera\":{\"fov_angle\":" << player_scene_camera.fov_angle
                   << ",\"aspect\":" << player_scene_camera.aspect
                   << ",\"near\":" << player_scene_camera.near_plane
                   << ",\"far\":" << player_scene_camera.far_plane
                   << ",\"eye\":[" << player_scene_camera.eye[0u] << ',' << player_scene_camera.eye[1u] << ','
                   << player_scene_camera.eye[2u] << ']'
                   << ",\"target\":[" << player_scene_camera.target[0u] << ',' << player_scene_camera.target[1u]
                   << ',' << player_scene_camera.target[2u] << ']'
                   << ",\"roll_angle\":" << player_scene_camera.roll_angle << '}'
                   << ",\"player_world_translation\":[" << player_scene_world[12u] << ','
                   << player_scene_world[13u] << ',' << player_scene_world[14u] << ']'
                   << ",\"rings\":{\"model\":\"RING.ZNO\""
                   << ",\"model_node_count\":" << ring_model.data.node_count
                   << ",\"matrix_palette_count\":" << ring_model.data.matrix_palette_count
                   << ",\"mesh_packets\":" << ring_model.packet_count
                   << ",\"triangle_count\":" << ring_mesh.triangle_count
                   << ",\"instances\":" << ring_instances.size()
                   << ",\"frame_draw_submissions\":" << ring_frame_draws
                   << ",\"frame_triangles\":" << ring_frame_triangles
                   << ",\"frame_non_shadow_submissions\":" << ring_frame_non_shadow
                   << ",\"frame_lit_submissions\":" << ring_frame_lit
                   << ",\"texture\":\"CMN_METAL_MS_RINGSKY_REF.DDS\""
                   << ",\"shader_profile\":\"0000000000000RDMR8000022C4\""
                   << ",\"material_context\":{\"subobject_flags\":" << ring_model.data.sub_objects[0u].flags
                   << ",\"draw_flags_low\":" << kRingDrawFlagsLow
                   << ",\"draw_flags_high\":" << kRingDrawFlagsHigh
                   << ",\"light_mask\":" << kRingLightMask
                   << ",\"parallel_lights\":2,\"texture_stage_limit\":1,\"texture_coordinate\":-1"
                   << ",\"texture_matrix\":\"controlled_identity\"}"
                   << ",\"ambient_field\":{\"points\":" << ring_ambient_points.size()
                   << ",\"palette_preset\":0,\"tinted_instances\":" << ring_frame_tinted
                   << ",\"tint_digest\":\"" << digest_string(ring_tint_digest) << '\"'
                   << ",\"target\":\"selected_light_rgb_before_intensity\",\"event_scheduler_accepted\":false}"
                   << ",\"full_live_context_accepted\":false}"
                   << ",\"ring_effects\":{\"active_sprites\":" << ring_effect_sprites.size()
                   << ",\"frame_draw_submissions\":" << ring_effect_frame_draws
                   << ",\"frame_submitted_sprites\":" << ring_effect_frame_sprites
                   << ",\"frame_submitted_triangles\":" << ring_effect_frame_triangles
                   << ",\"cumulative_submitted_sprites\":" << ring_effect_submitted_sprites
                   << ",\"cumulative_submitted_triangles\":" << ring_effect_submitted_triangles
                   << ",\"textures\":[\"KIRA1.DDS\",\"KIRA2.DDS\"]"
                   << ",\"shader_profile\":\"0000000000000RDMRC00002081\""
                   << ",\"vertex_format\":\"PCT24\",\"pre_backend_color\":\"RRGGBBAA\""
                   << ",\"backend_color\":\"AARRGGBB_to_normalized_RGBA\""
                   << ",\"state\":{\"blend\":\"SRCALPHA_ONE_ADD\",\"depth_compare\":\"LEQUAL\""
                   << ",\"depth_write\":false,\"alpha_test\":false,\"cull\":\"none\""
                   << ",\"color_write\":\"RGBA\",\"address\":\"wrap\",\"filter\":\"linear\"}"
                   << ",\"ordering\":\"runtime_node_active_list\",\"global_sort_ties_accepted\":false}"
                   << ",\"jump_dash_effects\":{\"frame_draw_submissions\":" << dash_effect_frame_draws
                   << ",\"frame_submitted_sprites\":" << dash_effect_frame_sprites
                   << ",\"frame_submitted_lines\":" << dash_effect_frame_lines
                   << ",\"cumulative_submitted_sprites\":" << dash_effect_submitted_sprites
                   << ",\"cumulative_submitted_lines\":" << dash_effect_submitted_lines
                   << ",\"cumulative_submitted_triangles\":" << dash_effect_submitted_triangles
                   << ",\"textures\":[\"KIRA2.DDS\",\"HIT3.DDS\"]"
                   << ",\"shader_profile\":\"0000000000000RDMRC00002081\""
                   << ",\"ordering\":\"effect_creation_fifo_then_primitive_selection_sort\"}"
                   << ",\"character\":{\"motion_name\":" << json_string(player_motion_name)
                   << ",\"motion_start\":" << player_motion_start
                   << ",\"motion_end\":" << player_motion_end
                   << ",\"motion_rate\":" << player_motion_rate
                   << ",\"frame\":" << character_frame
                   << ",\"blend_active\":" << (player_blend_active ? "true" : "false")
                   << ",\"blend_weight\":" << player_blend_weight
                   << ",\"secondary_motion_name\":" << json_string(player_secondary_motion_name)
                   << ",\"secondary_motion_start\":" << player_secondary_motion_start
                   << ",\"secondary_motion_end\":" << player_secondary_motion_end
                   << ",\"secondary_motion_rate\":" << player_secondary_motion_rate
                   << ",\"secondary_frame\":" << player_secondary_motion_frame
                   << ",\"model_node_count\":" << selected_model.node_count
                   << ",\"motion_pose_node_count\":" << character_pose_nodes
                   << ",\"motion_channel_count\":" << selected_player_motion->data.channel_count
                   << ",\"motion_channel_cursor\":" << character_final_channel_cursor
                   << ",\"secondary_motion_channel_count\":" << player_secondary_motion_channel_count
                   << ",\"secondary_motion_channel_cursor\":" << character_secondary_final_channel_cursor
                   << ",\"matrix_palette_count\":" << selected_model.matrix_palette_count
                   << ",\"mesh_packets\":" << selected_meshes.size()
                   << ",\"triangle_count\":" << selected_triangle_count
                   << ",\"texture_cache_delta\":" << character_texture_cache_delta
                   << ",\"pipeline_cache_delta\":" << character_pipeline_cache_delta
                   << ",\"shader_pairs\":" << character_pipeline_count
                   << ",\"light_mask\":" << kCharacterLightMask
                   << ",\"frame_draw_submissions\":" << character_frame_draws
                   << ",\"frame_triangles\":" << character_frame_triangles
                   << ",\"frame_non_shadow_submissions\":" << character_frame_non_shadow
                   << ",\"frame_lit_submissions\":" << character_frame_lit
                   << ",\"pose_digest\":\"" << digest_string(character_pose_digest) << '\"'
                   << ",\"palette_digest\":\"" << digest_string(character_palette_digest) << '\"'
                   << ",\"user_uniform\":{\"declared_float4s\":7,\"vertex_active_float4s\":2"
                   << ",\"pixel_active_float4s\":1,\"fixture\":\"zero cold-storage vectors\"}}"
                   << ",\"lighting_preset\":\"LIGHT_SETTING_Z11.LTS\",\"light_mask\":" << kStageLightMask
                   << ",\"complete_render_accepted\":false,\"gameplay_available\":false"
                   << ",\"open\":[\"complete original draw ordering, culling, and materials\""
                   << ",\"inherited user-uniform values\",\"effects, HUD, controls, and gameplay\"]}";
            return output.str();
        }
        if (character_inspection) {
            std::ostringstream output;
            output << std::setprecision(9);
            output << "{\"mode\":\"original-asset animated character inspection\""
                   << ",\"action\":\"RUN\",\"frame\":" << character_frame
                   << ",\"fixed_frame\":" << (fixed_character_frame.has_value() ? "true" : "false")
                   << ",\"model_node_count\":" << character_model.node_count
                   << ",\"motion_pose_node_count\":" << character_pose_nodes
                   << ",\"motion_channel_count\":" << character_motion.channel_count
                   << ",\"motion_channel_cursor\":" << character_final_channel_cursor
                   << ",\"matrix_palette_count\":" << character_model.matrix_palette_count
                   << ",\"mesh_packets\":" << character_meshes.size()
                   << ",\"triangle_count\":" << character_triangle_count
                   << ",\"frame_draw_submissions\":" << frame_draws
                   << ",\"frame_triangles\":" << frame_triangles
                   << ",\"frame_lit_submissions\":" << frame_lit
                   << ",\"shader_pairs\":" << pipelines.size() << ",\"textures\":" << textures.size()
                   << ",\"camera\":[" << camera_x << ',' << camera_y << ',' << camera_distance << ']'
                   << ",\"camera_target\":[" << camera_x << ',' << camera_y << ",0]"
                   << ",\"lighting_preset\":\"LIGHT_SETTING_Z11.LTS\",\"light_mask\":" << kCharacterLightMask
                   << ",\"material_context\":{\"subobject_flags\":" << kCharacterSubobjectFlags
                   << ",\"draw_flags_low\":" << kStageLightDrawFlags
                   << ",\"draw_flags_high\":" << kCharacterDrawFlagsHigh
                   << ",\"parallel_lights\":2,\"texture_stage_limit\":1}"
                   << ",\"user_uniform\":{\"declared_float4s\":7,\"vertex_active_float4s\":2"
                   << ",\"pixel_active_float4s\":1,\"fixture\":\"zero cold-storage vectors\""
                   << ",\"live_inherited_values\":\"open\"}"
                   << ",\"pose_digest\":\"" << digest_string(character_pose_digest) << '\"'
                   << ",\"palette_digest\":\"" << digest_string(character_palette_digest) << '\"'
                   << ",\"complete_render_accepted\":false,\"gameplay_available\":false"
                   << ",\"open\":[\"live character selector and gameplay placement\""
                   << ",\"inherited user-uniform values\",\"complete scene composition and effects\"]}";
            return output.str();
        }
        std::ostringstream output;
        output << "{\"mode\":\"partial original first-act stage inspection\",\"region_cells\":["
               << loaded_region.min_x << ',' << loaded_region.max_x << ',' << loaded_region.min_y << ','
               << loaded_region.max_y << "],"
               << "\"clear_rgba\":" << 0x31577fffu << ",\"model_count\":" << models.size()
               << ",\"catalog_mesh_packets\":" << all_packets << ",\"supported_mesh_packets\":" << accepted_packets
               << ",\"supported_non_shadow_mesh_packets\":" << non_shadow_packets
               << ",\"supported_lit_mesh_packets\":" << lit_packets << ",\"placed_instances\":" << instances.size()
               << ",\"scene_unique_meshes\":" << scene_unique_meshes
               << ",\"shader_pairs\":" << pipelines.size() << ",\"textures\":" << textures.size()
               << ",\"frame_draw_submissions\":" << frame_draws << ",\"frame_triangles\":" << frame_triangles
               << ",\"frame_non_shadow_submissions\":" << frame_non_shadow
               << ",\"frame_lit_submissions\":" << frame_lit
               << ",\"lighting_preset\":\"LIGHT_SETTING_Z11.LTS\",\"light_mask\":" << kStageLightMask
               << ",\"lighting_context\":\"ordinary map-model defaults; live command context pending\""
               << ",\"camera\":[" << camera_x << ',' << camera_y << ',' << camera_distance << "]"
               << ",\"complete_render_accepted\":false,\"gameplay_available\":false,\"skipped_packet_reasons\":{";
        bool first = true;
        for (const auto& entry : skipped) {
            if (!first) output << ',';
            first = false;
            output << json_string(entry.first) << ':' << entry.second;
        }
        output << "},\"layers\":[";
        first = true;
        for (const auto& layer : layers) {
            if (!first) output << ',';
            first = false;
            output << "{\"name\":" << json_string(layer.name) << ",\"depth\":" << layer.depth
                   << ",\"instances\":" << layer.instances << ",\"requested_draws\":" << layer.requested_draws
                   << ",\"supported_draws\":" << layer.supported_draws << '}';
        }
        output << "],\"open\":[\"legacy and lit materials\",\"complete original draw ordering and culling\","
               << "\"stage lighting and fog lifecycle\",\"animation and effects\",\"HUD and gameplay\"]}";
        return output.str();
    }
};

NativeStageRenderer::NativeStageRenderer(
    D3d9Device& device,
    const std::filesystem::path& data_root,
    bool character_inspection,
    std::optional<float> character_frame,
    bool player_scene)
    : implementation_(std::make_unique<Implementation>(
          device,
          data_root,
          character_inspection,
          character_frame,
          player_scene)) {}

NativeStageRenderer::~NativeStageRenderer() = default;

void NativeStageRenderer::render(
    float horizontal_offset,
    float vertical_offset,
    float distance_factor,
    float delta_seconds) {
    implementation_->render(horizontal_offset, vertical_offset, distance_factor, delta_seconds);
}

void NativeStageRenderer::set_player_motion(const std::string& motion_name) {
    implementation_->set_player_motion(motion_name);
}

void NativeStageRenderer::set_ring_instances(const std::vector<CameraMatrix>& worlds) {
    implementation_->set_ring_instances(worlds);
}

void NativeStageRenderer::set_player_effects(const std::vector<NativePlayerEffectFrame>& effects) {
    implementation_->set_player_effects(effects);
}

void NativeStageRenderer::set_ring_effect_sprites(const std::vector<AmeRuntimeSprite>& sprites) {
    implementation_->set_ring_effect_sprites(sprites);
}

void NativeStageRenderer::render_player_scene(
    const CameraMatrix& world,
    const CameraViewInput& camera,
    float motion_frame) {
    implementation_->render_player_scene(world, camera, motion_frame);
}

void NativeStageRenderer::render_player_scene(
    const CameraMatrix& world,
    const CameraViewInput& camera,
    const SonicGroundAnimationState& animation) {
    implementation_->render_player_scene(world, camera, animation);
}

std::string NativeStageRenderer::report_json() const {
    return implementation_->report_json();
}

}
