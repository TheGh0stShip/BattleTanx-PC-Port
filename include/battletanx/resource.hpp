#pragma once

#include "battletanx/rom.hpp"

#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>
#include <vector>

namespace battletanx {

enum class ResourceEncoding { raw, lzari };

struct ResourceDescriptor {
    std::string_view name;
    std::size_t rom_start{};
    std::size_t rom_end{};
    ResourceEncoding encoding{};
    std::size_t capacity{};
};

std::span<const ResourceDescriptor> resource_descriptors() noexcept;
const ResourceDescriptor& find_resource(std::string_view name);
std::vector<std::uint8_t> load_resource(const Rom& rom,const ResourceDescriptor& descriptor);

} // namespace battletanx

