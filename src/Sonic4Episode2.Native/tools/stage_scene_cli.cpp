#include "nn_model_data.h"
#include "stage_scene.h"

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <locale>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {

constexpr std::uint64_t kMaxInputBytes = 64u * 1024u * 1024u;
constexpr double kOffsetLimit = 16777216.0;

struct PrecisionChoice {
    StageTransformPrecision transform_precision;
    std::uint32_t output_precision;
};

struct SelectedLayer {
    StageGridData mp;
    StageGridData md;
};

[[noreturn]] void fail(const char* message) {
    throw StageDataError(message);
}

std::vector<std::uint8_t> load_input(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary | std::ios::ate);
    if (!input) {
        fail("cannot open scene input");
    }
    const std::streampos end = input.tellg();
    if (end < std::streampos(0) || static_cast<std::uint64_t>(end) > kMaxInputBytes) {
        fail("scene input size is invalid or exceeds the 64 MiB limit");
    }

    std::vector<std::uint8_t> bytes(static_cast<std::size_t>(end));
    input.seekg(0, std::ios::beg);
    if (!bytes.empty()) {
        input.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
        if (!input) {
            fail("cannot read complete scene input");
        }
    }
    return bytes;
}

bool is_ascii_alphanumeric_or_underscore(unsigned char value) {
    return (value >= static_cast<unsigned char>('A') && value <= static_cast<unsigned char>('Z')) ||
           (value >= static_cast<unsigned char>('a') && value <= static_cast<unsigned char>('z')) ||
           (value >= static_cast<unsigned char>('0') && value <= static_cast<unsigned char>('9')) ||
           value == static_cast<unsigned char>('_');
}

std::string uppercase_ascii(std::string_view value) {
    std::string result;
    result.reserve(value.size());
    for (const char character : value) {
        const unsigned char byte = static_cast<unsigned char>(character);
        if (byte >= static_cast<unsigned char>('a') && byte <= static_cast<unsigned char>('z')) {
            result.push_back(static_cast<char>(byte - static_cast<unsigned char>('a') + static_cast<unsigned char>('A')));
        } else {
            result.push_back(character);
        }
    }
    return result;
}

std::string canonical_layer(std::string_view value) {
    if (value.empty() || value.size() > 64u) {
        fail("scene layer length is unsupported");
    }
    for (const char character : value) {
        if (!is_ascii_alphanumeric_or_underscore(static_cast<unsigned char>(character))) {
            fail("scene layer must contain only ASCII letters, digits, or underscores");
        }
    }
    return uppercase_ascii(value);
}

std::string filename_part(const std::string& value) {
    const std::size_t separator = value.find_last_of("\\/");
    return separator == std::string::npos ? value : value.substr(separator + 1u);
}

std::string filename_stem(const std::string& value) {
    const std::string filename = filename_part(value);
    const std::size_t dot = filename.find_last_of('.');
    return dot == std::string::npos ? std::string{} : filename.substr(0u, dot);
}

std::string filename_extension(const std::string& value) {
    const std::string filename = filename_part(value);
    const std::size_t dot = filename.find_last_of('.');
    return dot == std::string::npos ? std::string{} : filename.substr(dot);
}

SelectedLayer select_layer(
    const std::vector<std::uint8_t>& archive,
    const std::string& layer) {
    const std::vector<StageArchiveEntry> entries =
        parse_pc_stage_archive(archive.data(), archive.size());
    std::optional<StageGridData> mp;
    std::optional<StageGridData> md;
    for (const StageArchiveEntry& entry : entries) {
        if (uppercase_ascii(filename_stem(entry.name)) != layer) {
            continue;
        }
        const std::string extension = uppercase_ascii(filename_extension(entry.name));
        if (extension == ".MP") {
            if (mp.has_value()) {
                fail("scene map contains duplicate MP grids for the layer");
            }
            mp.emplace(parse_stage_grid(archive.data() + entry.offset, entry.length, 2u));
        } else if (extension == ".MD") {
            if (md.has_value()) {
                fail("scene map contains duplicate MD grids for the layer");
            }
            md.emplace(parse_stage_grid(archive.data() + entry.offset, entry.length, 1u));
        }
    }
    if (!mp.has_value() || !md.has_value()) {
        fail("scene map is missing an MP or MD grid for the layer");
    }
    return {std::move(*mp), std::move(*md)};
}

