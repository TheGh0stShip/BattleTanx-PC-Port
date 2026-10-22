#pragma once

#include "battletanx/big_endian.hpp"

#include <cstdint>
#include <filesystem>
#include <span>
#include <string>
#include <vector>

namespace battletanx {

struct RomHeader {
    std::string title;
    std::string game_code;
    std::uint8_t revision{};
};

class Rom {
  public:
    static constexpr std::size_t expected_size = 8U * 1024U * 1024U;
    static constexpr std::string_view expected_sha256 =
        "c5b7cf3523de025e3f18e2c8df2deb0eced5613e7c46cb8c6c5a4f22644ead3f";

    static Rom load_verified(const std::filesystem::path& path);
    static RomHeader inspect_header(std::span<const std::uint8_t> bytes);

    [[nodiscard]] std::span<const std::uint8_t> bytes() const noexcept { return bytes_; }
    [[nodiscard]] BigEndianView view() const noexcept { return BigEndianView(bytes_); }
    [[nodiscard]] const RomHeader& header() const noexcept { return header_; }

  private:
    std::vector<std::uint8_t> bytes_;
    RomHeader header_;
};

} // namespace battletanx

