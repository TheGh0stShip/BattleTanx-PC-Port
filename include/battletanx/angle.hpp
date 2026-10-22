#pragma once

#include <array>
#include <cstdint>
#include <span>

namespace battletanx {

class AngleTable {
  public:
    static constexpr std::size_t value_count=257U;

    explicit AngleTable(std::array<float,value_count> values) : values_(values) {}
    static AngleTable from_big_endian(std::span<const std::uint8_t> bytes);

    [[nodiscard]] float sine(std::uint32_t angle) const noexcept;
    [[nodiscard]] float cosine(std::uint32_t angle) const noexcept;

  private:
    std::array<float,value_count> values_{};
};

} // namespace battletanx