std::vector<StageSceneModel> load_models(const std::vector<std::uint8_t>& archive) {
    const std::vector<StageArchiveEntry> entries =
        parse_pc_stage_archive(archive.data(), archive.size());
    if (entries.empty() || entries.size() > 4096u) {
        fail("scene model count is unsupported");
    }

    std::vector<StageSceneModel> models;
    models.reserve(entries.size());
    for (const StageArchiveEntry& entry : entries) {
        StageSceneModel model{};
        model.archive_flags = entry.flags;
        if (entry.length != 0u) {
            const NnModelData parsed =
                parse_pc_nn_model(archive.data() + entry.offset, entry.length);
            model.first_node_translation_bits = parsed.first_node_translation_bits;
        }
        models.push_back(std::move(model));
    }
    return models;
}

bool is_decimal(std::string_view value) {
    std::size_t cursor = 0u;
    if (cursor < value.size() && (value[cursor] == '+' || value[cursor] == '-')) {
        ++cursor;
    }

    bool has_digits = false;
    while (cursor < value.size() && value[cursor] >= '0' && value[cursor] <= '9') {
        has_digits = true;
        ++cursor;
    }
    if (cursor < value.size() && value[cursor] == '.') {
        ++cursor;
        while (cursor < value.size() && value[cursor] >= '0' && value[cursor] <= '9') {
            has_digits = true;
            ++cursor;
        }
    }
    if (!has_digits) {
        return false;
    }
    if (cursor < value.size() && (value[cursor] == 'e' || value[cursor] == 'E')) {
        ++cursor;
        if (cursor < value.size() && (value[cursor] == '+' || value[cursor] == '-')) {
            ++cursor;
        }
        const std::size_t exponent_start = cursor;
        while (cursor < value.size() && value[cursor] >= '0' && value[cursor] <= '9') {
            ++cursor;
        }
        if (cursor == exponent_start) {
            return false;
        }
    }
    return cursor == value.size();
}

float parse_offset(std::string_view value) {
    if (!is_decimal(value)) {
        fail("scene offset must be a decimal number");
    }

    std::istringstream input{std::string(value)};
    input.imbue(std::locale::classic());
    double parsed = 0.0;
    input >> parsed;
    if (!input || !std::isfinite(parsed) || std::fabs(parsed) > kOffsetLimit) {
        fail("scene offset is nonfinite or out of range");
    }
    const float result = static_cast<float>(parsed);
    if (!std::isfinite(result) || std::fabs(result) > static_cast<float>(kOffsetLimit)) {
        fail("scene offset is nonfinite or out of range");
    }
    return result;
}

PrecisionChoice parse_precision(std::string_view value) {
    if (value == "24") {
        return {StageTransformPrecision::Single, 24u};
    }
    if (value == "53") {
        return {StageTransformPrecision::Double, 53u};
    }
    fail("scene precision must be 24 or 53");
}

std::uint32_t float_bits(float value) {
    static_assert(sizeof(float) == sizeof(std::uint32_t));
    std::uint32_t result = 0u;
    std::memcpy(&result, &value, sizeof(result));
    return result;
}

template <std::size_t Size>
void write_bits(std::ostringstream& output, const std::array<float, Size>& values) {
    output << '[';
    for (std::size_t index = 0u; index < values.size(); ++index) {
        if (index != 0u) {
            output << ',';
        }
        output << float_bits(values[index]);
    }
    output << ']';
}

std::string make_scene_json(
    const std::string& layer,
    std::uint32_t precision,
    const SelectedLayer& grids,
    const std::vector<StageSceneModel>& models,
    const std::array<float, 3u>& offsets,
    const std::vector<StageSceneInstance>& instances) {
    std::ostringstream output;
    output << "{\"format\":\"pc-stage-scene-v1\",\"precision\":" << precision
           << ",\"layer\":\"" << layer << "\",\"width\":" << grids.mp.width
           << ",\"height\":" << grids.mp.height << ",\"model_count\":" << models.size()
           << ",\"offset_bits\":";
    write_bits(output, offsets);
    output << ",\"instances\":[";
    for (std::size_t index = 0u; index < instances.size(); ++index) {
        if (index != 0u) {
            output << ',';
        }
        const StageSceneInstance& instance = instances[index];
        const StageMapPlacement& placement = instance.placement;
        output << "{\"x\":" << placement.anchor_x << ",\"y\":" << placement.anchor_y
               << ",\"model\":" << placement.model_id << ",\"rotation\":" << placement.rotation
               << ",\"flip_x\":" << (placement.flip_x ? "true" : "false")
               << ",\"flip_y\":" << (placement.flip_y ? "true" : "false")
               << ",\"matrix_bits\":";
        write_bits(output, instance.transform);
        output << '}';
    }
    output << "]}";
    return output.str();
}

