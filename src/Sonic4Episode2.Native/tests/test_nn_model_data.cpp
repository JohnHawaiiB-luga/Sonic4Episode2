#include "nn_model_data.h"
#include "stage_data.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace {

constexpr std::size_t kDataBase = 0x20u;
constexpr std::size_t kNztlPayloadSize = 0x180u;
constexpr std::size_t kNzobPayloadSize = 0x3000u;
constexpr std::size_t kNodeSize = 0x90u;
constexpr std::size_t kMeshSize = 0x28u;
constexpr std::size_t kSubObjectSize = 0x14u;
constexpr std::size_t kMaterialColorRecordSize = 76u;
constexpr std::size_t kMaterialStateRecordSize = 28u;
constexpr std::array<std::array<std::uint32_t, 4u>, 4u> kMaterialColorTerms = {{
    {{0x3E99999Au, 0x80000000u, 0x7FC00001u, 0x3F800000u}},
    {{0x3F800000u, 0x3F000000u, 0x3E800000u, 0x00000000u}},
    {{0xFF800000u, 0x3F4CCCCDu, 0x3F19999Au, 0x3F000000u}},
    {{0x7F800000u, 0x80000001u, 0x00000001u, 0x7FC00002u}},
}};
constexpr std::uint32_t kMaterialShininessBits = 0xFF800000u;
constexpr std::uint32_t kMaterialSpecularIntensityBits = 0u;
constexpr std::array<std::uint32_t, 16u> kExtendedMaterialStageWords = {{
    0x60000008u,
    0u,
    0xD0000002u,
    0xD0000003u,
    0xD0000004u,
    0xD0000005u,
    0xD0000006u,
    0xD0000007u,
    0x00010001u,
    0xD0000009u,
    0xD000000Au,
    0xD000000Bu,
    0xD000000Cu,
    0xD000000Du,
    0xD000000Eu,
    0xD000000Fu,
}};

struct VertexSpec {
    std::uint32_t flags;
    std::uint32_t format;
    std::uint32_t fvf;
    std::uint32_t stride;
    std::uint32_t count;
    std::vector<std::uint8_t> bytes;
    std::vector<std::uint32_t> matrix_indices;
};

struct PrimitiveSpec {
    std::uint32_t flags;
    std::vector<std::uint32_t> strip_lengths;
    std::vector<std::uint16_t> indices;
};

struct MeshSpec {
    std::int32_t node_index;
    std::int32_t matrix_index;
    std::int32_t material_index;
    std::int32_t vertex_index;
    std::int32_t primitive_index;
    std::uint32_t reserved;
};

struct Fixture {
    std::vector<std::uint8_t> data;
    std::size_t nzob_offset;
    std::size_t nof0_offset;
    std::size_t object_offset;
    std::vector<std::size_t> nodes;
    std::vector<std::size_t> vertex_descriptors;
    std::vector<std::size_t> vertex_buffers;
    std::vector<std::size_t> primitive_descriptors;
    std::vector<std::size_t> primitive_indices;
    std::vector<std::size_t> meshes;
    std::size_t subobject_offset;
    std::vector<std::vector<std::uint8_t>> vertex_bytes;
};

bool check(bool condition, const char* message) {
    if (condition) {
        return true;
    }
    std::fprintf(stderr, "%s\n", message);
    return false;
}

void write_le16(std::vector<std::uint8_t>& data, std::size_t offset, std::uint16_t value) {
    data[offset] = static_cast<std::uint8_t>(value & 0xFFu);
    data[offset + 1u] = static_cast<std::uint8_t>((value >> 8u) & 0xFFu);
}

void write_le32(std::vector<std::uint8_t>& data, std::size_t offset, std::uint32_t value) {
    data[offset] = static_cast<std::uint8_t>(value & 0xFFu);
    data[offset + 1u] = static_cast<std::uint8_t>((value >> 8u) & 0xFFu);
    data[offset + 2u] = static_cast<std::uint8_t>((value >> 16u) & 0xFFu);
    data[offset + 3u] = static_cast<std::uint8_t>((value >> 24u) & 0xFFu);
}

std::uint32_t read_le32(const std::vector<std::uint8_t>& data, std::size_t offset) {
    return static_cast<std::uint32_t>(
        static_cast<std::uint32_t>(data[offset]) |
        (static_cast<std::uint32_t>(data[offset + 1u]) << 8u) |
        (static_cast<std::uint32_t>(data[offset + 2u]) << 16u) |
        (static_cast<std::uint32_t>(data[offset + 3u]) << 24u));
}

std::size_t material_descriptor_offset(const Fixture& fixture, std::size_t index) {
    const std::size_t material_array = kDataBase + static_cast<std::size_t>(
        read_le32(fixture.data, fixture.object_offset + 20u));
    return kDataBase + static_cast<std::size_t>(
        read_le32(fixture.data, material_array + index * 8u + 4u));
}

std::size_t material_color_offset(const Fixture& fixture, std::size_t index) {
    return kDataBase + static_cast<std::size_t>(
        read_le32(fixture.data, material_descriptor_offset(fixture, index) + 8u));
}

bool replace_relocation_location(
    Fixture& fixture,
    std::uint32_t original_location,
    std::uint32_t replacement_location) {
    const std::size_t relocation_count = read_le32(fixture.data, fixture.nof0_offset + 8u);
    for (std::size_t index = 0u; index < relocation_count; ++index) {
        const std::size_t entry = fixture.nof0_offset + 16u + index * 4u;
        if (read_le32(fixture.data, entry) == original_location) {
            write_le32(fixture.data, entry, replacement_location);
            return true;
        }
    }
    return false;
}

void write_tag(std::vector<std::uint8_t>& data, std::size_t offset, const char (&tag)[5]) {
    for (std::size_t index = 0u; index < 4u; ++index) {
        data[offset + index] = static_cast<std::uint8_t>(tag[index]);
    }
}

std::size_t align_four(std::size_t value) {
    return (value + 3u) & ~std::size_t(3u);
}

class DataAllocator {
public:
    DataAllocator(std::vector<std::uint8_t>& data, std::size_t start, std::size_t end)
        : data_(data), cursor_(start), end_(end) {}

    std::size_t allocate(std::size_t length) {
        cursor_ = align_four(cursor_);
        if (cursor_ > end_ || length > end_ - cursor_) {
            throw std::runtime_error("synthetic NN fixture exhausted its data region");
        }
        const std::size_t result = cursor_;
        cursor_ += length;
        return result;
    }

private:
    std::vector<std::uint8_t>& data_;
    std::size_t cursor_;
    std::size_t end_;
};

std::vector<std::uint8_t> make_vertex_bytes(std::uint32_t count, std::uint32_t stride, std::uint8_t seed) {
    std::vector<std::uint8_t> bytes(static_cast<std::size_t>(count) * stride, 0u);
    for (std::size_t index = 0u; index < bytes.size(); ++index) {
        bytes[index] = static_cast<std::uint8_t>(
            static_cast<std::uint32_t>(seed) + static_cast<std::uint32_t>(index * 13u));
    }
    return bytes;
}

std::vector<std::uint8_t> make_indexed_skinned_vertex_bytes() {
    constexpr std::uint32_t kStride = 48u;
    std::vector<std::uint8_t> bytes(kStride * 3u, 0u);
    for (std::size_t vertex = 0u; vertex < 3u; ++vertex) {
        const std::size_t base = vertex * kStride;
        write_le32(bytes, base, 0x3F800000u + static_cast<std::uint32_t>(vertex));
        write_le32(bytes, base + 4u, 0x40000000u + static_cast<std::uint32_t>(vertex));
        write_le32(bytes, base + 8u, 0x40400000u + static_cast<std::uint32_t>(vertex));
        write_le32(bytes, base + 12u, 0x3E800000u);
        write_le32(bytes, base + 16u, 0x3F000000u);
        write_le32(bytes, base + 20u, 0x3E800000u);
        bytes[base + 24u] = static_cast<std::uint8_t>(vertex);
        bytes[base + 25u] = 3u;
        bytes[base + 26u] = 2u;
        bytes[base + 27u] = 1u;
        write_le32(bytes, base + 28u, 0x00000000u);
        write_le32(bytes, base + 32u, 0x00000000u);
        write_le32(bytes, base + 36u, 0x3F800000u);
        write_le32(bytes, base + 40u, 0x3E800000u);
        write_le32(bytes, base + 44u, 0x3F000000u);
    }
    return bytes;
}

