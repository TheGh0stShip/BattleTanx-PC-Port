#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <stdexcept>

namespace battletanx {

class BigEndianView {
  public:
    explicit BigEndianView(std::span<const std::uint8_t> bytes) : bytes_(bytes) {}

    [[nodiscard]] std::size_t size() const noexcept { return bytes_.size(); }

    [[nodiscard]] std::uint8_t u8(std::size_t offset) const {
        require(offset, 1);
        return bytes_[offset];
    }

    [[nodiscard]] std::uint16_t u16(std::size_t offset) const {
        require(offset, 2);
        return static_cast<std::uint16_t>(
            (static_cast<std::uint16_t>(bytes_[offset]) << 8U) |
            static_cast<std::uint16_t>(bytes_[offset + 1]));
    }

    [[nodiscard]] std::uint32_t u32(std::size_t offset) const {
        require(offset, 4);
        return (static_cast<std::uint32_t>(bytes_[offset]) << 24U) |
               (static_cast<std::uint32_t>(bytes_[offset + 1]) << 16U) |
               (static_cast<std::uint32_t>(bytes_[offset + 2]) << 8U) |
               static_cast<std::uint32_t>(bytes_[offset + 3]);
    }

    [[nodiscard]] std::int16_t s16(std::size_t offset) const {
        return static_cast<std::int16_t>(u16(offset));
    }

    [[nodiscard]] std::int32_t s32(std::size_t offset) const {
        return static_cast<std::int32_t>(u32(offset));
    }

    [[nodiscard]] std::span<const std::uint8_t> subspan(
        std::size_t offset, std::size_t length) const {
        require(offset, length);
        return bytes_.subspan(offset, length);
    }

  private:
    void require(std::size_t offset, std::size_t length) const {
        if (offset > bytes_.size() || length > bytes_.size() - offset) {
            throw std::out_of_range("big-endian data read is out of bounds");
        }
    }

    std::span<const std::uint8_t> bytes_;
};

static_assert(sizeof(std::uint32_t) == 4);
static_assert(sizeof(std::uint16_t) == 2);

} // namespace battletanx

