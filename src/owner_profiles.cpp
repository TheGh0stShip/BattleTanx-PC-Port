#include "battletanx/owner_profiles.hpp"

#include <optional>

namespace battletanx::owner_profiles {

GunTableLookup find_gun_table(std::uint32_t vram) noexcept {
    GunTableLookup result;
    if (vram==0U) { result.status=LookupStatus::null_pointer; return result; }
    const std::optional<std::array<std::int16_t,4>> found=campaign_gun_table(vram);
    if (!found) { result.status=LookupStatus::outside_pool; return result; }
    result.status=LookupStatus::ok;
    result.values=*found;
    return result;
}

float rebuild_time(bool uses_profile_limits,const CampaignSkillProfile& profile,std::uint32_t difficulty) noexcept {
    if (uses_profile_limits) return static_cast<float>(profile.gun_resurrection);
    if (difficulty==0U) return difficulty_rebuild_time[0];
    if (difficulty==2U) return difficulty_rebuild_time[2];
    return difficulty_rebuild_time[1];
}

TurretOwnerResult make_turret_owner(const CampaignSkillProfile& profile,bool uses_profile_limits,
                                    std::uint32_t difficulty) noexcept {
    TurretOwnerResult result;
    const GunTableLookup damage=find_gun_table(profile.gun_damage_vram);
    const GunTableLookup delay=find_gun_table(profile.gun_fire_rate_vram);
    result.damage_status=damage.status;
    result.fire_delay_status=delay.status;
    result.config.rebuild_time=rebuild_time(uses_profile_limits,profile,difficulty);
    result.config.effect_kind=damage.values;
    result.config.fire_delay=delay.values;
    // TEMPORARY POLICY: a zero delay table fires every frame; a failed damage or delay lookup
    // gets the maximum s16 delay instead. Callers should not assign the config
    // for a non-ok status at all.
    if (delay.status!=LookupStatus::ok || damage.status!=LookupStatus::ok) result.config.fire_delay=never_fire_delay;
    return result;
}

} // namespace battletanx::owner_profiles