Fixture make_geometry_fixture() {
    const std::vector<VertexSpec> vertices = {
        {0x00000007u, 0x00019u, 0x0C2u, 20u, 3u, make_vertex_bytes(3u, 20u, 0x10u), {}},
        {0x00000001u, 0x10019u, 0x1C2u, 28u, 4u, make_vertex_bytes(4u, 28u, 0x20u), {}},
        {0x00000001u, 0x1001Bu, 0x1D2u, 40u, 3u, make_vertex_bytes(3u, 40u, 0x30u), {}},
        {0x00000001u, 0x1701Bu, 0x1DAu, 52u, 4u, make_vertex_bytes(4u, 52u, 0x40u), {0u, 2u, 3u}},
        {0x00000001u, 0x2001Bu, 0x2D2u, 48u, 3u, make_vertex_bytes(3u, 48u, 0x50u), {}},
        {0x00000001u, 0x2701Bu, 0x2DAu, 60u, 4u, make_vertex_bytes(4u, 60u, 0x60u), {0u, 1u, 2u, 3u}},
        {0x00000001u, 0x17403u, 0x111Au, 48u, 3u, make_indexed_skinned_vertex_bytes(), {
            0u, 1u, 2u, 3u, 4u, 5u, 6u, 7u,
            8u, 9u, 10u, 11u, 12u, 13u, 14u, 15u}},
    };
    const std::vector<PrimitiveSpec> primitives = {
        {0x00000009u, {3u, 3u}, {0u, 1u, 2u, 2u, 3u, 1u}},
        {0x00000001u, {3u}, {2u, 1u, 0u}},
    };
    const std::vector<MeshSpec> meshes = {
        {0, 0, 0, 1, 0, 0xA0A0A0A0u},
        {1, 1, 1, 1, 0, 0xB0B0B0B0u},
        {0, -1, 0, 0, 1, 0xC0C0C0C0u},
    };

    const std::size_t nztl_offset = kDataBase;
    const std::size_t nzob_offset = nztl_offset + 8u + kNztlPayloadSize;
    const std::size_t nof0_offset = nzob_offset + 8u + kNzobPayloadSize;
    std::vector<std::uint8_t> data(nof0_offset, 0u);
    Fixture fixture{std::move(data), nzob_offset, nof0_offset, 0u, {}, {}, {}, {}, {}, {}, 0u, {}};
    std::vector<std::uint32_t> relocations;

    const auto add_pointer = [&](std::size_t field, std::size_t target) {
        write_le32(
            fixture.data,
            field,
            static_cast<std::uint32_t>(target - kDataBase));
        relocations.push_back(static_cast<std::uint32_t>(field - kDataBase));
    };

    write_tag(fixture.data, nztl_offset, "NZTL");
    write_le32(fixture.data, nztl_offset + 4u, static_cast<std::uint32_t>(kNztlPayloadSize));
    const std::size_t texture_root = nztl_offset + 0x30u;
    const std::size_t texture_list = nztl_offset + 0x40u;
    const std::size_t texture_name = nztl_offset + 0x70u;
    write_le32(
        fixture.data,
        nztl_offset + 8u,
        static_cast<std::uint32_t>(texture_root - kDataBase));
    write_le32(fixture.data, nztl_offset + 12u, 0u);
    write_le32(fixture.data, texture_root, 1u);
    add_pointer(texture_root + 4u, texture_list);
    write_le32(fixture.data, texture_list, 0x10004u);
    add_pointer(texture_list + 4u, texture_name);
    const char texture_name_text[] = "fixture.dds";
    for (std::size_t index = 0u; index < sizeof(texture_name_text); ++index) {
        fixture.data[texture_name + index] = static_cast<std::uint8_t>(texture_name_text[index]);
    }

    write_tag(fixture.data, nzob_offset, "NZOB");
    write_le32(fixture.data, nzob_offset + 4u, static_cast<std::uint32_t>(kNzobPayloadSize));
    fixture.object_offset = nzob_offset + 0x108u;
    write_le32(
        fixture.data,
        nzob_offset + 8u,
        static_cast<std::uint32_t>(fixture.object_offset - kDataBase));
    write_le32(fixture.data, nzob_offset + 12u, 3u);

    DataAllocator allocator(
        fixture.data,
        fixture.object_offset + 0x58u,
        nof0_offset);
    const std::size_t material_array = allocator.allocate(3u * 8u);
    const std::size_t material0 = allocator.allocate(0x1Cu);
    const std::size_t material1 = allocator.allocate(0x20u);
    const std::size_t material2 = allocator.allocate(0x1Cu);
    const std::size_t colour_block = allocator.allocate(kMaterialColorRecordSize);
    const std::size_t state_block = allocator.allocate(kMaterialStateRecordSize);
    const std::size_t stage_block = allocator.allocate(2u * 64u);
    const std::size_t extended_stage_block = allocator.allocate(64u);
    const std::size_t legacy_stage_block = allocator.allocate(32u);
    write_le32(fixture.data, material_array, 0x10000000u);
    add_pointer(material_array + 4u, material0);
    write_le32(fixture.data, material_array + 8u, 0x30000000u);
    add_pointer(material_array + 12u, material1);
    write_le32(fixture.data, material_array + 16u, 0x20000000u);
    add_pointer(material_array + 20u, material2);
    write_le32(fixture.data, material0, 0x00001102u);
    write_le32(fixture.data, material0 + 4u, 0xF00DF00Du);
    add_pointer(material0 + 8u, colour_block);
    add_pointer(material0 + 12u, state_block);
    write_le32(fixture.data, material0 + 16u, 0x12345678u);
    write_le32(fixture.data, material0 + 20u, 2u);
    add_pointer(material0 + 24u, stage_block);
    write_le32(fixture.data, material1, 0x00000000u);
    write_le32(fixture.data, material1 + 4u, 0xCAFEBABEu);
    write_le32(fixture.data, material1 + 20u, 1u);
    add_pointer(material1 + 24u, extended_stage_block);
    write_le32(fixture.data, material1 + 28u, 5u);
    write_le32(fixture.data, material2, 0x00000000u);
    write_le32(fixture.data, material2 + 4u, 0xDEADBEEFu);
    write_le32(fixture.data, material2 + 20u, 1u);
    add_pointer(material2 + 24u, legacy_stage_block);
    write_le32(fixture.data, colour_block, 2u);
    for (std::size_t term = 0u; term < kMaterialColorTerms.size(); ++term) {
        for (std::size_t component = 0u; component < kMaterialColorTerms[term].size(); ++component) {
            write_le32(
                fixture.data,
                colour_block + 4u + term * 16u + component * 4u,
                kMaterialColorTerms[term][component]);
        }
    }
    write_le32(fixture.data, colour_block + 68u, kMaterialShininessBits);
    write_le32(fixture.data, colour_block + 72u, kMaterialSpecularIntensityBits);
    for (std::size_t word = 0u; word < 7u; ++word) {
        write_le32(
            fixture.data,
            state_block + word * 4u,
            0xA0000000u + static_cast<std::uint32_t>(word));
    }
    for (std::size_t word = 0u; word < 16u; ++word) {
        write_le32(
            fixture.data,
            stage_block + word * 4u,
            0xB0000000u + static_cast<std::uint32_t>(word));
        write_le32(
            fixture.data,
            stage_block + 64u + word * 4u,
            0xC0000000u + static_cast<std::uint32_t>(word));
    }
    write_le32(fixture.data, stage_block, 0x60000002u);
    write_le32(fixture.data, stage_block + 4u, 0u);
    write_le32(fixture.data, stage_block + 32u, 0u);
    write_le32(fixture.data, stage_block + 36u, 0u);
    write_le32(fixture.data, stage_block + 64u, 0x20000004u);
    write_le32(fixture.data, stage_block + 68u, 0u);
    for (std::size_t word = 0u; word < kExtendedMaterialStageWords.size(); ++word) {
        write_le32(fixture.data, extended_stage_block + word * 4u, kExtendedMaterialStageWords[word]);
    }
    for (std::size_t word = 0u; word < 8u; ++word) {
        write_le32(
            fixture.data,
            legacy_stage_block + word * 4u,
            0xD0000000u + static_cast<std::uint32_t>(word));
    }
    write_le32(fixture.data, legacy_stage_block, 0x60000008u);
    write_le32(fixture.data, legacy_stage_block + 4u, 0u);

    const std::size_t vertex_array = allocator.allocate(vertices.size() * 8u);
    for (std::size_t index = 0u; index < vertices.size(); ++index) {
        const VertexSpec& spec = vertices[index];
        const std::size_t descriptor = allocator.allocate(28u);
        const std::size_t buffer = allocator.allocate(spec.bytes.size());
        fixture.vertex_descriptors.push_back(descriptor);
        fixture.vertex_buffers.push_back(buffer);
        fixture.vertex_bytes.push_back(spec.bytes);
        write_le32(fixture.data, vertex_array + index * 8u, spec.flags);
        add_pointer(vertex_array + index * 8u + 4u, descriptor);
        write_le32(fixture.data, descriptor, spec.format);
        write_le32(fixture.data, descriptor + 4u, spec.fvf);
        write_le32(fixture.data, descriptor + 8u, spec.stride);
        write_le32(fixture.data, descriptor + 12u, spec.count);
        add_pointer(descriptor + 16u, buffer);
        for (std::size_t byte = 0u; byte < spec.bytes.size(); ++byte) {
            fixture.data[buffer + byte] = spec.bytes[byte];
        }
        if (!spec.matrix_indices.empty()) {
            const std::size_t matrix_indices = allocator.allocate(spec.matrix_indices.size() * 4u);
            write_le32(
                fixture.data,
                descriptor + 20u,
                static_cast<std::uint32_t>(spec.matrix_indices.size()));
            add_pointer(descriptor + 24u, matrix_indices);
            for (std::size_t matrix = 0u; matrix < spec.matrix_indices.size(); ++matrix) {
                write_le32(fixture.data, matrix_indices + matrix * 4u, spec.matrix_indices[matrix]);
            }
        }
    }

    const std::size_t primitive_array = allocator.allocate(primitives.size() * 8u);
    for (std::size_t index = 0u; index < primitives.size(); ++index) {
        const PrimitiveSpec& spec = primitives[index];
        std::size_t total = 0u;
        for (std::uint32_t length : spec.strip_lengths) {
            total += length;
        }
        const std::size_t descriptor = allocator.allocate(20u);
        const std::size_t lengths = allocator.allocate(spec.strip_lengths.size() * 4u);
        const std::size_t indices = allocator.allocate(spec.indices.size() * 2u);
        fixture.primitive_descriptors.push_back(descriptor);
        fixture.primitive_indices.push_back(indices);
        write_le32(fixture.data, primitive_array + index * 8u, spec.flags);
        add_pointer(primitive_array + index * 8u + 4u, descriptor);
        write_le32(fixture.data, descriptor, 0x4810u);
        write_le32(fixture.data, descriptor + 4u, static_cast<std::uint32_t>(total));
        write_le32(
            fixture.data,
            descriptor + 8u,
            static_cast<std::uint32_t>(spec.strip_lengths.size()));
        add_pointer(descriptor + 12u, lengths);
        add_pointer(descriptor + 16u, indices);
        for (std::size_t strip = 0u; strip < spec.strip_lengths.size(); ++strip) {
            write_le32(fixture.data, lengths + strip * 4u, spec.strip_lengths[strip]);
        }
        for (std::size_t item = 0u; item < spec.indices.size(); ++item) {
            write_le16(fixture.data, indices + item * 2u, spec.indices[item]);
        }
    }

    const std::size_t node_array = allocator.allocate(2u * kNodeSize);
    fixture.nodes.push_back(node_array);
    fixture.nodes.push_back(node_array + kNodeSize);
    write_le32(fixture.data, fixture.nodes[0], 0x00000008u);
    write_le16(fixture.data, fixture.nodes[0] + 4u, 0u);
    write_le16(fixture.data, fixture.nodes[0] + 6u, 0xFFFFu);
    write_le16(fixture.data, fixture.nodes[0] + 8u, 1u);
    write_le16(fixture.data, fixture.nodes[0] + 10u, 0xFFFFu);
    write_le32(fixture.data, fixture.nodes[0] + 0x0Cu, 0x80000000u);
    write_le32(fixture.data, fixture.nodes[0] + 0x10u, 0x7FC01234u);
    write_le32(fixture.data, fixture.nodes[0] + 0x14u, 0xFFC00002u);
    write_le32(fixture.data, fixture.nodes[0] + 0x18u, 0xFFFFC000u);
    write_le32(fixture.data, fixture.nodes[0] + 0x1Cu, 0x00004000u);
    write_le32(fixture.data, fixture.nodes[0] + 0x20u, 0xFFFF8000u);
    write_le32(fixture.data, fixture.nodes[0] + 0x24u, 0x3F800000u);
    write_le32(fixture.data, fixture.nodes[0] + 0x28u, 0x40000000u);
    write_le32(fixture.data, fixture.nodes[0] + 0x2Cu, 0x40400000u);
    write_le32(fixture.data, fixture.nodes[1], 0x00000000u);
    write_le16(fixture.data, fixture.nodes[1] + 4u, 1u);
    write_le16(fixture.data, fixture.nodes[1] + 6u, 0u);
    write_le16(fixture.data, fixture.nodes[1] + 8u, 0xFFFFu);
    write_le16(fixture.data, fixture.nodes[1] + 10u, 0xFFFFu);
    write_le32(fixture.data, fixture.nodes[1] + 0x0Cu, 0x3F800000u);
    write_le32(fixture.data, fixture.nodes[1] + 0x10u, 0x40000000u);
    write_le32(fixture.data, fixture.nodes[1] + 0x14u, 0x40400000u);
    write_le32(fixture.data, fixture.nodes[1] + 0x18u, 0x00000000u);
    write_le32(fixture.data, fixture.nodes[1] + 0x1Cu, 0x00002000u);
    write_le32(fixture.data, fixture.nodes[1] + 0x20u, 0x00004000u);
    write_le32(fixture.data, fixture.nodes[1] + 0x24u, 0x3F000000u);
    write_le32(fixture.data, fixture.nodes[1] + 0x28u, 0x3F800000u);
    write_le32(fixture.data, fixture.nodes[1] + 0x2Cu, 0x40000000u);
    for (std::size_t word = 0u; word < 16u; ++word) {
        write_le32(
            fixture.data,
            fixture.nodes[0] + 0x30u + word * 4u,
            0xD0000000u + static_cast<std::uint32_t>(word));
        write_le32(
            fixture.data,
            fixture.nodes[1] + 0x30u + word * 4u,
            0xE0000000u + static_cast<std::uint32_t>(word));
    }
    for (std::size_t word = 0u; word < 8u; ++word) {
        write_le32(
            fixture.data,
            fixture.nodes[0] + 0x70u + word * 4u,
            0xF0000000u + static_cast<std::uint32_t>(word));
        write_le32(
            fixture.data,
            fixture.nodes[1] + 0x70u + word * 4u,
            0xF1000000u + static_cast<std::uint32_t>(word));
    }
    fixture.subobject_offset = allocator.allocate(kSubObjectSize);
    const std::size_t mesh_array = allocator.allocate(meshes.size() * kMeshSize);
    const std::size_t subobject_textures = allocator.allocate(4u);
    write_le32(fixture.data, subobject_textures, 0u);
    write_le32(fixture.data, fixture.subobject_offset, 0x101u);
    write_le32(fixture.data, fixture.subobject_offset + 4u, static_cast<std::uint32_t>(meshes.size()));
    add_pointer(fixture.subobject_offset + 8u, mesh_array);
    write_le32(fixture.data, fixture.subobject_offset + 12u, 1u);
    add_pointer(fixture.subobject_offset + 16u, subobject_textures);
    for (std::size_t index = 0u; index < meshes.size(); ++index) {
        const MeshSpec& spec = meshes[index];
        const std::size_t mesh = mesh_array + index * kMeshSize;
        fixture.meshes.push_back(mesh);
        write_le32(fixture.data, mesh, 0x7FC00001u + static_cast<std::uint32_t>(index));
        write_le32(fixture.data, mesh + 4u, 0x7F800000u);
        write_le32(fixture.data, mesh + 8u, 0xFF800000u);
        write_le32(fixture.data, mesh + 12u, 0x3F800000u);
        write_le32(fixture.data, mesh + 16u, static_cast<std::uint32_t>(spec.node_index));
        write_le32(fixture.data, mesh + 20u, static_cast<std::uint32_t>(spec.matrix_index));
        write_le32(fixture.data, mesh + 24u, static_cast<std::uint32_t>(spec.material_index));
        write_le32(fixture.data, mesh + 28u, static_cast<std::uint32_t>(spec.vertex_index));
        write_le32(fixture.data, mesh + 32u, static_cast<std::uint32_t>(spec.primitive_index));
        write_le32(fixture.data, mesh + 36u, spec.reserved);
    }

    write_le32(fixture.data, fixture.object_offset, 0x7FC00001u);
    write_le32(fixture.data, fixture.object_offset + 4u, 0x7F800000u);
    write_le32(fixture.data, fixture.object_offset + 8u, 0xFF800000u);
    write_le32(fixture.data, fixture.object_offset + 12u, 0x3F800000u);
    write_le32(fixture.data, fixture.object_offset + 16u, 3u);
    add_pointer(fixture.object_offset + 20u, material_array);
    write_le32(fixture.data, fixture.object_offset + 24u, static_cast<std::uint32_t>(vertices.size()));
    add_pointer(fixture.object_offset + 28u, vertex_array);
    write_le32(fixture.data, fixture.object_offset + 32u, static_cast<std::uint32_t>(primitives.size()));
    add_pointer(fixture.object_offset + 36u, primitive_array);
    write_le32(fixture.data, fixture.object_offset + 40u, 2u);
    write_le32(fixture.data, fixture.object_offset + 44u, 2u);
    add_pointer(fixture.object_offset + 48u, node_array);
    write_le32(fixture.data, fixture.object_offset + 52u, 16u);
    write_le32(fixture.data, fixture.object_offset + 56u, 1u);
    add_pointer(fixture.object_offset + 60u, fixture.subobject_offset);
    write_le32(fixture.data, fixture.object_offset + 64u, 1u);
    write_le32(fixture.data, fixture.object_offset + 68u, 0x0000002Fu);
    write_le32(fixture.data, fixture.object_offset + 72u, 3u);
    write_le32(fixture.data, fixture.object_offset + 76u, 0x80000000u);
    write_le32(fixture.data, fixture.object_offset + 80u, 0x00000001u);
    write_le32(fixture.data, fixture.object_offset + 84u, 0x7FC00002u);

    const std::size_t nof0_payload_size = 8u + relocations.size() * 4u + 12u;
    const std::size_t nfn0_offset = nof0_offset + 8u + nof0_payload_size;
    const std::size_t nend_offset = nfn0_offset + 8u + 24u;
    fixture.data.resize(nend_offset + 16u, 0u);
    write_tag(fixture.data, 0u, "NZIF");
    write_le32(fixture.data, 4u, 24u);
    write_le32(fixture.data, 8u, 2u);
    write_le32(fixture.data, 12u, static_cast<std::uint32_t>(kDataBase));
    write_le32(fixture.data, 16u, static_cast<std::uint32_t>(nof0_offset - kDataBase));
    write_le32(fixture.data, 20u, static_cast<std::uint32_t>(nof0_offset));
    write_le32(fixture.data, 24u, static_cast<std::uint32_t>(8u + nof0_payload_size));
    write_le32(fixture.data, 28u, 1u);
    write_tag(fixture.data, nof0_offset, "NOF0");
    write_le32(fixture.data, nof0_offset + 4u, static_cast<std::uint32_t>(nof0_payload_size));
    write_le32(fixture.data, nof0_offset + 8u, static_cast<std::uint32_t>(relocations.size()));
    write_le32(fixture.data, nof0_offset + 12u, 0u);
    for (std::size_t index = 0u; index < relocations.size(); ++index) {
        write_le32(fixture.data, nof0_offset + 16u + index * 4u, relocations[index]);
    }
    write_tag(fixture.data, nfn0_offset, "NFN0");
    write_le32(fixture.data, nfn0_offset + 4u, 24u);
    const char file_name[] = "fixture.zno";
    for (std::size_t index = 0u; index < sizeof(file_name); ++index) {
        fixture.data[nfn0_offset + 16u + index] = static_cast<std::uint8_t>(file_name[index]);
    }
    write_tag(fixture.data, nend_offset, "NEND");
    write_le32(fixture.data, nend_offset + 4u, 8u);
    return fixture;
}

