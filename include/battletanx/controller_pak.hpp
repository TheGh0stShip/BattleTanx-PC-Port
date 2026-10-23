#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>

namespace battletanx {

struct ControllerPakRecord {
    static constexpr std::size_t size=0x100U;
    std::array<std::uint8_t,size> bytes{};
};

std::uint32_t controller_pak_checksum(const ControllerPakRecord& record) noexcept;
bool controller_pak_checksum_valid(const ControllerPakRecord& record) noexcept;
std::optional<ControllerPakRecord> load_controller_pak_record(
    const std::filesystem::path& path);
void write_controller_pak_record(const std::filesystem::path& path,
                                 ControllerPakRecord record);

} // namespace battletanx
