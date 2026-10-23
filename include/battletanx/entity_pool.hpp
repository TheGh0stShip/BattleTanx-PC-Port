#pragma once

#include "battletanx/entity_id.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <type_traits>

namespace battletanx {

// Fixed-capacity pool with generation-checked handles.
//
// Allocation always returns the lowest free slot, so allocation order is a
// pure function of the sequence of allocate/release calls. Exhaustion returns
// no value and leaves the pool unchanged. Iteration visits occupied slots in
// ascending slot order.
template <typename T,std::size_t Capacity>
class EntityPool {
    static_assert(Capacity>0U && Capacity<=0xFFFFU,"pool slots must fit EntityId::slot");
    static_assert(std::is_default_constructible_v<T>);

public:
    static constexpr std::size_t capacity=Capacity;

    [[nodiscard]] std::optional<EntityId> allocate() {
        for (std::size_t slot=0U; slot<Capacity; ++slot) {
            Entry& entry=entries_[slot];
            if (entry.occupied) continue;
            entry.occupied=true;
            entry.value=T{};
            ++count_;
            return EntityId{static_cast<std::uint16_t>(slot),entry.generation};
        }
        return std::nullopt;
    }

    bool release(EntityId id) noexcept {
        Entry* entry=lookup(id);
        if (entry==nullptr) return false;
        entry->occupied=false;
        entry->value=T{};
        entry->generation=next_generation(entry->generation);
        --count_;
        return true;
    }

    [[nodiscard]] T* get(EntityId id) noexcept {
        Entry* entry=lookup(id);
        return entry==nullptr ? nullptr : &entry->value;
    }
    [[nodiscard]] const T* get(EntityId id) const noexcept {
        const Entry* entry=lookup(id);
        return entry==nullptr ? nullptr : &entry->value;
    }
    [[nodiscard]] bool contains(EntityId id) const noexcept { return lookup(id)!=nullptr; }

    [[nodiscard]] std::size_t size() const noexcept { return count_; }
    [[nodiscard]] bool full() const noexcept { return count_==Capacity; }

    // Visits (EntityId, T&) for every occupied slot in ascending slot order.
    // The callback may release the visited entity; it must not allocate.
    template <typename Visitor>
    void for_each(Visitor&& visit) {
        for (std::size_t slot=0U; slot<Capacity; ++slot) {
            Entry& entry=entries_[slot];
            if (!entry.occupied) continue;
            visit(EntityId{static_cast<std::uint16_t>(slot),entry.generation},entry.value);
        }
    }
    template <typename Visitor>
    void for_each(Visitor&& visit) const {
        for (std::size_t slot=0U; slot<Capacity; ++slot) {
            const Entry& entry=entries_[slot];
            if (!entry.occupied) continue;
            visit(EntityId{static_cast<std::uint16_t>(slot),entry.generation},entry.value);
        }
    }

    // Raw slot inspection for deterministic serialization.
    [[nodiscard]] bool slot_occupied(std::size_t slot) const noexcept {
        return slot<Capacity && entries_[slot].occupied;
    }
    [[nodiscard]] std::uint16_t slot_generation(std::size_t slot) const noexcept {
        return slot<Capacity ? entries_[slot].generation : std::uint16_t{0U};
    }
    [[nodiscard]] EntityId slot_id(std::size_t slot) const noexcept {
        if (!slot_occupied(slot)) return no_entity;
        return EntityId{static_cast<std::uint16_t>(slot),entries_[slot].generation};
    }

private:
    struct Entry {
        T value{};
        std::uint16_t generation{1U};
        bool occupied{};
    };

    static constexpr std::uint16_t next_generation(std::uint16_t generation) noexcept {
        const auto next=static_cast<std::uint16_t>(generation+1U);
        return next==0U ? std::uint16_t{1U} : next;
    }

    Entry* lookup(EntityId id) noexcept {
        if (id.slot>=Capacity) return nullptr;
        Entry& entry=entries_[id.slot];
        return entry.occupied && entry.generation==id.generation ? &entry : nullptr;
    }
    const Entry* lookup(EntityId id) const noexcept {
        if (id.slot>=Capacity) return nullptr;
        const Entry& entry=entries_[id.slot];
        return entry.occupied && entry.generation==id.generation ? &entry : nullptr;
    }

    std::array<Entry,Capacity> entries_{};
    std::size_t count_{};
};

} // namespace battletanx