Fixture make_locator_fixture() {
    const std::size_t nzob_offset = kDataBase;
    const std::size_t nof0_offset = nzob_offset + 8u + kNzobPayloadSize;
    std::vector<std::uint8_t> data(nof0_offset, 0u);
    Fixture fixture{std::move(data), nzob_offset, nof0_offset, 0u, {}, {}, {}, {}, {}, {}, 0u, {}};
    std::vector<std::uint32_t> relocations;
    const auto add_pointer = [&](std::size_t field, std::size_t target) {
        write_le32(
            fixture.data,
            field,
            static_cast<std::uint32_t>(target - kDataBase));
        relocations.push_back(static_cast<std::uint32_t>(field - kDataBase));
    };

    write_tag(fixture.data, nzob_offset, "NZOB");
    write_le32(fixture.data, nzob_offset + 4u, static_cast<std::uint32_t>(kNzobPayloadSize));
    fixture.object_offset = nzob_offset + 0x108u;
    write_le32(
        fixture.data,
        nzob_offset + 8u,
        static_cast<std::uint32_t>(fixture.object_offset - kDataBase));
    write_le32(fixture.data, nzob_offset + 12u, 3u);
    DataAllocator allocator(
        fixture.data,
        fixture.object_offset + 0x58u,
        nof0_offset);
    const std::size_t material_array = allocator.allocate(8u);
    const std::size_t material_descriptor = allocator.allocate(0x1Cu);
    const std::size_t node_array = allocator.allocate(kNodeSize);
    fixture.nodes.push_back(node_array);
    write_le16(fixture.data, node_array + 4u, 0xFFFFu);
    write_le16(fixture.data, node_array + 6u, 0xFFFFu);
    write_le16(fixture.data, node_array + 8u, 0xFFFFu);
    write_le16(fixture.data, node_array + 10u, 0xFFFFu);
    write_le32(fixture.data, node_array + 0x0Cu, 0x7FC00003u);
    write_le32(fixture.data, node_array + 0x10u, 0x80000000u);
    write_le32(fixture.data, node_array + 0x14u, 0x7F800000u);
    write_le32(fixture.data, material_array, 1u);
    add_pointer(material_array + 4u, material_descriptor);
    write_le32(fixture.data, fixture.object_offset + 16u, 1u);
    add_pointer(fixture.object_offset + 20u, material_array);
    write_le32(fixture.data, fixture.object_offset + 24u, 0u);
    write_le32(fixture.data, fixture.object_offset + 28u, 0u);
    write_le32(fixture.data, fixture.object_offset + 32u, 0u);
    write_le32(fixture.data, fixture.object_offset + 36u, 0u);
    write_le32(fixture.data, fixture.object_offset + 40u, 1u);
    write_le32(fixture.data, fixture.object_offset + 44u, 1u);
    add_pointer(fixture.object_offset + 48u, node_array);
    write_le32(fixture.data, fixture.object_offset + 52u, 0u);
    write_le32(fixture.data, fixture.object_offset + 56u, 0u);
    write_le32(fixture.data, fixture.object_offset + 60u, 0u);
    write_le32(fixture.data, fixture.object_offset + 64u, 0u);
    write_le32(fixture.data, fixture.object_offset + 68u, 0x20u);
    write_le32(fixture.data, fixture.object_offset + 72u, 3u);

    const std::size_t nof0_payload_size = 8u + relocations.size() * 4u;
    const std::size_t nfn0_offset = nof0_offset + 8u + nof0_payload_size;
    const std::size_t nend_offset = nfn0_offset + 8u + 24u;
    fixture.data.resize(nend_offset + 16u, 0u);
    write_tag(fixture.data, 0u, "NZIF");
    write_le32(fixture.data, 4u, 24u);
    write_le32(fixture.data, 8u, 1u);
    write_le32(fixture.data, 12u, static_cast<std::uint32_t>(kDataBase));
    write_le32(fixture.data, 16u, static_cast<std::uint32_t>(nof0_offset - kDataBase));
    write_le32(fixture.data, 20u, static_cast<std::uint32_t>(nof0_offset));
    write_le32(fixture.data, 24u, static_cast<std::uint32_t>(8u + nof0_payload_size));
    write_le32(fixture.data, 28u, 1u);
    write_tag(fixture.data, nof0_offset, "NOF0");
    write_le32(fixture.data, nof0_offset + 4u, static_cast<std::uint32_t>(nof0_payload_size));
    write_le32(fixture.data, nof0_offset + 8u, static_cast<std::uint32_t>(relocations.size()));
    write_le32(fixture.data, nof0_offset + 12u, 0u);
    for (std::size_t index = 0u; index < relocations.size(); ++index) {
        write_le32(fixture.data, nof0_offset + 16u + index * 4u, relocations[index]);
    }
    write_tag(fixture.data, nfn0_offset, "NFN0");
    write_le32(fixture.data, nfn0_offset + 4u, 24u);
    const char file_name[] = "locator.zno";
    for (std::size_t index = 0u; index < sizeof(file_name); ++index) {
        fixture.data[nfn0_offset + 16u + index] = static_cast<std::uint8_t>(file_name[index]);
    }
    write_tag(fixture.data, nend_offset, "NEND");
    write_le32(fixture.data, nend_offset + 4u, 8u);
    return fixture;
}

