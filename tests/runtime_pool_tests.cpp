#include "battletanx/entity_pool.hpp"
#include "battletanx/runtime_events.hpp"
#include "battletanx/state_hash.hpp"

#include "runtime_test_support.hpp"

#include <vector>

namespace {
struct Payload {
    int value{};
};
using SmallPool=battletanx::EntityPool<Payload,3>;
} // namespace

RUNTIME_TEST(pool_allocates_lowest_free_slot) {
    SmallPool pool;
    const auto a=pool.allocate();
    const auto b=pool.allocate();
    REQUIRE(a && b);
    REQUIRE(a->slot==0U && b->slot==1U);
    REQUIRE(a->generation==1U && b->generation==1U);
    REQUIRE(pool.release(*a));
    const auto c=pool.allocate();
    REQUIRE(c && c->slot==0U);
    return true;
}

RUNTIME_TEST(pool_reuse_increments_generation_and_rejects_stale_handles) {
    SmallPool pool;
    const auto first=*pool.allocate();
    pool.get(first)->value=7;
    REQUIRE(pool.release(first));
    REQUIRE(!pool.release(first));
    REQUIRE(pool.get(first)==nullptr);
    const auto second=*pool.allocate();
    REQUIRE(second.slot==first.slot);
    REQUIRE(second.generation==first.generation+1U);
    REQUIRE(pool.get(first)==nullptr);
    REQUIRE(pool.get(second)!=nullptr && pool.get(second)->value==0);
    return true;
}

RUNTIME_TEST(pool_exhaustion_is_deterministic) {
    SmallPool pool;
    REQUIRE(pool.allocate() && pool.allocate() && pool.allocate());
    REQUIRE(pool.full());
    REQUIRE(!pool.allocate());
    REQUIRE(pool.size()==3U);
    return true;
}

RUNTIME_TEST(pool_rejects_invalid_and_out_of_range_ids) {
    SmallPool pool;
    REQUIRE(pool.get(battletanx::no_entity)==nullptr);
    REQUIRE(pool.get(battletanx::EntityId{9U,1U})==nullptr);
    REQUIRE(!battletanx::no_entity.valid());
    return true;
}

RUNTIME_TEST(pool_iterates_in_slot_order_and_allows_release_during_visit) {
    SmallPool pool;
    const auto a=*pool.allocate();
    const auto b=*pool.allocate();
    const auto c=*pool.allocate();
    pool.get(a)->value=1; pool.get(b)->value=2; pool.get(c)->value=3;
    REQUIRE(pool.release(b));
    pool.get(*pool.allocate())->value=4;   // reuses slot 1
    std::vector<int> seen;
    pool.for_each([&](battletanx::EntityId id,Payload& payload) {
        seen.push_back(payload.value);
        if (payload.value==1) pool.release(id);
    });
    REQUIRE((seen==std::vector<int>{1,4,3}));
    REQUIRE(pool.size()==2U);
    return true;
}

RUNTIME_TEST(pool_generation_wraps_without_reaching_zero) {
    battletanx::EntityPool<Payload,1> pool;
    battletanx::EntityId id{};
    for (int i=0; i<0x10001; ++i) {
        id=*pool.allocate();
        REQUIRE(id.generation!=0U);
        REQUIRE(pool.release(id));
    }
    return true;
}

RUNTIME_TEST(event_queue_is_bounded_and_ordered) {
    battletanx::GameplayEventQueue queue(2U);
    queue.push({battletanx::GameplayEventKind::weapon_fired,1});
    queue.push({battletanx::GameplayEventKind::projectile_impact,2});
    queue.push({battletanx::GameplayEventKind::unit_destroyed,3});
    REQUIRE(queue.events().size()==2U);
    REQUIRE(queue.events()[0].kind==battletanx::GameplayEventKind::weapon_fired);
    REQUIRE(queue.events()[1].tick==2);
    REQUIRE(queue.dropped()==1U);
    queue.clear();
    REQUIRE(queue.events().empty() && queue.dropped()==0U);
    return true;
}

RUNTIME_TEST(state_writer_is_big_endian) {
    battletanx::StateWriter writer;
    writer.u16(0x1234U);
    writer.u32(0xA1B2C3D4U);
    writer.f32(1.0F);
    writer.id({0x0102U,0x0304U});
    const std::vector<std::uint8_t> expected{0x12,0x34,0xA1,0xB2,0xC3,0xD4,0x3F,0x80,0x00,0x00,
                                             0x01,0x02,0x03,0x04};
    REQUIRE(writer.bytes()==expected);
    battletanx::StateWriter empty;
    REQUIRE(empty.hex()=="e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
    return true;
}

RUNTIME_TEST_MAIN()
