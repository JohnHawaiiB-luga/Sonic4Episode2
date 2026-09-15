#include "stage_data.h"

#include <algorithm>
#include <cstdint>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <optional>
#include <string>
#include <vector>

namespace {

std::vector<std::uint8_t> load_archive(const std::filesystem::path& path) {
    constexpr std::uint64_t max_archive_bytes = 64u * 1024u * 1024u;
    std::ifstream input(path, std::ios::binary | std::ios::ate);
    if (!input) {
        throw StageDataError("cannot open archive");
    }
    const auto end = input.tellg();
    if (end < 0 || static_cast<std::uint64_t>(end) > max_archive_bytes) {
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

std::uint8_t grid_cell_bytes(const std::string& name) {
    if (name.size() < 3 || name[name.size() - 3] != '.') {
        return 0;
    }
    const auto upper = [](char c) { return c >= 'a' && c <= 'z' ? c - 'a' + 'A' : c; };
    if (upper(name[name.size() - 2]) != 'M') {
        return 0;
    }
    const auto suffix = upper(name.back());
    return suffix == 'P' ? 2 : suffix == 'D' ? 1 : 0;
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

void inspect(const std::filesystem::path& path, bool include_cells) {
    const auto bytes = load_archive(path);
    const auto entries = parse_pc_stage_archive(bytes.data(), bytes.size());
    std::vector<std::optional<StageGridData>> grids;
    grids.reserve(entries.size());
    std::size_t grid_count = 0;
    for (const auto& entry : entries) {
        const auto cell_bytes = grid_cell_bytes(entry.name);
        if (cell_bytes != 0) {
            grids.emplace_back(parse_stage_grid(bytes.data() + entry.offset,
                                                entry.length, cell_bytes));
            ++grid_count;
        } else {
            grids.emplace_back(std::nullopt);
        }
    }

    std::cout << "{\"format\":\"pc-amb32\",\"entry_count\":" << entries.size()
              << ",\"grid_count\":" << grid_count << ",\"entries\":[";
    for (std::size_t i = 0; i < entries.size(); ++i) {
        const auto& entry = entries[i];
        if (i != 0) {
            std::cout << ',';
        }
        std::cout << "{\"index\":" << i << ",\"name\":";
        write_string(entry.name);
        std::cout << ",\"offset\":" << entry.offset << ",\"length\":" << entry.length
                  << ",\"flags\":" << entry.flags;
        if (grids[i]) {
            const auto& grid = *grids[i];
            const auto nonzero = std::count_if(grid.cells.begin(), grid.cells.end(),
                                             [](std::uint16_t cell) { return cell != 0; });
            std::cout << ",\"grid\":{\"width\":" << grid.width
                      << ",\"height\":" << grid.height
                      << ",\"cell_bytes\":" << static_cast<unsigned int>(grid.cell_bytes)
                      << ",\"cell_count\":" << grid.cells.size()
                      << ",\"nonzero_cells\":" << nonzero
                      << ",\"max_cell\":" << *std::max_element(grid.cells.begin(), grid.cells.end());
            if (include_cells) {
                std::cout << ",\"cells\":[";
                for (std::size_t cell = 0; cell < grid.cells.size(); ++cell) {
                    if (cell != 0) {
                        std::cout << ',';
                    }
                    std::cout << grid.cells[cell];
                }
                std::cout << ']';
            }
            std::cout << '}';
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
        std::cout << "usage: stage_inspect_cli <pc-map.amb> [--cells]\n";
        return 0;
    }
    if ((argc != 2 && argc != 3) ||
        (argc == 3 && std::filesystem::path(argv[2]) != "--cells")) {
        std::cerr << "usage: stage_inspect_cli <pc-map.amb> [--cells]\n";
        return 2;
    }
    try {
        inspect(std::filesystem::path(argv[1]), argc == 3);
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "stage_inspect_cli: " << error.what() << '\n';
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