template <typename Callable>
bool expects_stage_data_error(Callable&& callable, const char* message) {
    try {
        std::forward<Callable>(callable)();
    } catch (const StageDataError&) {
        return true;
    } catch (...) {
        std::fprintf(stderr, "%s (wrong exception type)\n", message);
        return false;
    }

    std::fprintf(stderr, "%s\n", message);
    return false;
}

bool test_decodes_raw_geometry_and_mesh_links() {
    Fixture fixture = make_geometry_fixture();
    const std::vector<std::uint8_t> before = fixture.data;
    const NnModelData model = parse_pc_nn_model(fixture.data.data(), fixture.data.size());
    const std::array<std::uint32_t, 3u> first_node_translation = {{
        read_le32(fixture.data, fixture.nodes[0] + 0x0Cu),
        read_le32(fixture.data, fixture.nodes[0] + 0x10u),
        read_le32(fixture.data, fixture.nodes[0] + 0x14u),
    }};
    const std::array<std::uint32_t, 3u> second_node_translation = {{
        read_le32(fixture.data, fixture.nodes[1] + 0x0Cu),
        read_le32(fixture.data, fixture.nodes[1] + 0x10u),
        read_le32(fixture.data, fixture.nodes[1] + 0x14u),
    }};

    const std::array<std::array<std::uint32_t, 3u>, 7u> expected_layouts = {{
        {{0x00019u, 0x0C2u, 20u}},
        {{0x10019u, 0x1C2u, 28u}},
        {{0x1001Bu, 0x1D2u, 40u}},
        {{0x1701Bu, 0x1DAu, 52u}},
        {{0x2001Bu, 0x2D2u, 48u}},
        {{0x2701Bu, 0x2DAu, 60u}},
        {{0x17403u, 0x111Au, 48u}},
    }};
    if (!check(fixture.data == before, "NN parser mutated its input") ||
        !check(
            model.flags == 0x2Fu && model.version == 3u && model.material_count == 3u &&
                model.node_count == 2u && model.max_node_depth == 2u &&
                model.matrix_palette_count == 16u && model.texture_count == 1u,
            "NN model header did not preserve declared values") ||
        !check(
            model.bounds_bits == std::array<std::uint32_t, 4u>({
                0x7FC00001u, 0x7F800000u, 0xFF800000u, 0x3F800000u}) &&
                model.box_bits == std::array<std::uint32_t, 3u>({
                    0x80000000u, 0x00000001u, 0x7FC00002u}),
            "NN model bounds were converted instead of preserved as raw bits") ||
        !check(
            first_node_translation != second_node_translation &&
                model.first_node_translation_bits.has_value() &&
                *model.first_node_translation_bits == first_node_translation,
            "NN first node translation was not preserved from the first serialized node") ||
        !check(model.vertices.size() == expected_layouts.size(), "NN vertex list count mismatch") ||
        !check(model.primitives.size() == 2u, "NN primitive list count mismatch") ||
        !check(model.sub_objects.size() == 1u, "NN subobject count mismatch") ||
        !check(model.nodes.size() == 2u && model.materials.size() == 3u && model.textures.size() == 1u,
               "NN full node, material, or texture record count mismatch")) {
        return false;
    }

    if (!check(
            model.vertices[0].flags == 0x7u && model.primitives[0].flags == 0x9u,
            "NN pointer-record flags were not preserved")) {
        return false;
    }

    for (std::size_t index = 0u; index < expected_layouts.size(); ++index) {
        const NnVertexData& vertex = model.vertices[index];
        if (!check(
                vertex.format == expected_layouts[index][0] &&
                    vertex.fvf == expected_layouts[index][1] &&
                    vertex.stride == expected_layouts[index][2],
                "NN vertex layout tuple mismatch") ||
            !check(vertex.bytes == fixture.vertex_bytes[index], "NN vertex bytes were not copied exactly")) {
            return false;
        }
    }

    if (!check(
            model.vertices[0].matrix_indices.empty() && model.vertices[1].matrix_indices.empty() &&
                model.vertices[2].matrix_indices.empty() && model.vertices[4].matrix_indices.empty() &&
                model.vertices[3].matrix_indices == std::vector<std::uint32_t>({0u, 2u, 3u}) &&
                model.vertices[5].matrix_indices == std::vector<std::uint32_t>({0u, 1u, 2u, 3u}),
            "NN rigid or weighted matrix subsets mismatch") ||
        !check(
            model.vertices[5].bytes[44u] == fixture.vertex_bytes[5u][44u] &&
                model.vertices[5].bytes[59u] == fixture.vertex_bytes[5u][59u],
            "NN two-UV vertex payload was not preserved") ||
        !check(
            model.primitives[0].mode == 0x4810u &&
                model.primitives[0].strip_lengths == std::vector<std::uint32_t>({3u, 3u}) &&
                model.primitives[0].indices == std::vector<std::uint16_t>({0u, 1u, 2u, 2u, 3u, 1u}),
            "NN strip indices mismatch")) {
        return false;
    }

    const NnVertexAttributes& indexed_skinned = model.vertices[6].attributes[0];
    if (!check(model.vertices[6].attributes.size() == 3u,
               "NN decoded attribute count did not match indexed skinned vertices") ||
        !check(
            indexed_skinned.position_bits == std::array<std::uint32_t, 3u>({{
                0x3F800000u, 0x40000000u, 0x40400000u}}) &&
                indexed_skinned.normal_bits.has_value() && indexed_skinned.texcoord_bits.has_value() &&
                indexed_skinned.weight_bits == std::vector<std::uint32_t>({
                    0x3E800000u, 0x3F000000u, 0x3E800000u}) &&
                indexed_skinned.blend_indices.has_value() &&
                *indexed_skinned.blend_indices == std::array<std::uint8_t, 4u>({{0u, 3u, 2u, 1u}}) &&
                model.vertices[6].matrix_indices.size() == 16u &&
                model.vertices[6].matrix_indices[15] == 15u,
            "NN indexed skin attributes were not decoded from their serialized offsets")) {
        return false;
    }

    const NnVertexAttributes& colored_vertex = model.vertices[2].attributes[0];
    if (!check(
            colored_vertex.diffuse_bgra.has_value() && colored_vertex.specular_bgra.has_value() &&
                *colored_vertex.diffuse_bgra == std::array<std::uint8_t, 4u>({{
                    fixture.vertex_bytes[2][24u], fixture.vertex_bytes[2][25u],
                    fixture.vertex_bytes[2][26u], fixture.vertex_bytes[2][27u]}}) &&
                *colored_vertex.specular_bgra == std::array<std::uint8_t, 4u>({{
                    fixture.vertex_bytes[2][28u], fixture.vertex_bytes[2][29u],
                    fixture.vertex_bytes[2][30u], fixture.vertex_bytes[2][31u]}}),
            "NN diffuse or specular colors were not decoded from their serialized offsets")) {
        return false;
    }

    if (!check(
            model.nodes[0].flags == 0x00000008u && model.nodes[0].matrix_index == 0 &&
                model.nodes[0].parent_index == -1 && model.nodes[0].child_index == 1 &&
                model.nodes[0].sibling_index == -1 &&
                model.nodes[0].rotation_a16 == std::array<std::int32_t, 3u>({{-16384, 16384, -32768}}) &&
                model.nodes[0].scale_bits == std::array<std::uint32_t, 3u>({{
                    0x3F800000u, 0x40000000u, 0x40400000u}}) &&
                model.nodes[0].inverse_bind_bits[0] == 0xD0000000u &&
                model.nodes[0].inverse_bind_bits[15] == 0xD000000Fu &&
                model.nodes[0].opaque_bits[7] == 0xF0000007u &&
                model.nodes[1].matrix_index == 1 && model.nodes[1].parent_index == 0 &&
                model.nodes[1].child_index == -1 && model.nodes[1].sibling_index == -1,
            "NN full node hierarchy or inverse-bind record mismatch")) {
        return false;
    }

    if (!check(
            model.materials[0].pointer_flags == 0x10000000u &&
                model.materials[0].descriptor_bits[0] == 0x00001102u &&
                model.materials[0].descriptor_bits[1] == 0xF00DF00Du &&
                model.materials[0].descriptor_bits[4] == 0x12345678u &&
                model.materials[0].descriptor_bits[5] == 2u &&
                !model.materials[0].user_profile.has_value() &&
                model.materials[0].color_terms_bits ==
                    std::vector<std::array<std::uint32_t, 4u>>(
                        kMaterialColorTerms.begin(),
                        kMaterialColorTerms.end()) &&
                model.materials[0].color_flags == 2u &&
                model.materials[0].shininess_bits == kMaterialShininessBits &&
                model.materials[0].specular_intensity_bits == kMaterialSpecularIntensityBits &&
                model.materials[0].render_state.has_value() &&
                model.materials[0].render_state->words.size() == 7u &&
                model.materials[0].render_state->words[0] == 0xA0000000u &&
                model.materials[0].render_state->words[6] == 0xA0000006u &&
                model.materials[0].stages.size() == 2u &&
                model.materials[0].stages[0].flags == 0x60000002u &&
                model.materials[0].stages[0].texture_index == 0 &&
                model.materials[0].stages[0].raw_words.size() == 16u &&
                model.materials[0].stages[0].raw_words[15] == 0xB000000Fu &&
                model.materials[0].stages[1].flags == 0x20000004u &&
                model.materials[0].stages[1].raw_words[15] == 0xC000000Fu &&
                model.materials[1].pointer_flags == 0x30000000u &&
                model.materials[1].color_terms_bits.empty() && model.materials[1].color_flags == 0u &&
                model.materials[1].shininess_bits == 0u && model.materials[1].specular_intensity_bits == 0u &&
                model.materials[1].user_profile.has_value() && *model.materials[1].user_profile == 5u &&
                model.materials[1].stages.size() == 1u &&
                model.materials[1].stages[0].flags == 0x60000008u &&
                model.materials[1].stages[0].raw_words ==
                    std::vector<std::uint32_t>(
                        kExtendedMaterialStageWords.begin(), kExtendedMaterialStageWords.end()) &&
                model.materials[2].pointer_flags == 0x20000000u &&
                !model.materials[2].user_profile.has_value() &&
                model.materials[2].stages.size() == 1u &&
                model.materials[2].stages[0].flags == 0x60000008u &&
                model.materials[2].stages[0].raw_words.size() == 8u &&
                model.materials[2].stages[0].raw_words[7] == 0xD0000007u &&
                model.textures[0].name == "fixture.dds" &&
                model.textures[0].raw_words[0] == 0x00010004u,
            "NN material, raw state, stage, or texture-name record mismatch")) {
        return false;
    }

    const NnSubObjectData& sub_object = model.sub_objects[0];
    return check(
               sub_object.reserved == std::array<std::uint32_t, 2u>({
                   1u,
                   read_le32(fixture.data, fixture.subobject_offset + 16u)}),
               "NN subobject opaque fields mismatch") &&
           check(
               sub_object.flags == 0x101u && sub_object.meshes.size() == 3u,
               "NN subobject flags or mesh count mismatch") &&
           check(
               sub_object.meshes[0].vertex_index == 1u && sub_object.meshes[0].primitive_index == 0u &&
                   sub_object.meshes[1].vertex_index == 1u && sub_object.meshes[1].primitive_index == 0u &&
                   sub_object.meshes[2].vertex_index == 0u && sub_object.meshes[2].primitive_index == 1u,
               "NN mesh links were paired by position or did not preserve sharing") &&
           check(
               sub_object.meshes[0].bounds_bits == std::array<std::uint32_t, 4u>({
                   0x7FC00001u, 0x7F800000u, 0xFF800000u, 0x3F800000u}) &&
                   sub_object.meshes[1].reserved == 0xB0B0B0B0u &&
                   sub_object.meshes[2].matrix_index == -1,
               "NN mesh raw bounds, reserved word, or signed matrix link mismatch");
}

