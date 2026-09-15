#pragma once

#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <vector>

struct TxbTextureData {
    std::uint32_t file_type = 0u;
    std::uint16_t min_filter = 0u;
    std::uint16_t mag_filter = 0u;
    std::uint32_t global_index = 0u;
    std::uint32_t bank = 0u;
    std::string name;
};

class TxbTextureError : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

std::vector<TxbTextureData> parse_pc_txb_texture_list(
    const std::uint8_t* data,
    std::size_t size);
