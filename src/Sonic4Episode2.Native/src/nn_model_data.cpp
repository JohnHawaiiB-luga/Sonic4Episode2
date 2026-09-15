#include "nn_model_data.h"

#include "stage_data.h"

#include <algorithm>
#include <limits>
#include <utility>

namespace {

constexpr std::size_t kDataBase = 0x20u;
constexpr std::size_t kChunkHeaderSize = 8u;
constexpr std::size_t kNnIfPayloadSize = 24u;
constexpr std::size_t kNnObjectSize = 0x58u;
constexpr std::size_t kPointerRecordSize = 8u;
constexpr std::size_t kVertexDescriptorPrefixSize = 28u;
constexpr std::size_t kPrimitiveDescriptorSize = 20u;
constexpr std::size_t kMaterialDescriptorSize = 28u;
constexpr std::size_t kExtendedMaterialDescriptorSize = 32u;
constexpr std::size_t kMaterialColorTermCount = 4u;
constexpr std::size_t kMaterialColorRecordSize = 76u;
constexpr std::size_t kMaterialColorTermsOffset = 4u;
constexpr std::size_t kMaterialShininessOffset = 68u;
constexpr std::size_t kMaterialSpecularIntensityOffset = 72u;
constexpr std::size_t kMaterialStateWordCount = 7u;
constexpr std::uint32_t kShaderMaterialFlag = 0x10000000u;
constexpr std::uint32_t kUserProfileMaterialFlag = 0x20000000u;
constexpr std::size_t kLegacyMaterialStageSize = 32u;
constexpr std::size_t kStandardMaterialStageSize = 64u;
constexpr std::size_t kNodeSize = 0x90u;
constexpr std::size_t kSubObjectSize = 0x14u;
constexpr std::size_t kMeshSize = 0x28u;
constexpr std::size_t kTextureEntrySize = 0x14u;
constexpr std::uint32_t kNnIfVersion = 1u;
constexpr std::uint32_t kNnObjectVersion = 3u;
constexpr std::uint32_t kTriangleStripMode = 0x4810u;

struct Chunk {
    std::size_t offset;
    std::size_t payload_size;
};

class OutputBudget {
public:
    explicit OutputBudget(std::size_t available) : available_(available) {}

    void claim(std::size_t amount, const char* message) {
        if (amount > available_) {
            fail(message);
        }
        available_ -= amount;
    }

private:
    [[noreturn]] static void fail(const char* message) {
        throw StageDataError(message);
    }

    std::size_t available_;
};

[[noreturn]] void fail(const char* message) {
    throw StageDataError(message);
}

std::uint16_t read_le16(const std::uint8_t* data) {
    return static_cast<std::uint16_t>(
        static_cast<std::uint16_t>(data[0]) |
        (static_cast<std::uint16_t>(data[1]) << 8u));
}

std::int16_t read_le_i16(const std::uint8_t* data) {
    const std::uint16_t value = read_le16(data);
    if ((value & 0x8000u) == 0u) {
        return static_cast<std::int16_t>(value);
    }
    return static_cast<std::int16_t>(
        static_cast<std::int32_t>(value) - static_cast<std::int32_t>(0x10000u));
}

std::uint32_t read_le32(const std::uint8_t* data) {
    return static_cast<std::uint32_t>(
        static_cast<std::uint32_t>(data[0]) |
        (static_cast<std::uint32_t>(data[1]) << 8u) |
        (static_cast<std::uint32_t>(data[2]) << 16u) |
        (static_cast<std::uint32_t>(data[3]) << 24u));
}

std::int32_t read_le_i32(const std::uint8_t* data) {
    const std::uint32_t value = read_le32(data);
    if ((value & 0x80000000u) == 0u) {
        return static_cast<std::int32_t>(value);
    }
    return static_cast<std::int32_t>(
        static_cast<std::int64_t>(value) - static_cast<std::int64_t>(0x100000000ull));
}

std::size_t checked_size(std::uint32_t value, const char* message) {
    const std::size_t result = static_cast<std::size_t>(value);
    if (static_cast<std::uint32_t>(result) != value) {
        fail(message);
    }
    return result;
}

std::size_t read_nonnegative_i32(const std::uint8_t* data, const char* message) {
    const std::uint32_t value = read_le32(data);
    if ((value & 0x80000000u) != 0u) {
        fail(message);
    }
    return checked_size(value, message);
}

std::size_t checked_sum(std::size_t left, std::size_t right, const char* message) {
    if (right > std::numeric_limits<std::size_t>::max() - left) {
        fail(message);
    }
    return left + right;
}

std::size_t checked_product(std::size_t left, std::size_t right, const char* message) {
    if (left != 0u && right > std::numeric_limits<std::size_t>::max() / left) {
        fail(message);
    }
    return left * right;
}

bool fits_range(std::size_t offset, std::size_t length, std::size_t size) {
    return offset <= size && length <= size - offset;
}

void require_range(std::size_t offset, std::size_t length, std::size_t size, const char* message) {
    if (!fits_range(offset, length, size)) {
        fail(message);
    }
}

bool tag_equals(const std::uint8_t* data, std::size_t offset, const char (&tag)[5]) {
    return data[offset] == static_cast<std::uint8_t>(tag[0]) &&
           data[offset + 1u] == static_cast<std::uint8_t>(tag[1]) &&
           data[offset + 2u] == static_cast<std::uint8_t>(tag[2]) &&
           data[offset + 3u] == static_cast<std::uint8_t>(tag[3]);
}

void require_tag(const std::uint8_t* data, const Chunk& chunk, const char (&tag)[5], const char* message) {
    if (!tag_equals(data, chunk.offset, tag)) {
        fail(message);
    }
}

std::vector<Chunk> walk_chunks(const std::uint8_t* data, std::size_t size) {
    std::vector<Chunk> chunks;
    std::size_t offset = 0u;
    while (true) {
        require_range(offset, kChunkHeaderSize, size, "NN chunk header is truncated");
        const std::size_t payload_size = read_nonnegative_i32(
            data + offset + 4u,
            "NN chunk payload size is negative");
        const std::size_t next = checked_sum(
            checked_sum(offset, kChunkHeaderSize, "NN chunk layout is too large"),
            payload_size,
            "NN chunk layout is too large");
        require_range(offset, next - offset, size, "NN chunk payload is truncated");
        if (chunks.size() == 6u) {
            fail("NN chunk sequence has unsupported extra chunks");
        }
        chunks.push_back({offset, payload_size});
        if (tag_equals(data, offset, "NEND")) {
            if (next != size) {
                fail("NN data follows NEND");
            }
            return chunks;
        }
        offset = next;
    }
}

struct DataRegion {
    const std::uint8_t* data;
    std::size_t base;
    std::size_t end;