bool test_accepts_locator_and_owns_output() {
    Fixture locator = make_locator_fixture();
    const NnModelData locator_model = parse_pc_nn_model(locator.data.data(), locator.data.size());
    const std::array<std::uint32_t, 3u> locator_translation = {{
        read_le32(locator.data, locator.nodes[0] + 0x0Cu),
        read_le32(locator.data, locator.nodes[0] + 0x10u),
        read_le32(locator.data, locator.nodes[0] + 0x14u),
    }};
    if (!check(
            locator_model.flags == 0x20u && locator_model.material_count == 1u &&
                locator_model.node_count == 1u && locator_model.vertices.empty() &&
                locator_model.primitives.empty() && locator_model.sub_objects.empty() &&
                locator_model.first_node_translation_bits.has_value() &&
                *locator_model.first_node_translation_bits == locator_translation,
            "NN locator was not accepted as an empty-geometry model")) {
        return false;
    }

    Fixture geometry = make_geometry_fixture();
    const NnModelData model = parse_pc_nn_model(geometry.data.data(), geometry.data.size());
    const std::vector<std::uint8_t> copied = model.vertices[0].bytes;
    const auto copied_translation = model.first_node_translation_bits;
    geometry.data[geometry.vertex_buffers[0]] ^= 0xFFu;
    geometry.data[geometry.nodes[0] + 0x0Cu] ^= 0xFFu;
    return check(model.vertices[0].bytes == copied, "NN output retained input storage instead of owning bytes") &&
           check(
               model.first_node_translation_bits == copied_translation,
               "NN first node translation retained input storage instead of owning bits");
}

