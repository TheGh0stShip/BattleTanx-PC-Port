#include "battletanx/resource.hpp"

#include "battletanx/lzari.hpp"

#include <array>
#include <stdexcept>

namespace battletanx {
namespace {
constexpr std::array<ResourceDescriptor,21> kResources{{
    {"bundle_49C740",0x49C740U,0x49DCD0U,ResourceEncoding::raw,0x3A980U},
    {"bundle_49DCD0",0x49DCD0U,0x4AC150U,ResourceEncoding::lzari,0x25800U},
    {"bundle_4AC150",0x4AC150U,0x4AD7E0U,ResourceEncoding::lzari,0x25800U},
    {"bundle_4AD7E0",0x4AD7E0U,0x4B1300U,ResourceEncoding::lzari,0x25800U},
    {"bundle_4B1300",0x4B1300U,0x4B3280U,ResourceEncoding::lzari,0x25800U},
    {"bundle_4B3280",0x4B3280U,0x4B39B0U,ResourceEncoding::lzari,0x25800U},
    {"bundle_4B39B0",0x4B39B0U,0x4B4B00U,ResourceEncoding::lzari,0x25800U},
    {"bundle_3D6940",0x3D6940U,0x3DC430U,ResourceEncoding::lzari,0x25800U},
    {"bundle_3DFD10",0x3DFD10U,0x3EA970U,ResourceEncoding::lzari,0x25800U},
    {"bundle_2D2790",0x2D2790U,0x2E2CD0U,ResourceEncoding::lzari,0x25800U},
    {"bundle_3D16D0",0x3D16D0U,0x3D6940U,ResourceEncoding::lzari,0x25800U},
    {"bundle_3DC430",0x3DC430U,0x3DFD10U,ResourceEncoding::lzari,0x25800U},
    {"bundle_4B4B00",0x4B4B00U,0x4C1930U,ResourceEncoding::lzari,0x3A980U},
    {"bundle_4C1930",0x4C1930U,0x4E46E0U,ResourceEncoding::lzari,0x3A980U},
    {"bundle_4E46E0",0x4E46E0U,0x50D930U,ResourceEncoding::lzari,0x493E0U},
    {"bundle_50D930",0x50D930U,0x533870U,ResourceEncoding::lzari,0x3A980U},
    {"bundle_533870",0x533870U,0x554D00U,ResourceEncoding::lzari,0x3A980U},
    {"bundle_554D00",0x554D00U,0x56FDB0U,ResourceEncoding::lzari,0x25800U},
    {"bundle_56FDB0",0x56FDB0U,0x58AE00U,ResourceEncoding::lzari,0x3A980U},
    {"bundle_58AE00",0x58AE00U,0x596EA0U,ResourceEncoding::lzari,0x3A980U},
    {"bundle_596EA0",0x596EA0U,0x5A1110U,ResourceEncoding::lzari,0x25800U},
}};
} // namespace

std::span<const ResourceDescriptor> resource_descriptors() noexcept { return kResources; }

const ResourceDescriptor& find_resource(std::string_view name) {
    for (const auto& descriptor : kResources) {
        if (descriptor.name==name) return descriptor;
    }
    throw std::runtime_error("unknown BattleTanx resource: "+std::string(name));
}

std::vector<std::uint8_t> load_resource(
    const Rom& rom,const ResourceDescriptor& descriptor) {
    if (descriptor.rom_start>=descriptor.rom_end || descriptor.rom_end>rom.bytes().size()) {
        throw std::runtime_error("resource range is outside the verified ROM");
    }
    const auto source=rom.bytes().subspan(
        descriptor.rom_start,descriptor.rom_end-descriptor.rom_start);
    if (descriptor.encoding==ResourceEncoding::raw) {
        if (source.size()>descriptor.capacity) {
            throw std::runtime_error("raw resource exceeds its original loader capacity");
        }
        return {source.begin(),source.end()};
    }
    return lzari_decompress(source,descriptor.capacity);
}

} // namespace battletanx

