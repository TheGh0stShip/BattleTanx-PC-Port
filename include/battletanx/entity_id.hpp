#pragma once

#include <cstdint>

namespace battletanx {

// Host-owned reference to a pooled runtime object. Slots are reused; the
// generation changes every time a slot is released, so a stale handle never
// resolves to the object that later occupies the same slot.
struct EntityId {
    std::uint16_t slot{};
    std::uint16_t generation{};

    [[nodiscard]] constexpr bool valid() const noexcept { return generation!=0U; }
    [[nodiscard]] friend constexpr bool operator==(EntityId,EntityId) noexcept=default;
};

inline constexpr EntityId no_entity{};

} // namespace battletanx