bool test_preserves_absent_first_node_translation() {
    Fixture zero_nodes = make_locator_fixture();
    write_le32(zero_nodes.data, zero_nodes.object_offset + 0x28u, 0u);
    write_le32(zero_nodes.data, zero_nodes.object_offset + 0x2Cu, 0u);
    write_le32(zero_nodes.data, zero_nodes.object_offset + 0x30u, 0u);
    const NnModelData model = parse_pc_nn_model(zero_nodes.data.data(), zero_nodes.data.size());
    return check(
        model.node_count == 0u && !model.first_node_translation_bits.has_value(),
        "NN zero-node model did not preserve absent first-node translation");
}

bool test_accepts_observed_weighted_subset_bounds() {
    Fixture one_matrix_subset = make_geometry_fixture();
    write_le32(one_matrix_subset.data, one_matrix_subset.vertex_descriptors[6] + 20u, 1u);
    for (std::size_t vertex = 0u; vertex < 3u; ++vertex) {
        const std::size_t blend = one_matrix_subset.vertex_buffers[6] + vertex * 48u + 24u;
        one_matrix_subset.data[blend] = 0u;
        one_matrix_subset.data[blend + 1u] = 0u;
        one_matrix_subset.data[blend + 2u] = 0u;
        one_matrix_subset.data[blend + 3u] = 0u;
    }
    const NnModelData model = parse_pc_nn_model(
        one_matrix_subset.data.data(),
        one_matrix_subset.data.size());
    return check(
        model.vertices[6].matrix_indices == std::vector<std::uint32_t>({0u}) &&
            model.vertices[6].attributes[2].blend_indices.has_value() &&
            *model.vertices[6].attributes[2].blend_indices ==
                std::array<std::uint8_t, 4u>({{0u, 0u, 0u, 0u}}),
        "NN one-entry weighted subset was not accepted and preserved");
}

bool test_decodes_unindexed_weighted_normal_texture_vertices() {
    Fixture fixture = make_geometry_fixture();
    const std::size_t descriptor = fixture.vertex_descriptors[3u];
    write_le32(fixture.data, descriptor, 0x17003u);
    write_le32(fixture.data, descriptor + 4u, 0x11Au);
    write_le32(fixture.data, descriptor + 8u, 44u);
    for (std::size_t vertex = 0u; vertex < 4u; ++vertex) {
        for (std::size_t word = 0u; word < 11u; ++word) {
            write_le32(fixture.data, fixture.vertex_buffers[3u] + vertex * 44u + word * 4u,
                0x3f000000u + static_cast<std::uint32_t>(vertex * 256u + word));
        }
    }
    const auto before = fixture.data;
    const auto model = parse_pc_nn_model(fixture.data.data(), fixture.data.size());
    const auto& vertex_list = model.vertices[3u];
    if (!check(vertex_list.format == 0x17003u && vertex_list.fvf == 0x11Au &&
                   vertex_list.stride == 44u && vertex_list.bytes.size() == 176u &&
                   vertex_list.matrix_indices == std::vector<std::uint32_t>({0u, 2u, 3u}),
               "NN unindexed weighted vertex descriptor changed")) {
        return false;
    }
    for (std::size_t vertex = 0u; vertex < 4u; ++vertex) {
        const auto& attributes = vertex_list.attributes[vertex];
        const std::uint32_t base = 0x3f000000u + static_cast<std::uint32_t>(vertex * 256u);
        if (!check(attributes.position_bits == std::array<std::uint32_t, 3u>({base, base + 1u, base + 2u}) &&
                       attributes.weight_bits == std::vector<std::uint32_t>({base + 3u, base + 4u, base + 5u}) &&
                       attributes.normal_bits == std::array<std::uint32_t, 3u>({base + 6u, base + 7u, base + 8u}) &&
                       attributes.texcoord_bits == std::array<std::uint32_t, 2u>({base + 9u, base + 10u}) &&
                       !attributes.diffuse_bgra && !attributes.specular_bgra && !attributes.blend_indices,
                   "NN unindexed weighted vertex attributes overlap or use wrong offsets")) {
            return false;
        }
    }
    if (!check(fixture.data == before, "NN unindexed weighted vertex parsing modified input")) {
        return false;
    }
    write_le32(fixture.data, descriptor + 8u, 48u);
    return expects_stage_data_error(
        [&]() { parse_pc_nn_model(fixture.data.data(), fixture.data.size()); },
        "NN unindexed weighted format accepted an indexed stride");
}

bool test_reads_fixed_material_records_without_flag_counts() {
    const std::array<std::uint32_t, 3u> flags = {{0u, 2u, 0x80000002u}};
    for (const std::uint32_t flag : flags) {
        Fixture fixture = make_geometry_fixture();
        write_le32(fixture.data, material_color_offset(fixture, 0u), flag);
        const std::vector<std::uint8_t> before = fixture.data;
        const NnModelData model = parse_pc_nn_model(fixture.data.data(), fixture.data.size());
        if (!check(fixture.data == before, "fixed material-record parsing mutated its input") ||
            !check(
                model.materials[0].color_flags == flag &&
                    model.materials[0].color_terms_bits ==
                        std::vector<std::array<std::uint32_t, 4u>>(
                            kMaterialColorTerms.begin(),
                            kMaterialColorTerms.end()) &&
                    model.materials[0].shininess_bits == kMaterialShininessBits &&
                    model.materials[0].specular_intensity_bits == kMaterialSpecularIntensityBits,
                "material color flags changed fixed record decoding")) {
            return false;
        }
    }

    Fixture short_color = make_geometry_fixture();
    const std::size_t short_color_offset = short_color.nof0_offset - (kMaterialColorRecordSize - 1u);
    write_le32(
        short_color.data,
        material_descriptor_offset(short_color, 0u) + 8u,
        static_cast<std::uint32_t>(short_color_offset - kDataBase));
    if (!expects_stage_data_error(
            [&]() { parse_pc_nn_model(short_color.data.data(), short_color.data.size()); },
            "75-byte material color record was accepted")) {
        return false;
    }

    Fixture short_state = make_geometry_fixture();
    const std::size_t short_state_offset = short_state.nof0_offset - (kMaterialStateRecordSize - 1u);
    write_le32(
        short_state.data,
        material_descriptor_offset(short_state, 0u) + 12u,
        static_cast<std::uint32_t>(short_state_offset - kDataBase));
    return expects_stage_data_error(
        [&]() { parse_pc_nn_model(short_state.data.data(), short_state.data.size()); },
        "27-byte material state record was accepted");
}

