#pragma once

#include <array>
#include <cstdint>
#include <string>

std::string nn_shader_archive_basename(const std::array<std::uint8_t, 16u>& key);