void export_scene(
    const std::filesystem::path& models_path,
    const std::filesystem::path& map_path,
    const std::string& layer,
    const PrecisionChoice& precision,
    const std::array<float, 3u>& offsets) {
    const std::vector<std::uint8_t> model_archive = load_input(models_path);
    const std::vector<std::uint8_t> map_archive = load_input(map_path);
    const std::vector<StageSceneModel> models = load_models(model_archive);
    const SelectedLayer grids = select_layer(map_archive, layer);
    if (static_cast<std::uint64_t>(grids.mp.width) * grids.mp.height > 1048576u) {
        fail("scene grid exceeds the one-million-cell limit");
    }
    const StageMapRange range = {
        0,
        static_cast<std::int32_t>(grids.mp.width) - 1,
        0,
        static_cast<std::int32_t>(grids.mp.height) - 1,
    };
    const std::vector<StageSceneInstance> instances = assemble_pc_stage_instances(
        grids.mp,
        grids.md,
        models,
        range,
        offsets,
        precision.transform_precision);
    if (instances.size() > 131072u) {
        fail("scene exceeds the instance limit");
    }
    const std::string json = make_scene_json(
        layer,
        precision.output_precision,
        grids,
        models,
        offsets,
        instances);
    if (json.size() > kMaxInputBytes) {
        fail("scene output exceeds the 64 MiB limit");
    }
    std::cout << json << '\n';
    if (!std::cout) {
        fail("cannot write scene output");
    }
}

void print_usage(std::ostream& output) {
    output << "usage: stage_scene_cli <models.amb> <map.amb> <layer> <precision:24|53> <offset-x> <offset-y> <offset-z>\n";
}

int run(
    const std::filesystem::path& models_path,
    const std::filesystem::path& map_path,
    const std::string& layer_argument,
    const std::string& precision_argument,
    const std::string& offset_x_argument,
    const std::string& offset_y_argument,
    const std::string& offset_z_argument) {
    try {
        const std::string layer = canonical_layer(layer_argument);
        const PrecisionChoice precision = parse_precision(precision_argument);
        const std::array<float, 3u> offsets = {{
            parse_offset(offset_x_argument),
            parse_offset(offset_y_argument),
            parse_offset(offset_z_argument),
        }};
        export_scene(models_path, map_path, layer, precision, offsets);
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "stage_scene_cli: " << error.what() << '\n';
        return 1;
    }
}

#ifdef _WIN32
std::string narrow_ascii(const wchar_t* value) {
    std::string result;
    for (const wchar_t character : std::wstring_view(value)) {
        const std::uint32_t code_point = static_cast<std::uint32_t>(character);
        if (code_point > 0x7Fu) {
            fail("scene non-path arguments must be ASCII");
        }
        result.push_back(static_cast<char>(code_point));
    }
    return result;
}
#endif

}

#ifdef _WIN32
int wmain(int argc, wchar_t* argv[]) {
    if (argc == 2 && std::wstring_view(argv[1]) == L"--help") {
        print_usage(std::cout);
        return std::cout ? 0 : 1;
    }
    if (argc != 8) {
        print_usage(std::cerr);
        return 2;
    }

    try {
        return run(
            std::filesystem::path(argv[1]),
            std::filesystem::path(argv[2]),
            narrow_ascii(argv[3]),
            narrow_ascii(argv[4]),
            narrow_ascii(argv[5]),
            narrow_ascii(argv[6]),
            narrow_ascii(argv[7]));
    } catch (const std::exception& error) {
        std::cerr << "stage_scene_cli: " << error.what() << '\n';
        return 1;
    }
}
#else
int main(int argc, char* argv[]) {
    if (argc == 2 && std::string_view(argv[1]) == "--help") {
        print_usage(std::cout);
        return std::cout ? 0 : 1;
    }
    if (argc != 8) {
        print_usage(std::cerr);
        return 2;
    }

    return run(
        std::filesystem::path(argv[1]),
        std::filesystem::path(argv[2]),
        argv[3],
        argv[4],
        argv[5],
        argv[6],
        argv[7]);
}
#endif
