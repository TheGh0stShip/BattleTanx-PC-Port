#include "battletanx/rom.hpp"
#include "battletanx/sha256.hpp"

#include <algorithm>
#include <fstream>
#include <stdexcept>

namespace battletanx {
namespace {
std::string trimmed_ascii(std::span<const std::uint8_t> bytes) {
    std::string text(bytes.begin(), bytes.end());
    while (!text.empty() && (text.back() == ' ' || text.back() == '\0')) text.pop_back();
    return text;
}
} // namespace

RomHeader Rom::inspect_header(std::span<const std::uint8_t> bytes) {
    const BigEndianView header(bytes);
    if (header.size() < 0x40U) throw std::runtime_error("ROM is too small to contain an N64 header");
    if (header.u32(0) != 0x80371240U) {
        throw std::runtime_error("ROM is not in big-endian .z64 format");
    }
    RomHeader result;
    result.title = trimmed_ascii(header.subspan(0x20U, 20U));
    result.game_code.reserve(4U);
    result.game_code.push_back(static_cast<char>(header.u8(0x3BU)));
    result.game_code.push_back(static_cast<char>(header.u8(0x3CU)));
    result.game_code.push_back(static_cast<char>(header.u8(0x3DU)));
    result.game_code.push_back(static_cast<char>(header.u8(0x3EU)));
    result.revision = header.u8(0x3FU);
    return result;
}

Rom Rom::load_verified(const std::filesystem::path& path) {
    if (path.extension() != ".z64") {
        throw std::runtime_error("ROM must be a big-endian .z64 file");
    }
    std::ifstream stream(path, std::ios::binary | std::ios::ate);
    if (!stream) throw std::runtime_error("could not open ROM: " + path.string());
    const auto length = stream.tellg();
    if (length != static_cast<std::streamoff>(expected_size)) {
        throw std::runtime_error("ROM must be exactly 8 MiB");
    }

    Rom rom;
    rom.bytes_.resize(expected_size);
    stream.seekg(0);
    stream.read(reinterpret_cast<char*>(rom.bytes_.data()),
                static_cast<std::streamsize>(rom.bytes_.size()));
    if (!stream) throw std::runtime_error("failed while reading ROM: " + path.string());

    rom.header_ = inspect_header(rom.bytes_);
    const auto actual_hash = hex_digest(sha256(rom.bytes_));
    if (actual_hash != expected_sha256) {
        throw std::runtime_error("unsupported ROM SHA-256: " + actual_hash);
    }
    return rom;
}

} // namespace battletanx

