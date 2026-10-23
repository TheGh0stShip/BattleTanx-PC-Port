#pragma once

#include "battletanx/entity_id.hpp"

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace battletanx {

// Explicit big-endian serializer used for deterministic state digests. Only
// values written through these methods are hashed, so padding, pointers and
// container iteration order never contribute.
class StateWriter {
public:
    void u8(std::uint8_t value) { bytes_.push_back(value); }
    void boolean(bool value) { u8(value ? std::uint8_t{1U} : std::uint8_t{0U}); }
    void u16(std::uint16_t value);
    void u32(std::uint32_t value);
    void u64(std::uint64_t value);
    void s16(std::int16_t value) { u16(static_cast<std::uint16_t>(value)); }
    void s32(std::int32_t value) { u32(static_cast<std::uint32_t>(value)); }
    // IEEE-754 binary32 bit pattern; -0.0 and NaN payloads are hashed as stored.
    void f32(float value);
    void id(EntityId value) { u16(value.slot); u16(value.generation); }

    [[nodiscard]] const std::vector<std::uint8_t>& bytes() const noexcept { return bytes_; }
    [[nodiscard]] std::array<std::uint8_t,32> digest() const;
    [[nodiscard]] std::string hex() const;

private:
    std::vector<std::uint8_t> bytes_;
};

} // namespace battletanx
