#include "nn_model_data.h"
#include "terrain_data.h"

#include <cstdint>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <variant>
#include <vector>

namespace {

constexpr std::size_t max_resource_bytes = 64u * 1024u * 1024u;
using Resource = std::variant<std::monostate, TerrainTable, NnModelData>;

std::vector<std::uint8_t> load_archive(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary | std::ios::ate);
    if (!input) {
        throw StageDataError("cannot open archive");
    }
    const auto end = input.tellg();
    if (end < 0 || static_cast<std::uint64_t>(end) > max_resource_bytes) {
        throw StageDataError("archive size is invalid or exceeds the 64 MiB inspection limit");
    }
    std::vector<std::uint8_t> bytes(static_cast<std::size_t>(end));
    input.seekg(0, std::ios::beg);
    if (!bytes.empty()) {
        input.read(reinterpret_cast<char*>(bytes.data()),
                   static_cast<std::streamsize>(bytes.size()));
    }
    if (!input) {
        throw StageDataError("cannot read complete archive");
    }
    return bytes;
}

std::string extension(const std::string& name) {
    const auto dot = name.find_last_of('.');
    if (dot == std::string::npos) {
        return {};
    }
    std::string result = name.substr(dot);
    for (char& c : result) {
        if (c >= 'a' && c <= 'z') {
            c = static_cast<char>(c - 'a' + 'A');
        }
    }
    return result;
}

void write_string(const std::string& value) {
    constexpr char hex[] = "0123456789abcdef";
    std::cout << '"';
    for (unsigned char c : value) {
        if (c == '"' || c == '\\') {
            std::cout << '\\' << static_cast<char>(c);
        } else if (c < 0x20 || c >= 0x7f) {
            std::cout << "\\u00" << hex[c >> 4] << hex[c & 0x0f];
        } else {
            std::cout << static_cast<char>(c);
        }
    }
    std::cout << '"';
}

template <typename Container>
void write_numbers(const Container& values) {
    std::cout << '[';
    bool first = true;
    for (const auto value : values) {
        if (!first) {
            std::cout << ',';
        }
        first = false;
        std::cout << +value;
    }
    std::cout << ']';
}

void write_hex(const std::vector<std::uint8_t>& bytes) {
    constexpr char hex[] = "0123456789abcdef";
    std::cout << '"';
    for (const auto value : bytes) {
        const char pair[] = {hex[value >> 4], hex[value & 0x0f]};
        std::cout.write(pair, 2);
    }
    std::cout << '"';
}

void write_terrain(const TerrainTable& table, bool include_data) {
    const char* kind = table.kind == TerrainRecordKind::Height ? "height" :
                       table.kind == TerrainRecordKind::Angle ? "angle" : "attribute";
    std::cout << "{\"kind\":\"" << kind << "\",\"record_count\":" << table.record_count
              << ",\"record_size\":" << terrain_record_size(table.kind)
              << ",\"record_bytes\":" << table.records.size()
              << ",\"chip_count\":" << table.chip_records.size();
    if (include_data) {
        std::cout << ",\"records_hex\":";
        write_hex(table.records);
        std::cout << ",\"chip_records\":";
        write_numbers(table.chip_records);
    }
    std::cout << '}';
}

