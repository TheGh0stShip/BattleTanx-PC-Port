#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace battletanx {
struct BundleEntry { std::size_t offset{}; std::size_t size{}; };

class Bundle {
  public:
    static Bundle parse(std::span<const std::uint8_t> bytes);
    [[nodiscard]] std::size_t payload_offset() const noexcept { return payload_offset_; }
    [[nodiscard]] std::span<const BundleEntry> entries() const noexcept { return entries_; }
  private:
    std::size_t payload_offset_{};
    std::vector<BundleEntry> entries_;
};
} // namespace battletanx