    std::size_t at(std::uint32_t relative, std::size_t length, const char* message) const {
        const std::size_t offset = checked_size(relative, message);
        const std::size_t absolute = checked_sum(base, offset, message);
        if (absolute < base || !fits_range(absolute, length, end) || absolute + length > end) {
            fail(message);
        }
        return absolute;
    }
};

using RelocationLocations = std::vector<std::uint32_t>;

bool has_relocation(const RelocationLocations& relocations, std::size_t field_relative) {
    if (field_relative > std::numeric_limits<std::uint32_t>::max()) {
        return false;
    }
    return std::binary_search(
        relocations.begin(),
        relocations.end(),
        static_cast<std::uint32_t>(field_relative));
}

void require_chunk_data_range(
    std::size_t absolute,
    std::size_t length,
    const Chunk& chunk,
    const char* message) {
    const std::size_t data_start = checked_sum(
        checked_sum(chunk.offset, kChunkHeaderSize, message),
        8u,
        message);
    const std::size_t data_end = checked_sum(
        checked_sum(chunk.offset, kChunkHeaderSize, message),
        chunk.payload_size,
        message);
    if (absolute < data_start || !fits_range(absolute, length, data_end) || absolute + length > data_end) {
        fail(message);
    }
}

std::size_t require_relocated_pointer(
    const DataRegion& region,
    const RelocationLocations& relocations,
    std::size_t field_relative,
    std::uint32_t relative,
    std::size_t target_length,
    const char* message) {
    if (relative == 0u || !has_relocation(relocations, field_relative)) {
        fail(message);
    }
    return region.at(relative, target_length, message);
}

std::size_t require_relocated_pointer_in_chunk(
    const DataRegion& region,
    const RelocationLocations& relocations,
    std::size_t field_relative,
    std::uint32_t relative,
    std::size_t target_length,
    const Chunk& chunk,
    const char* message) {
    const std::size_t target = require_relocated_pointer(
        region,
        relocations,
        field_relative,
        relative,
        target_length,
        message);
    require_chunk_data_range(target, target_length, chunk, message);
    return target;
}

std::size_t validate_declared_list(
    const DataRegion& region,
    const RelocationLocations& relocations,
    std::size_t field_relative,
    std::uint32_t relative,
    std::size_t count,
    std::size_t element_size,
    const Chunk& chunk,
    const char* message) {
    if (count == 0u) {
        if (relative != 0u) {
            fail(message);
        }
        return 0u;
    }
    const std::size_t length = checked_product(count, element_size, message);
    return require_relocated_pointer_in_chunk(
        region,
        relocations,
        field_relative,
        relative,
        length,
        chunk,
        message);
}

constexpr std::size_t kAbsentAttributeOffset = std::numeric_limits<std::size_t>::max();

struct VertexLayout {
    std::uint32_t format;
    std::uint32_t fvf;
    std::uint32_t stride;
    std::size_t normal_offset;
    std::size_t texcoord_offset;
    std::size_t diffuse_offset;
    std::size_t specular_offset;
    std::size_t weight_offset;
    std::size_t blend_index_offset;
};

const VertexLayout* find_vertex_layout(
    std::uint32_t format,
    std::uint32_t fvf,
    std::uint32_t stride) {
    static const std::array<VertexLayout, 9u> layouts = {{
        {0x00003u, 0x012u, 24u, 12u, kAbsentAttributeOffset, kAbsentAttributeOffset,
         kAbsentAttributeOffset, kAbsentAttributeOffset, kAbsentAttributeOffset},
        {0x00019u, 0x0C2u, 20u, kAbsentAttributeOffset, kAbsentAttributeOffset, 12u, 16u,
         kAbsentAttributeOffset, kAbsentAttributeOffset},
        {0x10019u, 0x1C2u, 28u, kAbsentAttributeOffset, 20u, 12u, 16u,
         kAbsentAttributeOffset, kAbsentAttributeOffset},
        {0x1001Bu, 0x1D2u, 40u, 12u, 32u, 24u, 28u,
         kAbsentAttributeOffset, kAbsentAttributeOffset},
        {0x1701Bu, 0x1DAu, 52u, 24u, 44u, 36u, 40u, 12u, kAbsentAttributeOffset},
        {0x2001Bu, 0x2D2u, 48u, 12u, kAbsentAttributeOffset, 24u, 28u,
         kAbsentAttributeOffset, kAbsentAttributeOffset},
        {0x2701Bu, 0x2DAu, 60u, 24u, kAbsentAttributeOffset, 36u, 40u, 12u,
         kAbsentAttributeOffset},
        {0x17403u, 0x111Au, 48u, 28u, 40u, kAbsentAttributeOffset,
         kAbsentAttributeOffset, 12u, 24u},
        {0x17003u, 0x11Au, 44u, 24u, 36u, kAbsentAttributeOffset,
         kAbsentAttributeOffset, 12u, kAbsentAttributeOffset},
    }};
    for (const VertexLayout& layout : layouts) {
        if (layout.format == format && layout.fvf == fvf && layout.stride == stride) {
            return &layout;
        }
    }
    return nullptr;
}

bool has_weighted_matrix_subset(const VertexLayout& layout) {
    return layout.weight_offset != kAbsentAttributeOffset;
}

std::string read_c_string_in_chunk(
    const DataRegion& region,
    const Chunk& chunk,
    std::size_t start,
    OutputBudget& budget,
    const char* message) {
    const std::size_t end = checked_sum(
        checked_sum(chunk.offset, kChunkHeaderSize, message),
        chunk.payload_size,
        message);
    require_chunk_data_range(start, 1u, chunk, message);
    for (std::size_t cursor = start; cursor < end; ++cursor) {
        if (region.data[cursor] == 0u) {
            budget.claim(cursor - start, "NN texture name output is too large");
            return std::string(
                reinterpret_cast<const char*>(region.data + start),
                cursor - start);
        }
    }
    fail(message);
}

std::vector<NnTextureData> parse_texture_list(
    const DataRegion& region,
    const RelocationLocations& relocations,
    const Chunk* texture_chunk,
    std::uint32_t texture_count,
    OutputBudget& budget) {
    if (texture_chunk == nullptr) {
        if (texture_count != 0u) {
            fail("NN object declares textures without NZTL");
        }
        return {};
    }
    if (texture_chunk->payload_size < 8u) {
        fail("NZTL data header is truncated");
    }
    if (read_le32(region.data + texture_chunk->offset + 12u) != 0u) {
        fail("NZTL data header version is unsupported");
    }
    const std::uint32_t root_relative = read_le32(region.data + texture_chunk->offset + 8u);
    const std::size_t root = region.at(root_relative, 8u, "NZTL root is out of range");
    require_chunk_data_range(root, 8u, *texture_chunk, "NZTL root is outside its chunk");
    const std::size_t count = read_nonnegative_i32(
        region.data + root,
        "NZTL texture count is negative");
    const std::uint32_t list_relative = read_le32(region.data + root + 4u);
    if (count != static_cast<std::size_t>(texture_count)) {
        fail("NZTL texture count does not match object header");
    }
    if (count == 0u) {
        if (list_relative != 0u) {
            fail("empty NZTL has a nonzero list pointer");
        }
        return {};
    }

    const std::size_t list_length = checked_product(
        count,
        kTextureEntrySize,
        "NZTL list is too large");
    const std::size_t list = require_relocated_pointer_in_chunk(
        region,
        relocations,
        static_cast<std::size_t>(root_relative) + 4u,
        list_relative,
        list_length,
        *texture_chunk,
        "NZTL list pointer is invalid");
    budget.claim(
        checked_product(count, sizeof(NnTextureData), "NN texture output is too large"),
        "NN texture output is too large");
    std::vector<NnTextureData> textures;
    textures.reserve(count);
    for (std::size_t index = 0u; index < count; ++index) {
        const std::size_t entry = list + index * kTextureEntrySize;
        const std::uint32_t name_relative = read_le32(region.data + entry + 4u);
        NnTextureData texture{{
            read_le32(region.data + entry),
            read_le32(region.data + entry + 4u),
            read_le32(region.data + entry + 8u),
            read_le32(region.data + entry + 12u),
            read_le32(region.data + entry + 16u),
        }, {}};
        if (name_relative != 0u) {
            const std::size_t name = require_relocated_pointer_in_chunk(
                region,
                relocations,
                static_cast<std::size_t>(list_relative) + index * kTextureEntrySize + 4u,
                name_relative,
                1u,
                *texture_chunk,
                "NZTL name pointer is invalid");
            texture.name = read_c_string_in_chunk(
                region,
                *texture_chunk,
                name,
                budget,
                "NZTL name is not NUL-terminated inside its chunk");
        }
        textures.push_back(std::move(texture));
    }
    return textures;
}

void decode_vertex_attributes(
    NnVertexData& vertex,
    const std::uint8_t* source,
    const VertexLayout& layout,
    std::size_t matrix_count,
    OutputBudget& budget) {
    const std::size_t count = static_cast<std::size_t>(vertex.count);
    budget.claim(
        checked_product(count, sizeof(NnVertexAttributes), "NN vertex attributes are too large"),
        "NN vertex attributes are too large");
    if (has_weighted_matrix_subset(layout)) {
        budget.claim(
            checked_product(
                count,
                3u * sizeof(std::uint32_t),
                "NN vertex weights are too large"),
            "NN vertex weights are too large");
    }
    vertex.attributes.reserve(count);
    for (std::size_t index = 0u; index < count; ++index) {
        const std::uint8_t* item = source + index * static_cast<std::size_t>(layout.stride);
        NnVertexAttributes attributes{{
            read_le32(item),
            read_le32(item + 4u),
            read_le32(item + 8u),
        }, std::nullopt, std::nullopt, std::nullopt, std::nullopt, {}, std::nullopt};
        if (layout.normal_offset != kAbsentAttributeOffset) {
            attributes.normal_bits = std::array<std::uint32_t, 3u>({{
                read_le32(item + layout.normal_offset),
                read_le32(item + layout.normal_offset + 4u),
                read_le32(item + layout.normal_offset + 8u),
            }});
        }
        if (layout.texcoord_offset != kAbsentAttributeOffset) {
            attributes.texcoord_bits = std::array<std::uint32_t, 2u>({{
                read_le32(item + layout.texcoord_offset),
                read_le32(item + layout.texcoord_offset + 4u),
            }});
        }
        if (layout.diffuse_offset != kAbsentAttributeOffset) {
            attributes.diffuse_bgra = std::array<std::uint8_t, 4u>({{
                item[layout.diffuse_offset],
                item[layout.diffuse_offset + 1u],
                item[layout.diffuse_offset + 2u],
                item[layout.diffuse_offset + 3u],
            }});
        }
        if (layout.specular_offset != kAbsentAttributeOffset) {
            attributes.specular_bgra = std::array<std::uint8_t, 4u>({{
                item[layout.specular_offset],
                item[layout.specular_offset + 1u],
                item[layout.specular_offset + 2u],
                item[layout.specular_offset + 3u],
            }});
        }
        if (has_weighted_matrix_subset(layout)) {
            attributes.weight_bits.reserve(3u);
            for (std::size_t weight = 0u; weight < 3u; ++weight) {
                attributes.weight_bits.push_back(
                    read_le32(item + layout.weight_offset + weight * 4u));
            }
        }
        if (layout.blend_index_offset != kAbsentAttributeOffset) {
            const std::array<std::uint8_t, 4u> blend_indices = {{
                item[layout.blend_index_offset],
                item[layout.blend_index_offset + 1u],
                item[layout.blend_index_offset + 2u],
                item[layout.blend_index_offset + 3u],
            }};
            for (std::uint8_t blend_index : blend_indices) {
                if (static_cast<std::size_t>(blend_index) >= matrix_count) {
                    fail("NN blend index is outside its weighted matrix subset");
                }
            }
            attributes.blend_indices = blend_indices;
        }
        vertex.attributes.push_back(std::move(attributes));
    }
}

std::vector<NnNodeData> parse_nodes(
    const std::uint8_t* data,
    std::size_t node_array,
    std::size_t node_count,
    std::size_t matrix_palette_count,
    OutputBudget& budget) {
    budget.claim(
        checked_product(node_count, sizeof(NnNodeData), "NN node output is too large"),
        "NN node output is too large");
    std::vector<NnNodeData> nodes;
    nodes.reserve(node_count);
    for (std::size_t index = 0u; index < node_count; ++index) {
        const std::size_t node_offset = node_array + index * kNodeSize;
        NnNodeData node{
            read_le32(data + node_offset),
            read_le_i16(data + node_offset + 4u),
            read_le_i16(data + node_offset + 6u),
            read_le_i16(data + node_offset + 8u),
            read_le_i16(data + node_offset + 10u),
            {{
                read_le32(data + node_offset + 12u),
                read_le32(data + node_offset + 16u),
                read_le32(data + node_offset + 20u),
            }},
            {{
                read_le_i32(data + node_offset + 24u),
                read_le_i32(data + node_offset + 28u),
                read_le_i32(data + node_offset + 32u),
            }},
            {{
                read_le32(data + node_offset + 36u),
                read_le32(data + node_offset + 40u),
                read_le32(data + node_offset + 44u),
            }},
            {},
            {},
        };
        for (std::size_t word = 0u; word < node.inverse_bind_bits.size(); ++word) {
            node.inverse_bind_bits[word] = read_le32(data + node_offset + 48u + word * 4u);
        }
        for (std::size_t word = 0u; word < node.opaque_bits.size(); ++word) {
            node.opaque_bits[word] = read_le32(data + node_offset + 112u + word * 4u);
        }
        if (node.matrix_index < -1 ||
            (node.matrix_index >= 0 &&
             static_cast<std::size_t>(node.matrix_index) >= matrix_palette_count)) {
            fail("NN node matrix index is out of range");
        }
        const std::array<std::int16_t, 3u> links = {{
            node.parent_index,
            node.child_index,
            node.sibling_index,
        }};
        for (std::int16_t link : links) {
            if (link < -1 || (link >= 0 && static_cast<std::size_t>(link) >= node_count) ||
                (link >= 0 && static_cast<std::size_t>(link) == index)) {
                fail("NN node hierarchy link is out of range");
            }
        }
        nodes.push_back(std::move(node));
    }
    for (std::size_t start = 0u; start < nodes.size(); ++start) {
        std::int16_t parent = nodes[start].parent_index;
        for (std::size_t depth = 0u; parent >= 0; ++depth) {
            if (depth >= nodes.size()) {
                fail("NN node hierarchy has a parent cycle");
            }
            parent = nodes[static_cast<std::size_t>(parent)].parent_index;
        }
    }
    return nodes;
}

std::vector<NnMaterialData> parse_materials(
    const DataRegion& region,
    const RelocationLocations& relocations,
    std::size_t material_array,
    std::uint32_t material_relative,
    std::size_t material_count,
    const Chunk& object_chunk,
    std::size_t texture_count,
    OutputBudget& budget) {
    budget.claim(
        checked_product(material_count, sizeof(NnMaterialData), "NN material output is too large"),
        "NN material output is too large");
    std::vector<NnMaterialData> materials;
    materials.reserve(material_count);
    for (std::size_t index = 0u; index < material_count; ++index) {
        const std::size_t pointer_offset = material_array + index * kPointerRecordSize;
        const std::uint32_t pointer_flags = read_le32(region.data + pointer_offset);
        const std::uint32_t descriptor_relative = read_le32(region.data + pointer_offset + 4u);
        const bool has_shader_stage = (pointer_flags & kShaderMaterialFlag) != 0u;
        const bool has_user_profile = has_shader_stage &&
            (pointer_flags & kUserProfileMaterialFlag) != 0u;
        const std::size_t descriptor_size = has_user_profile
            ? kExtendedMaterialDescriptorSize
            : kMaterialDescriptorSize;
        const std::size_t descriptor = require_relocated_pointer_in_chunk(
            region,
            relocations,
            static_cast<std::size_t>(material_relative) + index * kPointerRecordSize + 4u,
            descriptor_relative,
            descriptor_size,
            object_chunk,
            "NN material descriptor pointer is invalid");
        const std::size_t descriptor_data_relative = descriptor - region.base;
        NnMaterialData material{};
        material.pointer_flags = pointer_flags;
        for (std::size_t word = 0u; word < material.descriptor_bits.size(); ++word) {
            material.descriptor_bits[word] = read_le32(region.data + descriptor + word * 4u);
        }
        if (has_user_profile) {
            material.user_profile = read_le32(region.data + descriptor + kMaterialDescriptorSize);
        }

        const std::uint32_t color_relative = material.descriptor_bits[2];
        if (color_relative != 0u) {
            const std::size_t color = require_relocated_pointer_in_chunk(
                region,
                relocations,
                descriptor_data_relative + 8u,
                color_relative,
                kMaterialColorRecordSize,
                object_chunk,
                "NN material color pointer is invalid");
            material.color_flags = read_le32(region.data + color);
            budget.claim(
                kMaterialColorTermCount * sizeof(std::array<std::uint32_t, 4u>),
                "NN material color output is too large");
            material.color_terms_bits.reserve(kMaterialColorTermCount);
            for (std::size_t term = 0u; term < kMaterialColorTermCount; ++term) {
                const std::size_t term_offset = color + kMaterialColorTermsOffset + term * 16u;
                material.color_terms_bits.push_back({{
                    read_le32(region.data + term_offset),
                    read_le32(region.data + term_offset + 4u),
                    read_le32(region.data + term_offset + 8u),
                    read_le32(region.data + term_offset + 12u),
                }});
            }
            material.shininess_bits = read_le32(region.data + color + kMaterialShininessOffset);
            material.specular_intensity_bits = read_le32(
                region.data + color + kMaterialSpecularIntensityOffset);
        }

        const std::uint32_t state_relative = material.descriptor_bits[3];
        if (state_relative != 0u) {
            const std::size_t state = require_relocated_pointer_in_chunk(
                region,
                relocations,
                descriptor_data_relative + 12u,
                state_relative,
                kMaterialStateWordCount * 4u,
                object_chunk,
                "NN material render-state pointer is invalid");
            NnMaterialStateData render_state{{}};
            for (std::size_t word = 0u; word < render_state.words.size(); ++word) {
                render_state.words[word] = read_le32(region.data + state + word * 4u);
            }
            material.render_state = render_state;
        }

        const std::size_t stage_count = read_nonnegative_i32(
            region.data + descriptor + 20u,
            "NN material stage count is negative");
        const std::uint32_t stage_relative = material.descriptor_bits[6];
        const std::size_t stage_size = has_shader_stage
            ? kStandardMaterialStageSize
            : kLegacyMaterialStageSize;
        const std::size_t stage_word_count = stage_size / 4u;
        if (stage_count > 16u) {
            fail("NN material stage count is unsupported");
        }
        if (stage_count == 0u) {
            if (stage_relative != 0u) {
                fail("empty NN material stage array has a nonzero pointer");
            }
        } else {
            const std::size_t stage_bytes = checked_product(
                stage_count,
                stage_size,
                "NN material stage array is too large");
            const std::size_t stages = require_relocated_pointer_in_chunk(
                region,
                relocations,
                descriptor_data_relative + 24u,
                stage_relative,
                stage_bytes,
                object_chunk,
                "NN material stage pointer is invalid");
            budget.claim(
                checked_product(
                    stage_count,
                    sizeof(NnMaterialStageData),
                    "NN material stage output is too large"),
                "NN material stage output is too large");
            budget.claim(
                checked_product(
                    stage_count,
                    stage_word_count * sizeof(std::uint32_t),
                    "NN material stage words are too large"),
                "NN material stage words are too large");
            material.stages.reserve(stage_count);
            for (std::size_t stage_index = 0u; stage_index < stage_count; ++stage_index) {
                const std::size_t stage = stages + stage_index * stage_size;
                NnMaterialStageData parsed_stage{{}, read_le32(region.data + stage),
                    read_le_i32(region.data + stage + 4u)};
                parsed_stage.raw_words.reserve(stage_word_count);
                for (std::size_t word = 0u; word < stage_word_count; ++word) {
                    parsed_stage.raw_words.push_back(read_le32(region.data + stage + word * 4u));
                }
                if ((parsed_stage.flags & 0xF0000000u) != 0u &&
                    (parsed_stage.texture_index < 0 ||
                     static_cast<std::size_t>(parsed_stage.texture_index) >= texture_count)) {
                    fail("NN material stage texture index is out of range");
                }
                material.stages.push_back(std::move(parsed_stage));
            }
        }
        materials.push_back(std::move(material));
    }
    return materials;
}

}