void write_model(const NnModelData& model, bool include_data) {
    std::cout << "{\"version\":" << model.version << ",\"flags\":" << model.flags
              << ",\"material_count\":" << model.material_count
              << ",\"node_count\":" << model.node_count
              << ",\"max_node_depth\":" << model.max_node_depth
              << ",\"matrix_palette_count\":" << model.matrix_palette_count
              << ",\"texture_count\":" << model.texture_count
              << ",\"bounds_bits\":";
    write_numbers(model.bounds_bits);
    std::cout << ",\"box_bits\":";
    write_numbers(model.box_bits);
    std::cout << ",\"first_node_translation_bits\":";
    if (model.first_node_translation_bits.has_value()) {
        write_numbers(*model.first_node_translation_bits);
    } else {
        std::cout << "null";
    }
    std::cout << ",\"vertices\":[";
    for (std::size_t i = 0; i < model.vertices.size(); ++i) {
        if (i != 0) { std::cout << ','; }
        const auto& vertex = model.vertices[i];
        std::cout << "{\"flags\":" << vertex.flags << ",\"format\":" << vertex.format
                  << ",\"fvf\":" << vertex.fvf << ",\"stride\":" << vertex.stride
                  << ",\"count\":" << vertex.count << ",\"byte_count\":" << vertex.bytes.size()
                  << ",\"matrix_indices\":";
        write_numbers(vertex.matrix_indices);
        if (include_data) {
            std::cout << ",\"bytes_hex\":";
            write_hex(vertex.bytes);
        }
        std::cout << '}';
    }
    std::cout << "],\"primitives\":[";
    for (std::size_t i = 0; i < model.primitives.size(); ++i) {
        if (i != 0) { std::cout << ','; }
        const auto& primitive = model.primitives[i];
        std::cout << "{\"flags\":" << primitive.flags << ",\"mode\":" << primitive.mode
                  << ",\"index_count\":" << primitive.indices.size() << ",\"strip_lengths\":";
        write_numbers(primitive.strip_lengths);
        if (include_data) {
            std::cout << ",\"indices\":";
            write_numbers(primitive.indices);
        }
        std::cout << '}';
    }
    std::cout << "],\"sub_objects\":[";
    for (std::size_t i = 0; i < model.sub_objects.size(); ++i) {
        if (i != 0) { std::cout << ','; }
        const auto& sub = model.sub_objects[i];
        std::cout << "{\"flags\":" << sub.flags << ",\"reserved\":";
        write_numbers(sub.reserved);
        std::cout << ",\"meshes\":[";
        for (std::size_t j = 0; j < sub.meshes.size(); ++j) {
            if (j != 0) { std::cout << ','; }
            const auto& mesh = sub.meshes[j];
            std::cout << "{\"bounds_bits\":";
            write_numbers(mesh.bounds_bits);
            std::cout << ",\"node_index\":" << mesh.node_index
                      << ",\"matrix_index\":" << mesh.matrix_index
                      << ",\"material_index\":" << mesh.material_index
                      << ",\"vertex_index\":" << mesh.vertex_index
                      << ",\"primitive_index\":" << mesh.primitive_index
                      << ",\"reserved\":" << mesh.reserved << '}';
        }
        std::cout << "]}";
    }
    std::cout << "]}";
}

void inspect(const std::filesystem::path& path, bool include_data) {
    const auto bytes = load_archive(path);
    const auto entries = parse_pc_stage_archive(bytes.data(), bytes.size());
    std::vector<Resource> resources;
    resources.reserve(entries.size());
    std::size_t decoded_count = 0;
    std::size_t decoded_bytes = 0;
    for (const auto& entry : entries) {
        const auto suffix = extension(entry.name);
        if (suffix != ".ZNO" && suffix != ".DF" && suffix != ".DI" && suffix != ".AT") {
            resources.emplace_back(std::monostate{});
            continue;
        }
        if (entry.length > max_resource_bytes - decoded_bytes) {
            throw StageDataError("decoded resource inputs exceed the 64 MiB inspection limit");
        }
        decoded_bytes += entry.length;
        const auto* payload = bytes.data() + entry.offset;
        if (suffix == ".ZNO") {
            resources.emplace_back(parse_pc_nn_model(payload, entry.length));
        } else {
            const auto kind = suffix == ".DF" ? TerrainRecordKind::Height :
                              suffix == ".DI" ? TerrainRecordKind::Angle : TerrainRecordKind::Attribute;
            resources.emplace_back(parse_terrain_table(payload, entry.length, kind));
        }
        ++decoded_count;
    }

    std::cout << "{\"format\":\"pc-amb32-resources\",\"entry_count\":" << entries.size()
              << ",\"decoded_count\":" << decoded_count << ",\"entries\":[";
    for (std::size_t i = 0; i < entries.size(); ++i) {
        if (i != 0) { std::cout << ','; }
        const auto& entry = entries[i];
        std::cout << "{\"index\":" << i << ",\"name\":";
        write_string(entry.name);
        std::cout << ",\"offset\":" << entry.offset << ",\"length\":" << entry.length
                  << ",\"flags\":" << entry.flags;
        if (const auto* table = std::get_if<TerrainTable>(&resources[i])) {
            std::cout << ",\"terrain\":";
            write_terrain(*table, include_data);
        } else if (const auto* model = std::get_if<NnModelData>(&resources[i])) {
            std::cout << ",\"model\":";
            write_model(*model, include_data);
        }
        std::cout << '}';
    }
    std::cout << "]}\n";
    if (!std::cout) {
        throw StageDataError("cannot write inspection output");
    }
}

template <typename Char>
int run(int argc, Char** argv) {
    if (argc == 2 && std::filesystem::path(argv[1]) == "--help") {
        std::cout << "usage: resource_inspect_cli <pc-resource.amb> [--data]\n";
        return 0;
    }
    if ((argc != 2 && argc != 3) ||
        (argc == 3 && std::filesystem::path(argv[2]) != "--data")) {
        std::cerr << "usage: resource_inspect_cli <pc-resource.amb> [--data]\n";
        return 2;
    }
    try {
        inspect(std::filesystem::path(argv[1]), argc == 3);
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "resource_inspect_cli: " << error.what() << '\n';
        return 1;
    }
}

}

#ifdef _WIN32
int wmain(int argc, wchar_t** argv) {
    return run(argc, argv);
}
#else
int main(int argc, char** argv) {
    return run(argc, argv);
}
#endif
