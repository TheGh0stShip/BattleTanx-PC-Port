#include "battletanx/state_hash.hpp"

#include "battletanx/sha256.hpp"

#include <bit>

namespace battletanx {

void StateWriter::u16(std::uint16_t value) {
    u8(static_cast<std::uint8_t>(value>>8U));
    u8(static_cast<std::uint8_t>(value));
}

void StateWriter::u32(std::uint32_t value) {
    u16(static_cast<std::uint16_t>(value>>16U));
    u16(static_cast<std::uint16_t>(value));
}

void StateWriter::u64(std::uint64_t value) {
    u32(static_cast<std::uint32_t>(value>>32U));
    u32(static_cast<std::uint32_t>(value));
}

void StateWriter::f32(float value) {
    static_assert(sizeof(float)==4U);
    u32(std::bit_cast<std::uint32_t>(value));
}

std::array<std::uint8_t,32> StateWriter::digest() const {
    return sha256(bytes_);
}

std::string StateWriter::hex() const {
    return hex_digest(digest());
}

} // namespace battletanx