bool test_rejects_malformed_data() {
    Fixture base = make_geometry_fixture();
    if (!expects_stage_data_error(
            [&]() { parse_pc_nn_model(nullptr, base.data.size()); },
            "null NN data was accepted") ||
        !expects_stage_data_error(
            [&]() { parse_pc_nn_model(base.data.data(), base.data.size() - 1u); },
            "truncated NN data was accepted")) {
        return false;
    }

    Fixture bad_nzif_version = make_geometry_fixture();
    write_le32(bad_nzif_version.data, 28u, 2u);
    if (!expects_stage_data_error(
            [&]() { parse_pc_nn_model(bad_nzif_version.data.data(), bad_nzif_version.data.size()); },
            "unsupported NZIF version was accepted")) {
        return false;
    }

    Fixture bad_data_base = make_geometry_fixture();
    write_le32(bad_data_base.data, 12u, 0x24u);
    if (!expects_stage_data_error(
            [&]() { parse_pc_nn_model(bad_data_base.data.data(), bad_data_base.data.size()); },
            "unsupported NZIF data base was accepted")) {
        return false;
    }

    Fixture bad_data_extent = make_geometry_fixture();
    write_le32(bad_data_extent.data, 16u, read_le32(bad_data_extent.data, 16u) + 4u);
    if (!expects_stage_data_error(
            [&]() { parse_pc_nn_model(bad_data_extent.data.data(), bad_data_extent.data.size()); },
            "NZIF data extent mismatch was accepted")) {
        return false;
    }

    Fixture bad_nof0_extent = make_geometry_fixture();
    write_le32(bad_nof0_extent.data, 24u, read_le32(bad_nof0_extent.data, 24u) + 4u);
    if (!expects_stage_data_error(
            [&]() { parse_pc_nn_model(bad_nof0_extent.data.data(), bad_nof0_extent.data.size()); },
            "NZIF NOF0 extent mismatch was accepted")) {
        return false;
    }

    Fixture bad_nof0_table = make_geometry_fixture();
    write_le32(bad_nof0_table.data, bad_nof0_table.nof0_offset + 8u, 0x7FFFFFFFu);
    if (!expects_stage_data_error(
            [&]() { parse_pc_nn_model(bad_nof0_table.data.data(), bad_nof0_table.data.size()); },
            "oversized NOF0 table was accepted")) {
        return false;
    }

    Fixture bad_relocation = make_geometry_fixture();
    write_le32(bad_relocation.data, bad_relocation.nof0_offset + 16u, 0xFFFFFFFCu);
    if (!expects_stage_data_error(
            [&]() { parse_pc_nn_model(bad_relocation.data.data(), bad_relocation.data.size()); },
            "out-of-range NOF0 relocation was accepted")) {
        return false;
    }

    Fixture duplicate_relocation = make_geometry_fixture();
    write_le32(
        duplicate_relocation.data,
        duplicate_relocation.nof0_offset + 20u,
        read_le32(duplicate_relocation.data, duplicate_relocation.nof0_offset + 16u));
    if (!expects_stage_data_error(
            [&]() { parse_pc_nn_model(duplicate_relocation.data.data(), duplicate_relocation.data.size()); },
            "repeated NOF0 relocation location was accepted")) {
        return false;
    }

    Fixture negative_vertex_count = make_geometry_fixture();
    write_le32(negative_vertex_count.data, negative_vertex_count.object_offset + 24u, 0xFFFFFFFFu);
    if (!expects_stage_data_error(
            [&]() { parse_pc_nn_model(negative_vertex_count.data.data(), negative_vertex_count.data.size()); },
            "negative vertex count was accepted")) {
        return false;
    }

    Fixture overflowing_node_count = make_geometry_fixture();
    write_le32(overflowing_node_count.data, overflowing_node_count.object_offset + 40u, 0x7FFFFFFFu);
    if (!expects_stage_data_error(
            [&]() { parse_pc_nn_model(overflowing_node_count.data.data(), overflowing_node_count.data.size()); },
            "oversized node array was accepted")) {
        return false;
    }

    Fixture short_node_array = make_geometry_fixture();
    write_le32(
        short_node_array.data,
        short_node_array.object_offset + 0x30u,
        static_cast<std::uint32_t>(
            short_node_array.nof0_offset - kDataBase - (kNodeSize - 4u)));
    if (!expects_stage_data_error(
            [&]() { parse_pc_nn_model(short_node_array.data.data(), short_node_array.data.size()); },
            "truncated node array extent was accepted")) {
        return false;
    }

    Fixture bad_node_parent = make_geometry_fixture();
    write_le16(bad_node_parent.data, bad_node_parent.nodes[1] + 6u, 2u);
    if (!expects_stage_data_error(
            [&]() { parse_pc_nn_model(bad_node_parent.data.data(), bad_node_parent.data.size()); },
            "node hierarchy link outside the node array was accepted")) {
        return false;
    }

    Fixture excessive_material_stages = make_geometry_fixture();
    const std::size_t material0 = material_descriptor_offset(excessive_material_stages, 0u);
    write_le32(excessive_material_stages.data, material0 + 20u, 17u);
    if (!expects_stage_data_error(
            [&]() {
                parse_pc_nn_model(
                    excessive_material_stages.data.data(),
                    excessive_material_stages.data.size());
            },
            "material stage count above the observed maximum was accepted")) {
        return false;
    }

    Fixture missing_material_stage_relocation = make_geometry_fixture();
    const std::size_t missing_stage_descriptor = material_descriptor_offset(
        missing_material_stage_relocation,
        0u);
    if (!check(
            replace_relocation_location(
                missing_material_stage_relocation,
                static_cast<std::uint32_t>(missing_stage_descriptor + 24u - kDataBase),
                static_cast<std::uint32_t>(missing_stage_descriptor + 20u - kDataBase)),
            "synthetic NN fixture omitted a material-stage relocation") ||
        !expects_stage_data_error(
            [&]() {
                parse_pc_nn_model(
                    missing_material_stage_relocation.data.data(),
                    missing_material_stage_relocation.data.size());
            },
            "material stage pointer without NOF0 coverage was accepted")) {
        return false;
    }

    Fixture short_extended_descriptor = make_geometry_fixture();
    const std::size_t short_extended_material_array = kDataBase + static_cast<std::size_t>(
        read_le32(short_extended_descriptor.data, short_extended_descriptor.object_offset + 20u));
    write_le32(
        short_extended_descriptor.data,
        short_extended_material_array + 12u,
        static_cast<std::uint32_t>(short_extended_descriptor.nof0_offset - kDataBase - 28u));
    if (!expects_stage_data_error(
            [&]() {
                parse_pc_nn_model(
                    short_extended_descriptor.data.data(), short_extended_descriptor.data.size());
            },
            "28-byte extended material descriptor was accepted")) {
        return false;
    }

    Fixture short_extended_stage = make_geometry_fixture();
    const std::size_t extended_material = material_descriptor_offset(short_extended_stage, 1u);
    const std::size_t short_stage_offset = short_extended_stage.nof0_offset - 32u;
    write_le32(
        short_extended_stage.data,
        extended_material + 24u,
        static_cast<std::uint32_t>(short_stage_offset - kDataBase));
    write_le32(short_extended_stage.data, short_stage_offset, 0x60000008u);
    write_le32(short_extended_stage.data, short_stage_offset + 4u, 0u);
    if (!expects_stage_data_error(
            [&]() {
                parse_pc_nn_model(short_extended_stage.data.data(), short_extended_stage.data.size());
            },
            "32-byte extended material stage was accepted")) {
        return false;
    }

    Fixture unterminated_texture_name = make_geometry_fixture();
    std::fill(
        unterminated_texture_name.data.begin() + static_cast<std::ptrdiff_t>(kDataBase + 0x70u),
        unterminated_texture_name.data.begin() + static_cast<std::ptrdiff_t>(
            kDataBase + 8u + kNztlPayloadSize),
        static_cast<std::uint8_t>(0x41u));
    if (!expects_stage_data_error(
            [&]() {
                parse_pc_nn_model(
                    unterminated_texture_name.data.data(),
                    unterminated_texture_name.data.size());
            },
            "unterminated NZTL filename was accepted")) {
        return false;
    }

    Fixture bad_vertex_array_pointer = make_geometry_fixture();
    write_le32(
        bad_vertex_array_pointer.data,
        bad_vertex_array_pointer.object_offset + 28u,
        static_cast<std::uint32_t>(bad_vertex_array_pointer.nof0_offset - kDataBase));
    if (!expects_stage_data_error(
            [&]() { parse_pc_nn_model(bad_vertex_array_pointer.data.data(), bad_vertex_array_pointer.data.size()); },
            "vertex pointer array outside data region was accepted")) {
        return false;
    }

    Fixture zero_vertex_array_pointer = make_geometry_fixture();
    write_le32(zero_vertex_array_pointer.data, zero_vertex_array_pointer.object_offset + 28u, 0u);
    if (!expects_stage_data_error(
            [&]() { parse_pc_nn_model(zero_vertex_array_pointer.data.data(), zero_vertex_array_pointer.data.size()); },
            "required zero vertex pointer was accepted")) {
        return false;
    }

    Fixture overlapping_vertex_output = make_geometry_fixture();
    const std::size_t vertex_array = kDataBase + static_cast<std::size_t>(
        read_le32(overlapping_vertex_output.data, overlapping_vertex_output.object_offset + 28u));
    const std::uint32_t shared_descriptor_relative = read_le32(overlapping_vertex_output.data, vertex_array + 4u);
    for (std::size_t index = 1u; index < overlapping_vertex_output.vertex_descriptors.size(); ++index) {
        write_le32(
            overlapping_vertex_output.data,
            vertex_array + index * 8u + 4u,
            shared_descriptor_relative);
    }
    write_le32(overlapping_vertex_output.data, overlapping_vertex_output.vertex_descriptors[0] + 12u, 400u);
    write_le32(
        overlapping_vertex_output.data,
        overlapping_vertex_output.vertex_descriptors[0] + 16u,
        static_cast<std::uint32_t>(overlapping_vertex_output.object_offset - kDataBase));
    if (!expects_stage_data_error(
            [&]() {
                parse_pc_nn_model(
                    overlapping_vertex_output.data.data(),
                    overlapping_vertex_output.data.size());
            },
            "overlapping vertex descriptors bypassed the aggregate output budget")) {
        return false;
    }

    Fixture missing_vertex_buffer_relocation = make_geometry_fixture();
    const std::uint32_t vertex_buffer_location = static_cast<std::uint32_t>(
        missing_vertex_buffer_relocation.vertex_descriptors[0] + 16u - kDataBase);
    const std::uint32_t rigid_count_location = static_cast<std::uint32_t>(
        missing_vertex_buffer_relocation.vertex_descriptors[0] + 20u - kDataBase);
    if (!check(
            replace_relocation_location(
                missing_vertex_buffer_relocation,
                vertex_buffer_location,
                rigid_count_location),
            "synthetic NN fixture omitted a vertex-buffer relocation") ||
        !expects_stage_data_error(
            [&]() {
                parse_pc_nn_model(
                    missing_vertex_buffer_relocation.data.data(),
                    missing_vertex_buffer_relocation.data.size());
            },
            "vertex buffer without required NOF0 coverage was accepted")) {
        return false;
    }

    Fixture bad_object_version = make_geometry_fixture();
    write_le32(bad_object_version.data, bad_object_version.object_offset + 72u, 2u);
    if (!expects_stage_data_error(
            [&]() { parse_pc_nn_model(bad_object_version.data.data(), bad_object_version.data.size()); },
            "unsupported object version was accepted")) {
        return false;
    }

    Fixture bad_chunk_version = make_geometry_fixture();
    write_le32(bad_chunk_version.data, bad_chunk_version.nzob_offset + 12u, 2u);
    if (!expects_stage_data_error(
            [&]() { parse_pc_nn_model(bad_chunk_version.data.data(), bad_chunk_version.data.size()); },
            "unsupported NZOB data header version was accepted")) {
        return false;
    }

    Fixture bad_format = make_geometry_fixture();
    write_le32(bad_format.data, bad_format.vertex_descriptors[0], 0xDEADBEEFu);
    if (!expects_stage_data_error(
            [&]() { parse_pc_nn_model(bad_format.data.data(), bad_format.data.size()); },
            "unsupported vertex format was accepted")) {
        return false;
    }

    Fixture bad_fvf = make_geometry_fixture();
    write_le32(bad_fvf.data, bad_fvf.vertex_descriptors[0] + 4u, 0x0C3u);
    if (!expects_stage_data_error(
            [&]() { parse_pc_nn_model(bad_fvf.data.data(), bad_fvf.data.size()); },
            "unsupported vertex FVF was accepted")) {
        return false;
    }

    Fixture bad_stride = make_geometry_fixture();
    write_le32(bad_stride.data, bad_stride.vertex_descriptors[0] + 8u, 24u);
    if (!expects_stage_data_error(
            [&]() { parse_pc_nn_model(bad_stride.data.data(), bad_stride.data.size()); },
            "unsupported vertex stride was accepted")) {
        return false;
    }

    Fixture negative_matrix_count = make_geometry_fixture();
    write_le32(negative_matrix_count.data, negative_matrix_count.vertex_descriptors[3] + 20u, 0xFFFFFFFFu);
    if (!expects_stage_data_error(
            [&]() { parse_pc_nn_model(negative_matrix_count.data.data(), negative_matrix_count.data.size()); },
            "negative matrix subset count was accepted")) {
        return false;
    }

    Fixture unsupported_weighted_matrix_count = make_geometry_fixture();
    write_le32(unsupported_weighted_matrix_count.data, unsupported_weighted_matrix_count.vertex_descriptors[5] + 20u, 17u);
    if (!expects_stage_data_error(
            [&]() {
                parse_pc_nn_model(
                    unsupported_weighted_matrix_count.data.data(),
                    unsupported_weighted_matrix_count.data.size());
            },
            "weighted matrix subset count above the observed maximum was accepted")) {
        return false;
    }

    Fixture weighted_without_relocation = make_geometry_fixture();
    const std::uint32_t weighted_pointer_location = static_cast<std::uint32_t>(
        weighted_without_relocation.vertex_descriptors[5] + 24u - kDataBase);
    const std::uint32_t weighted_count_location = static_cast<std::uint32_t>(
        weighted_without_relocation.vertex_descriptors[5] + 20u - kDataBase);
    if (!check(
            replace_relocation_location(
                weighted_without_relocation,
                weighted_pointer_location,
                weighted_count_location),
            "synthetic NN fixture omitted a weighted matrix relocation") ||
        !expects_stage_data_error(
            [&]() {
                parse_pc_nn_model(
                    weighted_without_relocation.data.data(),
                    weighted_without_relocation.data.size());
            },
            "weighted matrix subset without relocation coverage was accepted")) {
        return false;
    }

    Fixture bad_blend_index = make_geometry_fixture();
    bad_blend_index.data[bad_blend_index.vertex_buffers[6] + 24u] = 16u;
    if (!expects_stage_data_error(
            [&]() { parse_pc_nn_model(bad_blend_index.data.data(), bad_blend_index.data.size()); },
            "blend index outside a weighted subset was accepted")) {
        return false;
    }

    Fixture rigid_matrix_pair = make_geometry_fixture();
    write_le32(rigid_matrix_pair.data, rigid_matrix_pair.vertex_descriptors[0] + 20u, 3u);
    if (!expects_stage_data_error(
            [&]() { parse_pc_nn_model(rigid_matrix_pair.data.data(), rigid_matrix_pair.data.size()); },
            "rigid vertex descriptor with a matrix subset pair was accepted")) {
        return false;
    }

    Fixture out_of_range_matrix_index = make_geometry_fixture();
    const std::uint32_t matrix_relative = read_le32(
        out_of_range_matrix_index.data,
        out_of_range_matrix_index.vertex_descriptors[5] + 24u);
    write_le32(
        out_of_range_matrix_index.data,
        kDataBase + static_cast<std::size_t>(matrix_relative),
        16u);
    if (!expects_stage_data_error(
            [&]() {
                parse_pc_nn_model(
                    out_of_range_matrix_index.data.data(),
                    out_of_range_matrix_index.data.size());
            },
            "matrix subset index outside the palette was accepted")) {
        return false;
    }

    Fixture unknown_mode = make_geometry_fixture();
    write_le32(unknown_mode.data, unknown_mode.primitive_descriptors[0], 0x00000004u);
    if (!expects_stage_data_error(
            [&]() { parse_pc_nn_model(unknown_mode.data.data(), unknown_mode.data.size()); },
            "unsupported primitive mode was accepted")) {
        return false;
    }

    Fixture mismatched_strip_total = make_geometry_fixture();
    write_le32(mismatched_strip_total.data, mismatched_strip_total.primitive_descriptors[0] + 4u, 5u);
    if (!expects_stage_data_error(
            [&]() { parse_pc_nn_model(mismatched_strip_total.data.data(), mismatched_strip_total.data.size()); },
            "primitive strip total mismatch was accepted")) {
        return false;
    }

    Fixture zero_strips = make_geometry_fixture();
    write_le32(zero_strips.data, zero_strips.primitive_descriptors[0] + 8u, 0u);
    if (!expects_stage_data_error(
            [&]() { parse_pc_nn_model(zero_strips.data.data(), zero_strips.data.size()); },
            "primitive descriptor without a strip was accepted")) {
        return false;
    }

    Fixture negative_mesh_count = make_geometry_fixture();
    write_le32(negative_mesh_count.data, negative_mesh_count.subobject_offset + 4u, 0xFFFFFFFFu);
    if (!expects_stage_data_error(
            [&]() { parse_pc_nn_model(negative_mesh_count.data.data(), negative_mesh_count.data.size()); },
            "negative mesh count was accepted")) {
        return false;
    }

    Fixture bad_mesh_reference = make_geometry_fixture();
    write_le32(bad_mesh_reference.data, bad_mesh_reference.meshes[0] + 28u, 6u);
    if (!expects_stage_data_error(
            [&]() { parse_pc_nn_model(bad_mesh_reference.data.data(), bad_mesh_reference.data.size()); },
            "out-of-range mesh vertex-list reference was accepted")) {
        return false;
    }

    Fixture shared_primitive_small_vertex_list = make_geometry_fixture();
    write_le32(shared_primitive_small_vertex_list.data, shared_primitive_small_vertex_list.meshes[1] + 28u, 0u);
    if (!expects_stage_data_error(
            [&]() {
                parse_pc_nn_model(
                    shared_primitive_small_vertex_list.data.data(),
                    shared_primitive_small_vertex_list.data.size());
            },
            "shared primitive index outside a later selected vertex list was accepted")) {
        return false;
    }

    Fixture bad_mesh_index = make_geometry_fixture();
    write_le16(bad_mesh_index.data, bad_mesh_index.primitive_indices[0], 4u);
    return expects_stage_data_error(
        [&]() { parse_pc_nn_model(bad_mesh_index.data.data(), bad_mesh_index.data.size()); },
        "mesh-bound index outside the selected vertex list was accepted");
}

}

int main() {
    if (!test_decodes_raw_geometry_and_mesh_links() ||
        !test_accepts_locator_and_owns_output() ||
        !test_preserves_absent_first_node_translation() ||
        !test_accepts_observed_weighted_subset_bounds() ||
        !test_decodes_unindexed_weighted_normal_texture_vertices() ||
        !test_reads_fixed_material_records_without_flag_counts() ||
        !test_rejects_malformed_data()) {
        return 1;
    }
    return 0;
}
