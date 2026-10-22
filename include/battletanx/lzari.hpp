#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace battletanx {
std::vector<std::uint8_t> lzari_decompress(
    std::span<const std::uint8_t> stream,
    std::size_t max_output = 64U << 20U);
} // namespace battletanx

