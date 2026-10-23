#pragma once

#include "battletanx/campaign_catalog.hpp"
#include "battletanx/unit_kinds.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <type_traits>

// Owner turret profile tables. initialize_player_record (func_800A31C0)
// selects the skill profile record +0x1C + (group*4 + owner)*0x24 (group =
// record +0x217, the selected difficulty) and copies, for type 0..3:
//   owner +0x338[type] = profile(+0x14)->s16[type]  turret shot damage
//   owner +0x340[type] = profile(+0x18)->s16[type]  turret fire delay
// The pointed-to s16[4] tables form one pool, D_80142CF8..D_80142DE7
// (ROM 0x0D2CF8..0x0D2DE7, 8-byte stride). CampaignSkillProfile keeps the raw
// pointers (gun_damage_vram / gun_fire_rate_vram); this module resolves them
// to values and builds TurretOwnerConfig.
namespace battletanx::owner_profiles {

using GunValues=std::array<std::int16_t,turret_types>;
static_assert(turret_types==4U);

// The 28 pool tables referenced by gOpponentWaves are already held by
// campaign_catalog (campaign_gun_table_vram/_rom, campaign_gun_table(vram));
// this module resolves through that lookup instead of duplicating values.
// Two further s16[4] at D_80142DD8 {2,10,17,17} and D_80142DE0 {3,13,20,20}
// (ROM 0x0D2DD8/0x0D2DE0) are not referenced by any wave and are not resolved.

// Rebuild time used when the owner does not take profile limits
// (owner +0x1A6 == 0): owner +0x30C difficulty 0 -> D_80072DA4 (ROM 0x002DA4),
// 2 -> D_80072DA8 (ROM 0x002DA8), otherwise D_80072DAC (ROM 0x002DAC).
inline constexpr std::array<float,3> difficulty_rebuild_time{80.0F,100.0F,120.0F};

enum class LookupStatus : std::uint8_t {
    ok=0U,
    null_pointer=1U,     // profile pointer 0 (unused owner slot); retail would read address 0
    outside_pool=2U,     // not one of the campaign_gun_table_count pool tables
};
static_assert(std::is_same_v<std::underlying_type_t<LookupStatus>,std::uint8_t>);

struct GunTableLookup {
    LookupStatus status=LookupStatus::null_pointer;
    GunValues values{};  // zero unless status == ok
};

[[nodiscard]] GunTableLookup find_gun_table(std::uint32_t vram) noexcept;

// initialize_player_record owner +0x318: profile +0x10 when uses_profile_limits
// (owner +0x1A6), else difficulty_rebuild_time by difficulty (owner +0x30C).
// +0x310/+0x314 (limit[0]/[1]) are also written by the same branch and are not
// modeled here; do not read limit[0] from rebuild_time. UNRESOLVED: whether
// owner +0x1A6 (setup "local") means local/human or computer-controlled.
[[nodiscard]] float rebuild_time(bool uses_profile_limits,const CampaignSkillProfile& profile,
                                 std::uint32_t difficulty) noexcept;

// TEMPORARY POLICY: fire delay for a failed fire-rate lookup (never fires in
// practice). Do not assign config to an owner on a non-ok status; log/count it.
inline constexpr GunValues never_fire_delay{0x7FFF,0x7FFF,0x7FFF,0x7FFF};

struct TurretOwnerResult {
    TurretOwnerConfig config{};      // zero damage / never_fire_delay for a failed lookup (TEMPORARY POLICY)
    LookupStatus damage_status=LookupStatus::null_pointer;
    LookupStatus fire_delay_status=LookupStatus::null_pointer;
};

[[nodiscard]] TurretOwnerResult make_turret_owner(const CampaignSkillProfile& profile,
                                                  bool uses_profile_limits,std::uint32_t difficulty) noexcept;

} // namespace battletanx::owner_profiles