NnModelData parse_pc_nn_model(const std::uint8_t* data, std::size_t size) {
    if (data == nullptr) {
        fail("NN model data is null");
    }
    if (size < kDataBase) {
        fail("NN model is truncated before its data base");
    }

    const std::vector<Chunk> chunks = walk_chunks(data, size);
    if (chunks.size() != 5u && chunks.size() != 6u) {
        fail("NN chunk sequence is unsupported");
    }
    require_tag(data, chunks[0], "NZIF", "NN model does not begin with NZIF");
    const bool has_texture_chunk = chunks.size() == 6u;
    const Chunk* texture_chunk = has_texture_chunk ? &chunks[1] : nullptr;
    const Chunk& object_chunk = chunks[has_texture_chunk ? 2u : 1u];
    const Chunk& nof0_chunk = chunks[has_texture_chunk ? 3u : 2u];
    const Chunk& name_chunk = chunks[has_texture_chunk ? 4u : 3u];
    const Chunk& end_chunk = chunks[has_texture_chunk ? 5u : 4u];
    if (has_texture_chunk) {
        require_tag(data, *texture_chunk, "NZTL", "NN data chunk sequence is unsupported");
    }
    require_tag(data, object_chunk, "NZOB", "NN object chunk is missing");
    require_tag(data, nof0_chunk, "NOF0", "NN relocation chunk is missing");
    require_tag(data, name_chunk, "NFN0", "NN source-name chunk is missing");
    require_tag(data, end_chunk, "NEND", "NN terminator chunk is missing");
    if (chunks[0].payload_size != kNnIfPayloadSize) {
        fail("NZIF payload size is unsupported");
    }
    if (end_chunk.payload_size != 8u) {
        fail("NEND payload size is unsupported");
    }
    if (name_chunk.payload_size < 9u) {
        fail("NFN0 payload is truncated");
    }

    const std::uint32_t declared_chunk_count = read_le32(data + 8u);
    const std::uint32_t declared_data_base = read_le32(data + 12u);
    const std::uint32_t declared_data_size = read_le32(data + 16u);
    const std::uint32_t declared_nof0_offset = read_le32(data + 20u);
    const std::uint32_t declared_nof0_size = read_le32(data + 24u);
    const std::uint32_t declared_version = read_le32(data + 28u);
    if (declared_chunk_count != (has_texture_chunk ? 2u : 1u) ||
        static_cast<std::size_t>(declared_data_base) != kDataBase ||
        declared_version != kNnIfVersion ||
        static_cast<std::size_t>(declared_nof0_offset) != nof0_chunk.offset ||
        static_cast<std::size_t>(declared_data_size) != nof0_chunk.offset - kDataBase ||
        static_cast<std::size_t>(declared_nof0_size) != kChunkHeaderSize + nof0_chunk.payload_size) {
        fail("NZIF declarations do not match the NN chunk layout");
    }
    if (nof0_chunk.offset < kDataBase ||
        chunks[0].offset + kChunkHeaderSize + chunks[0].payload_size != kDataBase) {
        fail("NN data base does not follow NZIF");
    }

    const DataRegion region{data, kDataBase, nof0_chunk.offset};
    if (nof0_chunk.payload_size < 8u) {
        fail("NOF0 payload is truncated");
    }
    const std::size_t relocation_count = read_nonnegative_i32(
        data + nof0_chunk.offset + 8u,
        "NOF0 relocation count is negative");
    const std::size_t relocation_bytes = checked_product(
        relocation_count,
        4u,
        "NOF0 relocation table is too large");
    const std::size_t required_nof0_payload = checked_sum(
        8u,
        relocation_bytes,
        "NOF0 relocation table is too large");
    if (required_nof0_payload > nof0_chunk.payload_size) {
        fail("NOF0 relocation table exceeds its chunk");
    }
    const std::size_t nof0_padding = nof0_chunk.payload_size - required_nof0_payload;
    if (nof0_padding > 12u || (nof0_padding & 3u) != 0u) {
        fail("NOF0 padding is unsupported");
    }
    if (read_le32(data + nof0_chunk.offset + 12u) != 0u) {
        fail("NOF0 reserved word is unsupported");
    }
    if (relocation_count > (region.end - region.base) / 4u) {
        fail("NOF0 relocation count exceeds the data region");
    }
    RelocationLocations relocations;
    relocations.reserve(relocation_count);
    for (std::size_t index = 0u; index < relocation_count; ++index) {
        const std::uint32_t location_raw = read_le32(
            data + nof0_chunk.offset + 16u + index * 4u);
        if ((location_raw & 3u) != 0u) {
            fail("NOF0 relocation location is unaligned");
        }
        region.at(
            location_raw,
            4u,
            "NOF0 relocation location is outside the data region");
        relocations.push_back(location_raw);
    }
    std::sort(relocations.begin(), relocations.end());
    if (std::unique(relocations.begin(), relocations.end()) != relocations.end()) {
        fail("NOF0 relocation table has repeated locations");
    }
    for (std::uint32_t location_raw : relocations) {
        const std::size_t target_word = region.at(
            location_raw,
            4u,
            "NOF0 relocation location is outside the data region");
        const std::uint32_t target_relative = read_le32(data + target_word);
        if (target_relative != 0u) {
            region.at(target_relative, 1u, "NOF0 relocation target is outside the data region");
        }
    }

    if (object_chunk.payload_size < 8u) {
        fail("NZOB data header is truncated");
    }
    const std::uint32_t object_relative = read_le32(data + object_chunk.offset + 8u);
    if (read_le32(data + object_chunk.offset + 12u) != kNnObjectVersion) {
        fail("NZOB data header version is unsupported");
    }
    const std::size_t object = region.at(object_relative, kNnObjectSize, "NZOB object header is out of range");
    require_chunk_data_range(object, kNnObjectSize, object_chunk, "NZOB object header is outside its chunk");

    const std::size_t material_count = read_nonnegative_i32(
        data + object + 0x10u,
        "NN material count is negative");
    const std::uint32_t material_relative = read_le32(data + object + 0x14u);
    const std::size_t vertex_count = read_nonnegative_i32(
        data + object + 0x18u,
        "NN vertex-list count is negative");
    const std::uint32_t vertex_relative = read_le32(data + object + 0x1Cu);
    const std::size_t primitive_count = read_nonnegative_i32(
        data + object + 0x20u,
        "NN primitive-list count is negative");
    const std::uint32_t primitive_relative = read_le32(data + object + 0x24u);
    const std::size_t node_count = read_nonnegative_i32(
        data + object + 0x28u,
        "NN node count is negative");
    const std::size_t max_node_depth = read_nonnegative_i32(
        data + object + 0x2Cu,
        "NN maximum node depth is negative");
    const std::uint32_t node_relative = read_le32(data + object + 0x30u);
    const std::size_t matrix_palette_count = read_nonnegative_i32(
        data + object + 0x34u,
        "NN matrix-palette count is negative");
    const std::size_t sub_object_count = read_nonnegative_i32(
        data + object + 0x38u,
        "NN subobject count is negative");
    const std::uint32_t sub_object_relative = read_le32(data + object + 0x3Cu);
    const std::size_t texture_count = read_nonnegative_i32(
        data + object + 0x40u,
        "NN texture count is negative");
    const std::uint32_t flags = read_le32(data + object + 0x44u);
    const std::uint32_t version = read_le32(data + object + 0x48u);
    if (version != kNnObjectVersion) {
        fail("NN object version is unsupported");
    }

    const std::size_t material_array = validate_declared_list(
        region,
        relocations,
        static_cast<std::size_t>(object_relative) + 0x14u,
        material_relative,
        material_count,
        kPointerRecordSize,
        object_chunk,
        "NN material pointer array is invalid");
    const std::size_t vertex_array = validate_declared_list(
        region,
        relocations,
        static_cast<std::size_t>(object_relative) + 0x1Cu,
        vertex_relative,
        vertex_count,
        kPointerRecordSize,
        object_chunk,
        "NN vertex pointer array is invalid");
    const std::size_t primitive_array = validate_declared_list(
        region,
        relocations,
        static_cast<std::size_t>(object_relative) + 0x24u,
        primitive_relative,
        primitive_count,
        kPointerRecordSize,
        object_chunk,
        "NN primitive pointer array is invalid");
    const std::size_t node_array = validate_declared_list(
        region,
        relocations,
        static_cast<std::size_t>(object_relative) + 0x30u,
        node_relative,
        node_count,
        kNodeSize,
        object_chunk,
        "NN node array is invalid");
    const std::size_t sub_object_array = validate_declared_list(
        region,
        relocations,
        static_cast<std::size_t>(object_relative) + 0x3Cu,
        sub_object_relative,
        sub_object_count,
        kSubObjectSize,
        object_chunk,
        "NN subobject array is invalid");

    for (std::size_t index = 0u; index < material_count; ++index) {
        const std::size_t pointer_offset = material_array + index * kPointerRecordSize;
        const std::uint32_t descriptor_relative = read_le32(data + pointer_offset + 4u);
        require_relocated_pointer_in_chunk(
            region,
            relocations,
            static_cast<std::size_t>(material_relative) + index * kPointerRecordSize + 4u,
            descriptor_relative,
            1u,
            object_chunk,
            "NN material descriptor pointer is invalid");
    }

    OutputBudget serialized_budget(region.end - region.base);
    OutputBudget semantic_budget(checked_product(
        region.end - region.base,
        8u,
        "NN semantic output budget is too large"));
    std::vector<NnTextureData> textures = parse_texture_list(
        region,
        relocations,
        texture_chunk,
        static_cast<std::uint32_t>(texture_count),
        semantic_budget);

    NnModelData model{
        flags,
        version,
        static_cast<std::uint32_t>(material_count),
        static_cast<std::uint32_t>(node_count),
        static_cast<std::uint32_t>(max_node_depth),
        static_cast<std::uint32_t>(matrix_palette_count),
        static_cast<std::uint32_t>(texture_count),
        {{
            read_le32(data + object),
            read_le32(data + object + 4u),
            read_le32(data + object + 8u),
            read_le32(data + object + 12u),
        }},
        {{
            read_le32(data + object + 0x4Cu),
            read_le32(data + object + 0x50u),
            read_le32(data + object + 0x54u),
        }},
        {},
        {},
        {},
        {},
    };
    model.textures = std::move(textures);
    model.nodes = parse_nodes(
        data,
        node_array,
        node_count,
        matrix_palette_count,
        semantic_budget);
    if (!model.nodes.empty()) {
        model.first_node_translation_bits = model.nodes[0].translation_bits;
    }
    model.materials = parse_materials(
        region,
        relocations,
        material_array,
        material_relative,
        material_count,
        object_chunk,
        texture_count,
        semantic_budget);
    serialized_budget.claim(
        checked_product(vertex_count, sizeof(NnVertexData), "NN vertex output is too large"),
        "NN vertex output is too large");
    model.vertices.reserve(vertex_count);
    for (std::size_t index = 0u; index < vertex_count; ++index) {
        const std::size_t pointer_offset = vertex_array + index * kPointerRecordSize;
        const std::uint32_t pointer_flags = read_le32(data + pointer_offset);
        const std::uint32_t descriptor_relative = read_le32(data + pointer_offset + 4u);
        const std::size_t descriptor = require_relocated_pointer_in_chunk(
            region,
            relocations,
            static_cast<std::size_t>(vertex_relative) + index * kPointerRecordSize + 4u,
            descriptor_relative,
            kVertexDescriptorPrefixSize,
            object_chunk,
            "NN vertex descriptor pointer is invalid");
        const std::size_t descriptor_data_relative = descriptor - region.base;
        const std::uint32_t format = read_le32(data + descriptor);
        const std::uint32_t fvf = read_le32(data + descriptor + 4u);
        const std::uint32_t stride = read_le32(data + descriptor + 8u);
        const std::size_t count = read_nonnegative_i32(
            data + descriptor + 12u,
            "NN vertex count is negative");
        const std::uint32_t buffer_relative = read_le32(data + descriptor + 16u);
        const VertexLayout* layout = find_vertex_layout(format, fvf, stride);
        if (layout == nullptr) {
            fail("NN vertex format, FVF, or stride is unsupported");
        }
        if (count == 0u) {
            fail("NN vertex descriptor has zero vertices");
        }
        const std::size_t byte_count = checked_product(
            count,
            static_cast<std::size_t>(stride),
            "NN vertex buffer is too large");
        const std::size_t buffer = require_relocated_pointer_in_chunk(
            region,
            relocations,
            static_cast<std::size_t>(descriptor_relative) + 16u,
            buffer_relative,
            byte_count,
            object_chunk,
            "NN vertex buffer pointer is invalid");
        NnVertexData vertex{
            pointer_flags,
            format,
            fvf,
            stride,
            static_cast<std::uint32_t>(count),
            {},
            {},
            {},
        };
        const std::size_t matrix_pointer_field = descriptor_data_relative + 24u;
        const std::uint32_t matrix_relative = read_le32(data + descriptor + 24u);
        if (has_weighted_matrix_subset(*layout)) {
            if (!has_relocation(relocations, matrix_pointer_field)) {
                fail("NN weighted vertex matrix subset is not relocation-declared");
            }
            const std::size_t matrix_count = read_nonnegative_i32(
                data + descriptor + 20u,
                "NN vertex matrix subset count is negative");
            if (matrix_count == 0u || matrix_count > 16u ||
                (layout->blend_index_offset == kAbsentAttributeOffset && matrix_count > 4u)) {
                fail("NN vertex matrix subset count is unsupported");
            }
            const std::size_t matrix_bytes = checked_product(
                matrix_count,
                4u,
                "NN vertex matrix subset is too large");
            const std::size_t matrix_data = require_relocated_pointer_in_chunk(
                region,
                relocations,
                matrix_pointer_field,
                matrix_relative,
                matrix_bytes,
                object_chunk,
                "NN vertex matrix subset pointer is invalid");
            serialized_budget.claim(matrix_bytes, "NN vertex output is too large");
            vertex.matrix_indices.reserve(matrix_count);
            for (std::size_t matrix = 0u; matrix < matrix_count; ++matrix) {
                const std::uint32_t matrix_index = read_le32(data + matrix_data + matrix * 4u);
                if (matrix_index >= matrix_palette_count) {
                    fail("NN vertex matrix subset index is out of range");
                }
                vertex.matrix_indices.push_back(matrix_index);
            }
        } else {
            if (read_le32(data + descriptor + 20u) != 0u || matrix_relative != 0u ||
                has_relocation(relocations, matrix_pointer_field)) {
                fail("NN rigid vertex descriptor has a matrix subset");
            }
        }
        serialized_budget.claim(byte_count, "NN vertex output is too large");
        vertex.bytes.assign(data + buffer, data + buffer + byte_count);
        decode_vertex_attributes(
            vertex,
            data + buffer,
            *layout,
            vertex.matrix_indices.size(),
            semantic_budget);
        model.vertices.push_back(std::move(vertex));
    }

    serialized_budget.claim(
        checked_product(primitive_count, sizeof(NnPrimitiveData), "NN primitive output is too large"),
        "NN primitive output is too large");
    model.primitives.reserve(primitive_count);
    std::vector<std::uint16_t> primitive_max_indices;
    primitive_max_indices.reserve(primitive_count);
    for (std::size_t index = 0u; index < primitive_count; ++index) {
        const std::size_t pointer_offset = primitive_array + index * kPointerRecordSize;
        const std::uint32_t pointer_flags = read_le32(data + pointer_offset);
        const std::uint32_t descriptor_relative = read_le32(data + pointer_offset + 4u);
        const std::size_t descriptor = require_relocated_pointer_in_chunk(
            region,
            relocations,
            static_cast<std::size_t>(primitive_relative) + index * kPointerRecordSize + 4u,
            descriptor_relative,
            kPrimitiveDescriptorSize,
            object_chunk,
            "NN primitive descriptor pointer is invalid");
        const std::uint32_t mode = read_le32(data + descriptor);
        const std::size_t total_index_count = read_nonnegative_i32(
            data + descriptor + 4u,
            "NN primitive index count is negative");
        const std::size_t strip_count = read_nonnegative_i32(
            data + descriptor + 8u,
            "NN primitive strip count is negative");
        const std::uint32_t strip_relative = read_le32(data + descriptor + 12u);
        const std::uint32_t index_relative = read_le32(data + descriptor + 16u);
        if (mode != kTriangleStripMode || total_index_count == 0u || strip_count == 0u) {
            fail("NN primitive descriptor is unsupported");
        }
        const std::size_t strip_bytes = checked_product(
            strip_count,
            4u,
            "NN primitive strip table is too large");
        const std::size_t strip_data = require_relocated_pointer_in_chunk(
            region,
            relocations,
            static_cast<std::size_t>(descriptor_relative) + 12u,
            strip_relative,
            strip_bytes,
            object_chunk,
            "NN primitive strip pointer is invalid");
        const std::size_t index_bytes = checked_product(
            total_index_count,
            2u,
            "NN primitive index buffer is too large");
        const std::size_t index_data = require_relocated_pointer_in_chunk(
            region,
            relocations,
            static_cast<std::size_t>(descriptor_relative) + 16u,
            index_relative,
            index_bytes,
            object_chunk,
            "NN primitive index pointer is invalid");
        NnPrimitiveData primitive{pointer_flags, mode, {}, {}};
        serialized_budget.claim(strip_bytes, "NN primitive output is too large");
        primitive.strip_lengths.reserve(strip_count);
        std::size_t strip_total = 0u;
        for (std::size_t strip = 0u; strip < strip_count; ++strip) {
            const std::size_t length = read_nonnegative_i32(
                data + strip_data + strip * 4u,
                "NN primitive strip length is negative");
            strip_total = checked_sum(strip_total, length, "NN primitive strip total is too large");
            primitive.strip_lengths.push_back(static_cast<std::uint32_t>(length));
        }
        if (strip_total != total_index_count) {
            fail("NN primitive strip lengths do not match index count");
        }
        serialized_budget.claim(index_bytes, "NN primitive output is too large");
        primitive.indices.reserve(total_index_count);
        std::uint16_t maximum_index = 0u;
        for (std::size_t item = 0u; item < total_index_count; ++item) {
            const std::uint16_t index_value = read_le16(data + index_data + item * 2u);
            if (index_value > maximum_index) {
                maximum_index = index_value;
            }
            primitive.indices.push_back(index_value);
        }
        primitive_max_indices.push_back(maximum_index);
        model.primitives.push_back(std::move(primitive));
    }

    serialized_budget.claim(
        checked_product(sub_object_count, sizeof(NnSubObjectData), "NN subobject output is too large"),
        "NN subobject output is too large");
    model.sub_objects.reserve(sub_object_count);
    for (std::size_t index = 0u; index < sub_object_count; ++index) {
        const std::size_t sub_object = sub_object_array + index * kSubObjectSize;
        const std::uint32_t sub_object_flags = read_le32(data + sub_object);
        const std::size_t mesh_count = read_nonnegative_i32(
            data + sub_object + 4u,
            "NN subobject mesh count is negative");
        const std::uint32_t mesh_relative = read_le32(data + sub_object + 8u);
        const std::size_t sub_texture_count = read_nonnegative_i32(
            data + sub_object + 12u,
            "NN subobject texture count is negative");
        const std::uint32_t sub_texture_relative = read_le32(data + sub_object + 16u);
        const std::size_t mesh_array = validate_declared_list(
            region,
            relocations,
            static_cast<std::size_t>(sub_object_relative) + index * kSubObjectSize + 8u,
            mesh_relative,
            mesh_count,
            kMeshSize,
            object_chunk,
            "NN subobject mesh array is invalid");
        validate_declared_list(
            region,
            relocations,
            static_cast<std::size_t>(sub_object_relative) + index * kSubObjectSize + 16u,
            sub_texture_relative,
            sub_texture_count,
            4u,
            object_chunk,
            "NN subobject texture array is invalid");
        NnSubObjectData parsed_sub_object{
            sub_object_flags,
            {{static_cast<std::uint32_t>(sub_texture_count), sub_texture_relative}},
            {},
        };
        serialized_budget.claim(
            checked_product(mesh_count, sizeof(NnMeshData), "NN mesh output is too large"),
            "NN mesh output is too large");
        parsed_sub_object.meshes.reserve(mesh_count);
        for (std::size_t mesh_index = 0u; mesh_index < mesh_count; ++mesh_index) {
            const std::size_t mesh = mesh_array + mesh_index * kMeshSize;
            const std::int32_t node_index = read_le_i32(data + mesh + 16u);
            const std::int32_t matrix_index = read_le_i32(data + mesh + 20u);
            const std::int32_t material_index = read_le_i32(data + mesh + 24u);
            const std::int32_t vertex_index = read_le_i32(data + mesh + 28u);
            const std::int32_t primitive_index = read_le_i32(data + mesh + 32u);
            if (node_index < 0 || static_cast<std::size_t>(node_index) >= node_count ||
                (matrix_index < -1) ||
                (matrix_index >= 0 && static_cast<std::size_t>(matrix_index) >= matrix_palette_count) ||
                material_index < 0 || static_cast<std::size_t>(material_index) >= material_count ||
                vertex_index < 0 || static_cast<std::size_t>(vertex_index) >= model.vertices.size() ||
                primitive_index < 0 || static_cast<std::size_t>(primitive_index) >= model.primitives.size()) {
                fail("NN mesh reference is out of range");
            }
            const NnVertexData& vertex = model.vertices[static_cast<std::size_t>(vertex_index)];
            const std::uint16_t maximum_index =
                primitive_max_indices[static_cast<std::size_t>(primitive_index)];
            if (static_cast<std::uint32_t>(maximum_index) >= vertex.count) {
                fail("NN mesh index is outside its vertex list");
            }
            parsed_sub_object.meshes.push_back({
                {{
                    read_le32(data + mesh),
                    read_le32(data + mesh + 4u),
                    read_le32(data + mesh + 8u),
                    read_le32(data + mesh + 12u),
                }},
                node_index,
                matrix_index,
                static_cast<std::uint32_t>(material_index),
                static_cast<std::uint32_t>(vertex_index),
                static_cast<std::uint32_t>(primitive_index),
                read_le32(data + mesh + 36u),
            });
        }
        model.sub_objects.push_back(std::move(parsed_sub_object));
    }

    return model;
}
