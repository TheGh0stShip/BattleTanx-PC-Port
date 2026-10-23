#include "battletanx/campaign_ai.hpp"

#include "battletanx/big_endian.hpp"

#include <cmath>
#include <cstdlib>
#include <limits>
#include <type_traits>

namespace battletanx {
namespace {

namespace car=campaign_ai_retail;

float xz_distance(float ax,float az,float bx,float bz) noexcept {
    const float dx=ax-bx;
    const float dz=az-bz;
    return std::sqrt(dx*dx+dz*dz);
}

// (u32)(f32) then stored to a u16 field, as the retail stores do.
std::uint16_t float_to_u16_field(float value) noexcept {
    if (!(value>0.0F)) return 0U;
    if (value>=4294967040.0F) return 0xFFFFU;
    return static_cast<std::uint16_t>(static_cast<std::uint32_t>(value));
}

// func_800E7490: limit = health > 1000 ? 1000 : health*(lo+(hi-lo)*(1-skill)).
std::uint16_t scale_unit_limit(float lo,float hi,float skill,std::int32_t health) noexcept {
    const float span=hi-lo;
    const auto value=static_cast<float>(health);
    const float scale=lo+span*(1.0F-skill);
    if (car::health_clamp<value) return 1000U;
    return float_to_u16_field(value*scale);
}

// func_800E7560: min(max_health, 1000) * p0.
std::uint16_t scale_member_value(float p0,std::int32_t max_health) noexcept {
    float value=static_cast<float>(max_health);
    if (car::health_clamp<value) value=car::health_clamp;
    return float_to_u16_field(value*p0);
}

// func_800E7628.
std::uint16_t close_range_from_skill(float lo,float hi,float skill) noexcept {
    const float span=hi-lo;
    return float_to_u16_field((lo+span*(car::close_range_one-skill))*car::close_range_scale);
}

// func_800E7458: s16 threshold = health - 1.
std::uint16_t health_minus_one(std::int32_t health) noexcept {
    return static_cast<std::uint16_t>(static_cast<std::int16_t>(health-1));
}

// ai_normalized_elapsed_time (func_800E95A8).
float normalized_elapsed(std::int32_t elapsed) noexcept {
    if (elapsed>=car::elapsed_normalizer) return 1.0F;
    return static_cast<float>(elapsed)/static_cast<float>(car::elapsed_normalizer);
}

// ---------------------------------------------------------------------------
// Play scripts (D_80137888, gameplay/ai_script_data.c) and policies consumed
// by choose_weighted_rule (func_800EA93C).

struct Term {
    std::uint16_t key;
    float value;
};

struct Rule {
    std::int8_t result;
    std::uint8_t condition_count;
    std::array<Term,3> conditions;
    std::uint8_t weight_count;
    std::array<Term,2> weights;
};

struct Policy {
    std::uint8_t count;
    std::array<Rule,5> rules;
};

constexpr Term no_term{0U,0.0F};
constexpr Rule rule(std::int8_t result,std::uint8_t nc,std::array<Term,3> c,
                    std::uint8_t nw=0U,std::array<Term,2> w={no_term,no_term}) {
    return Rule{result,nc,c,nw,w};
}
constexpr std::array<Term,3> none3{no_term,no_term,no_term};
// condition_count 0xFF: the record has no condition list (always eligible);
// weight_count 0xFF: no weight list (score 0).
constexpr std::uint8_t absent=0xFFU;

// D_80135F84 (play 4 state 1).
constexpr Policy policy_p04{5U,{{
    rule(2,2U,{{{0x1000U,0.25F},{0x110AU,0.25F},no_term}},1U,{{{0x8103U,1.0F},no_term}}),
    rule(2,1U,{{{0x1001U,1.0F},no_term,no_term}},absent),
    rule(3,absent,none3,2U,{{{0x1000U,0.9F},{0x110BU,0.25F}}}),
    rule(4,absent,none3,2U,{{{0x1000U,0.9F},{0x110BU,0.9F}}}),
    rule(5,absent,none3,2U,{{{0x110AU,0.9F},{0x110BU,0.25F}}}),
}}};
// D_80136078 (play 5 state 2 monitor).
constexpr Policy policy_p05{3U,{{
    rule(3,1U,{{{0x1002U,0.0001F},no_term,no_term}},absent),
    rule(1,1U,{{{0x1005U,0.9F},no_term,no_term}},absent),
    rule(1,1U,{{{0x2111U,0.0001F},no_term,no_term}},absent),
}}};
// D_801360E8 (play 5 state 3 monitor).
constexpr Policy policy_p06{4U,{{
    rule(2,1U,{{{0x1007U,0.9F},no_term,no_term}},absent),
    rule(4,1U,{{{0x1117U,0.0001F},no_term,no_term}},absent),
    rule(1,1U,{{{0x1005U,0.9F},no_term,no_term}},absent),
    rule(1,1U,{{{0x2111U,0.0001F},no_term,no_term}},absent),
}}};
// D_80136164 (play 5 state 4 monitor).
constexpr Policy policy_p07{3U,{{
    rule(1,1U,{{{0x1007U,0.9F},no_term,no_term}},absent),
    rule(5,3U,{{{0x110DU,1.0F},{0x110EU,0.0001F},{0x1116U,0.016F}}},absent),
    rule(1,1U,{{{0x2111U,0.0001F},no_term,no_term}},absent),
}}};
// D_801361CC (play 5 state 5 monitor).
constexpr Policy policy_p08{3U,{{
    rule(6,2U,{{{0x1007U,0.9F},{0x1100U,0.0001F},no_term}},absent),
    rule(1,1U,{{{0x1117U,0.0001F},no_term,no_term}},absent),
    rule(1,1U,{{{0x2111U,0.0001F},no_term,no_term}},absent),
}}};
// D_8013624C (play 6 state 1).
constexpr Policy policy_p09{2U,{{
    rule(3,1U,{{{0x1002U,0.0001F},no_term,no_term}},absent),
    rule(2,absent,none3,absent),
}}};
// D_80136280 / D_801362A8 / D_80136560.
constexpr Policy policy_p10{1U,{{rule(-2,1U,{{{0x1002U,0.0001F},no_term,no_term}},absent)}}};
constexpr Policy policy_p11{1U,{{rule(-2,1U,{{{0x1007U,0.9F},no_term,no_term}},absent)}}};
constexpr Policy policy_p19=policy_p11;
// D_801364E8 / D_8013652C.
constexpr Policy policy_p17{2U,{{
    rule(2,1U,{{{0x1003U,0.0001F},no_term,no_term}},absent),
    rule(-2,1U,{{{0x1005U,0.9F},no_term,no_term}},absent),
}}};
constexpr Policy policy_p18=policy_p17;

// Policies used by plays 1, 3, 8, 13, 16, 19..23 (decoded from gAiScriptData;
// all requirement thresholds are 0.0001 unless written).
constexpr Term t(std::uint16_t key,float value=0.0001F) { return Term{key,value}; }
constexpr std::array<Term,3> c1(Term a) { return {a,no_term,no_term}; }
// D_80135D80 (play 1 state 1).
constexpr Policy policy_d80{2U,{{rule(3,1U,c1(t(0x1002U))),rule(2,absent,none3)}}};
// D_80135DB4 / D_80135DDC (play 1 monitors).
constexpr Policy policy_db4{1U,{{rule(3,1U,c1(t(0x1002U)))}}};
constexpr Policy policy_ddc{1U,{{rule(2,1U,c1(t(0x1007U,0.9F)))}}};
// D_80135E64 (play 3 state 1).
constexpr Policy policy_e64{1U,{{rule(2,1U,c1(t(0x1002U)))}}};
// D_80136390 / D_801363E0 / D_80136424 / D_80136458 / D_80136480 (play 8).
constexpr Policy policy_390{3U,{{rule(5,1U,c1(t(0x1110U))),rule(4,1U,c1(t(0x1113U))),rule(2,absent,none3)}}};
constexpr Policy policy_3e0{2U,{{rule(5,1U,c1(t(0x1110U))),rule(3,1U,c1(t(0x1113U)))}}};
constexpr Policy policy_424{2U,{{rule(5,1U,c1(t(0x1110U))),rule(7,1U,c1(t(0x2117U,1.0F)))}}};
constexpr Policy policy_458{1U,{{rule(1,1U,c1(t(0x2116U)))}}};
constexpr Policy policy_480{1U,{{rule(7,1U,c1(t(0x1111U,1.0F)))}}};
// D_80136658 / D_801366A8 / D_801366DC (play 13).
constexpr Policy policy_658{3U,{{rule(4,1U,c1(t(0x2113U,1.0F))),rule(3,1U,c1(t(0x1113U))),rule(2,absent,none3)}}};
constexpr Policy policy_6a8{2U,{{rule(4,1U,c1(t(0x2113U,1.0F))),rule(3,1U,c1(t(0x1113U)))}}};
constexpr Policy policy_6dc{1U,{{rule(4,1U,c1(t(0x2113U,1.0F)))}}};
// D_801370E8 (play 16).
constexpr Policy policy_0e8{1U,{{rule(3,1U,c1(t(0x111BU,1.0F)))}}};
// D_801371D0 / D_80137210 / D_80137248 / D_8013728C (play 19).
constexpr Policy policy_1d0{3U,{{rule(2,1U,c1(t(0x1105U))),rule(3,1U,c1(t(0x1106U))),rule(4,absent,none3)}}};
constexpr Policy policy_210{1U,{{rule(1,1U,c1(t(0x1104U)))}}};
constexpr Policy policy_248{2U,{{rule(1,1U,c1(t(0x1105U))),rule(1,1U,c1(t(0x1107U)))}}};
constexpr Policy policy_28c{2U,{{rule(1,1U,c1(t(0x1105U))),rule(1,1U,c1(t(0x1106U)))}}};
// D_80137310 / D_80137354 / D_80137398 (play 20).
constexpr Policy policy_310{2U,{{rule(2,1U,c1(t(0x1109U))),rule(3,absent,none3)}}};
constexpr Policy policy_354{2U,{{rule(4,1U,c1(t(0x1102U,0.9F))),rule(3,1U,c1(t(0x1108U)))}}};
constexpr Policy policy_398{2U,{{rule(4,1U,c1(t(0x1103U,0.9F))),rule(2,1U,c1(t(0x1109U)))}}};
// D_8013741C / D_80137460 / D_801374A4 (play 21).
constexpr Policy policy_41c{2U,{{rule(2,1U,c1(t(0x1108U))),rule(3,absent,none3)}}};
constexpr Policy policy_460{2U,{{rule(4,1U,c1(t(0x1102U,0.9F))),rule(3,1U,c1(t(0x1109U)))}}};
constexpr Policy policy_4a4{2U,{{rule(4,1U,c1(t(0x1103U,0.9F))),rule(2,1U,c1(t(0x1108U)))}}};
// D_80137518 / D_8013755C / D_801375A0 (play 22).
constexpr Policy policy_518{2U,{{rule(2,1U,c1(t(0x8100U,0.5F))),rule(3,absent,none3)}}};
constexpr Policy policy_55c{2U,{{rule(3,1U,c1(t(0x1102U,0.9F))),rule(3,1U,c1(t(0x210EU,1.0F)))}}};
constexpr Policy policy_5a0{2U,{{rule(2,1U,c1(t(0x1103U,0.9F))),rule(2,1U,c1(t(0x210EU,1.0F)))}}};
// D_80137614 / D_80137648 / D_80137670 (play 23).
constexpr Policy policy_614{2U,{{rule(3,1U,c1(t(0x1002U))),rule(2,absent,none3)}}};
constexpr Policy policy_648{1U,{{rule(-2,1U,c1(t(0x1002U)))}}};
constexpr Policy policy_670{1U,{{rule(-2,1U,c1(t(0x1007U,0.9F)))}}};

// Retail step functions referenced by member-reachable scripts. Each value
// names the routine stored in the AiPlayStep run/monitor word.
enum class Fn : std::uint8_t {
    none,
    f_EABB0, f_EABFC, f_EAC48, f_EAC80, f_EACB4,               // play 1
    f_EAD4C,                                                    // play 3
    f_EAD74, f_EAD9C, f_EAE30, f_EAEA8, f_EAF14, f_EAF6C,       // play 4
    f_EAFC4, f_EAFE4, f_EB00C, f_EB02C, f_EB054, f_EB07C, f_EB0E8, f_EB110, // play 5
    f_EB130, f_EB158, f_EB178, f_EB1A0, f_EB1C0,               // play 6
    f_EB248, f_EB2A4, f_EB2CC, f_EB310, f_EB338, f_EB360, f_EB394, f_EB3BC, // play 8
    f_EB3F0, f_EB418, f_EB440, f_EB47C,                         // play 9
    f_EB504,                                                    // play 11
    f_EB554, f_EB5A0, f_EB5C0, f_EB5E8, f_EB640, f_EB668,       // play 13
    f_EBAE0, f_EBB58, f_EBB80,                                  // play 16
    f_EBBB4,                                                    // play 17
    f_EBC1C, f_EBC44, f_EBC6C, f_EBC94,                         // play 19
    f_EBCBC, f_EBCE4, f_EBD04, f_EBD2C, f_EBD4C, f_EBD74,       // play 20
    f_EBDB4, f_EBDDC, f_EBDFC, f_EBE24, f_EBE44, f_EBE6C,       // play 21
    f_EBEAC, f_EBED4, f_EBF28, f_EBF50, f_EBFA4,                // play 22
    f_EBFCC, f_EBFF4, f_EC014, f_EC03C, f_EC05C,                // play 23
    f_EC35C,                                                    // play 27
};
static_assert(std::is_same_v<std::underlying_type_t<Fn>,std::uint8_t>);

struct Step {
    Fn run;
    Fn monitor;
    std::uint8_t play;
    std::int8_t next;
};

constexpr Step hdr(std::uint8_t play) { return Step{Fn::none,Fn::none,play,1}; }
constexpr Step st(Fn run,Fn monitor,std::uint8_t play,std::int8_t next) { return Step{run,monitor,play,next}; }
constexpr Fn N=Fn::none;

// Scripts (gAiScriptData, D_80137888 table at ROM 0xC7888). Index 0 is the
// header {play, initial state}; no header carries a run or monitor routine.
constexpr std::array<Step,4> play1{{hdr(1U),st(Fn::f_EABB0,N,0U,-1),
    st(Fn::f_EABFC,Fn::f_EAC48,19U,-1),st(Fn::f_EAC80,Fn::f_EACB4,19U,-1)}};
constexpr std::array<Step,3> play3{{hdr(3U),st(Fn::f_EAD4C,N,0U,-1),st(N,N,5U,-1)}};
constexpr std::array<Step,7> play4{{hdr(4U),st(Fn::f_EAD74,N,0U,-1),
    st(Fn::f_EAD9C,N,0U,-2),st(Fn::f_EAE30,N,0U,-2),st(Fn::f_EAEA8,N,0U,-2),
    st(Fn::f_EAF14,N,0U,-2),st(Fn::f_EAF6C,N,0U,-2)}};
constexpr std::array<Step,7> play5{{hdr(5U),st(Fn::f_EAFC4,N,4U,2),st(N,Fn::f_EAFE4,6U,1),
    st(Fn::f_EB00C,Fn::f_EB02C,6U,1),st(N,Fn::f_EB054,6U,1),st(Fn::f_EB07C,Fn::f_EB0E8,6U,1),
    st(Fn::f_EB110,N,0U,1)}};
constexpr std::array<Step,4> play6{{hdr(6U),st(Fn::f_EB130,N,0U,-1),
    st(Fn::f_EB158,Fn::f_EB178,9U,-2),st(Fn::f_EB1A0,Fn::f_EB1C0,9U,-2)}};
constexpr std::array<Step,8> play8{{hdr(8U),st(Fn::f_EB248,N,0U,-1),st(N,Fn::f_EB2A4,6U,1),
    st(Fn::f_EB2CC,N,0U,4),st(N,Fn::f_EB310,6U,1),st(Fn::f_EB338,N,0U,6),
    st(Fn::f_EB360,Fn::f_EB394,6U,1),st(Fn::f_EB3BC,N,5U,-1)}};
constexpr std::array<Step,3> play9{{hdr(9U),st(Fn::f_EB3F0,Fn::f_EB418,12U,-2),
    st(Fn::f_EB440,Fn::f_EB47C,12U,-1)}};
constexpr std::array<Step,2> play11{{hdr(11U),st(Fn::f_EB504,N,23U,-1)}};
constexpr std::array<Step,2> play12{{hdr(12U),st(N,N,0U,1)}};
constexpr std::array<Step,5> play13{{hdr(13U),st(Fn::f_EB554,N,0U,-1),
    st(Fn::f_EB5A0,Fn::f_EB5C0,6U,1),st(Fn::f_EB5E8,Fn::f_EB640,12U,1),st(Fn::f_EB668,N,5U,-1)}};
constexpr std::array<Step,4> play16{{hdr(16U),st(Fn::f_EBAE0,N,0U,2),st(N,Fn::f_EBB58,27U,-1),
    st(Fn::f_EBB80,N,0U,-1)}};
constexpr std::array<Step,3> play17{{hdr(17U),st(Fn::f_EBBB4,N,0U,2),st(N,N,5U,-1)}};
constexpr std::array<Step,5> play19{{hdr(19U),st(Fn::f_EBC1C,N,0U,-1),st(N,Fn::f_EBC44,22U,-1),
    st(N,Fn::f_EBC6C,20U,-2),st(N,Fn::f_EBC94,21U,-2)}};
constexpr std::array<Step,5> play20{{hdr(20U),st(Fn::f_EBCBC,N,0U,-1),
    st(Fn::f_EBCE4,Fn::f_EBD04,12U,-1),st(Fn::f_EBD2C,Fn::f_EBD4C,12U,-1),st(Fn::f_EBD74,N,0U,-2)}};
constexpr std::array<Step,5> play21{{hdr(21U),st(Fn::f_EBDB4,N,0U,-1),
    st(Fn::f_EBDDC,Fn::f_EBDFC,12U,-1),st(Fn::f_EBE24,Fn::f_EBE44,12U,-1),st(Fn::f_EBE6C,N,0U,-2)}};
constexpr std::array<Step,4> play22{{hdr(22U),st(Fn::f_EBEAC,N,0U,-1),
    st(Fn::f_EBED4,Fn::f_EBF28,12U,-1),st(Fn::f_EBF50,Fn::f_EBFA4,12U,-1)}};
constexpr std::array<Step,4> play23{{hdr(23U),st(Fn::f_EBFCC,N,0U,-1),
    st(Fn::f_EBFF4,Fn::f_EC014,12U,-2),st(Fn::f_EC03C,Fn::f_EC05C,12U,-2)}};
constexpr std::array<Step,3> play27{{hdr(27U),st(Fn::f_EC35C,N,0U,2),st(N,N,6U,1)}};
constexpr std::array<Step,2> play28{{hdr(28U),st(N,N,12U,-1)}};

std::span<const Step> script_of(std::uint8_t play) noexcept {
    switch (play) {
    case 1U: return play1;
    case 3U: return play3;
    case 4U: return play4;
    case 5U: return play5;
    case 6U: return play6;
    case 8U: return play8;
    case 9U: return play9;
    case 11U: return play11;
    case 12U: return play12;
    case 13U: return play13;
    case 16U: return play16;
    case 17U: return play17;
    case 19U: return play19;
    case 20U: return play20;
    case 21U: return play21;
    case 22U: return play22;
    case 23U: return play23;
    case 27U: return play27;
    case 28U: return play28;
    default: return {};
    }
}

// Retail constants for the member action helpers.
constexpr float heading_param_scale=1800.0F;   // func_800E75D4 D_80074704
constexpr float action_value_scale=100.0F;     // func_800E7064 D_800746D8
constexpr float deadline_seconds_scale=30.0F;  // func_800E73E0 D_800746E8
constexpr float deadline_divisor=0.003333F;    // func_800E73E0 D_800746EC
constexpr float metric_level_divisor=255.0F;   // func_800E7168 D_800746E0
constexpr float metric_random_range=1.0F;      // func_800E7168 D_800746E4
constexpr float timer_random_range=1.0F;       // func_800E70DC D_800746DC
constexpr float keep_target_metric=600.0F;     // func_800E6D58 D_800746C8
constexpr float search_metric=16000.0F;        // func_800E6D58 D_800746CC
constexpr float segment_slack=1.2F;            // func_800E6D58 D_800746D0
constexpr float deadline_half=0.5F;            // func_800E96C0 D_800748A4
constexpr float deadline_full=1.0F;            // func_800E96C0 D_800748A8


} // namespace

// ---------------------------------------------------------------------------
// Difficulty.

std::optional<std::uint32_t> ai_profile_offset(std::uint8_t difficulty,std::uint16_t row) noexcept {
    if (difficulty>=car::difficulty_levels || row>=car::profile_rows) return std::nullopt;
    return car::profile_table_offset+car::profile_difficulty_stride*difficulty+
           car::profile_bytes*row;
}

AiProfile decode_ai_profile(std::span<const std::uint8_t,car::profile_bytes> bytes) noexcept {
    const BigEndianView view{std::span<const std::uint8_t>{bytes.data(),bytes.size()}};
    AiProfile profile{};
    profile.skill_a=view.f32(0x00U);
    profile.skill_b=view.f32(0x04U);
    profile.speed=view.u8(0x1CU);
    profile.rate_primary=view.u8(0x1DU);
    profile.rate_class4=view.u8(0x1EU);
    profile.rate_kind2=view.u8(0x1FU);
    profile.value_32c=view.u8(0x20U);
    return profile;
}

OwnerDifficulty owner_difficulty_from_profile(const AiProfile& profile) noexcept {
    OwnerDifficulty owner{};
    owner.skill_a=profile.skill_a;
    owner.skill_b=profile.skill_b;
    owner.skill_a_scaled=profile.skill_a*car::owner_skill_scale;
    owner.skill_b_scaled=profile.skill_b*car::owner_skill_scale;
    owner.speed=static_cast<float>(profile.speed);
    owner.rate_primary=static_cast<float>(profile.rate_primary);
    owner.rate_class4=static_cast<float>(profile.rate_class4);
    owner.rate_kind2=static_cast<float>(profile.rate_kind2);
    owner.value_32c=static_cast<float>(profile.value_32c);
    return owner;
}

void OwnerDifficulty::serialize(StateWriter& writer) const {
    writer.f32(skill_a); writer.f32(skill_b);
    writer.f32(skill_a_scaled); writer.f32(skill_b_scaled);
    writer.f32(speed); writer.f32(rate_primary); writer.f32(rate_class4);
    writer.f32(rate_kind2); writer.f32(value_32c);
}

float roll_unit_skill(float owner_skill,RetailRandom& rng) noexcept {
    // random_unit_average_pair (func_800F3940).
    const float first=rng.next_float(1.0F);
    const float second=rng.next_float(1.0F);
    const float average=(first+second)*0.5F;
    return owner_skill*car::skill_owner_weight+average*car::skill_noise_weight;
}

std::int32_t fire_cooldown_ticks(float rate) noexcept {
    const float cool=rate*car::fire_rate_scale/car::fire_rate_divisor;
    if (normalized_elapsed(car::elapsed_normalizer)<cool) return std::numeric_limits<std::int32_t>::max();
    if (!(cool>0.0F)) return 0;
    auto elapsed=static_cast<std::int32_t>(cool*static_cast<float>(car::elapsed_normalizer));
    if (elapsed>car::elapsed_normalizer) elapsed=car::elapsed_normalizer;
    while (elapsed>0 && !(normalized_elapsed(elapsed-1)<cool)) --elapsed;
    while (normalized_elapsed(elapsed)<cool) ++elapsed;
    return elapsed;
}

UnitThresholds apply_engagement_profile(EngagementProfile profile,float skill,std::int32_t health,
                                        std::int32_t max_health) noexcept {
    UnitThresholds out{};
    switch (profile) {
    case EngagementProfile::state2:
        out.health_threshold=scale_unit_limit(car::profile2_lo,car::profile2_hi,skill,health);
        out.close_range=close_range_from_skill(car::profile2_lo,car::profile2_hi,skill);
        break;
    case EngagementProfile::state3:
        out.health_threshold=scale_member_value(car::profile3_scale,max_health);
        out.close_range=close_range_from_skill(car::profile3_scale,0.5F,skill);
        break;
    case EngagementProfile::state4:
        out.health_threshold=scale_member_value(0.0F,max_health);
        out.close_range=close_range_from_skill(0.1F,0.25F,skill);
        break;
    case EngagementProfile::state5:
        out.health_threshold=health_minus_one(health);
        out.close_range=close_range_from_skill(0.5F,0.9F,skill);
        break;
    case EngagementProfile::state6:
        out.health_threshold=health_minus_one(health);
        out.close_range=close_range_from_skill(0.75F,0.9F,skill);
        break;
    }
    return out;
}

// ---------------------------------------------------------------------------
// Perception.

std::size_t perceive_candidates(const UnitPool& units,EntityId self,std::int32_t tick,
                                PerceptionPolicy policy,std::span<CandidatePerception> out) {
    const UnitRecord* me=units.get(self);
    if (me==nullptr) return 0U;
    std::size_t count=0U;
    switch (policy) {
    case PerceptionPolicy::opposing_units_linked_temporary:
        for (std::size_t slot=0U; slot<unit_capacity && count<out.size(); ++slot) {
            const EntityId id=units.slot_id(slot);
            if (!id.valid() || id==self) continue;
            const UnitRecord& unit=*units.get(id);
            if (unit.team==me->team) continue;
            CandidatePerception& c=out[count++];
            c=CandidatePerception{};
            c.unit=id;
            c.active=unit.alive;
            c.targetable=unit_is_targetable(unit,tick);
            c.linked=unit.alive;
            c.idle_mode=unit.control==UnitControl::player;
            c.class4=(unit.flags&enemy_retail::flag_ai_class4)!=0U;
            c.link_metric=xz_distance(me->x,me->z,unit.x,unit.z);
        }
        break;
    }
    return count;
}

// ---------------------------------------------------------------------------
// Scoring.

float candidate_chain_weight(std::uint8_t chain_length) noexcept {
    float weight=0.0F;
    for (std::uint8_t i=0U; i<chain_length; ++i) weight+=car::chain_step;
    return weight;
}

float score_candidate(const CandidatePerception& c,const ScoreWeights& weights,
                      bool owner_computer) noexcept {
    const auto& w=weights.values;
    float score=0.0F;
    if (c.linked && c.targetable) score=w[1]+score;
    score+=w[2]*candidate_chain_weight(c.chain_length);
    if (c.idle_mode) score+=w[4];
    if (c.class4) score+=w[3];
    score+=w[5]*c.link_metric*car::score_metric_scale;
    if (c.class4 && !owner_computer) score=0.0F;
    return score;
}

ScoredTarget select_best_scored(std::span<const CandidatePerception> candidates,
                                const ScoreWeights& weights,bool owner_computer) noexcept {
    ScoredTarget best{};
    best.score=weights.values[0]-car::score_epsilon;
    for (const CandidatePerception& c : candidates) {
        if (!c.active) continue;
        const float score=score_candidate(c,weights,owner_computer);
        if (best.score<score) {
            best.unit=c.unit;
            best.score=score;
            best.found=true;
        }
    }
    return best;
}

// ---------------------------------------------------------------------------
// Goals and play stack.

void AiGoal::serialize(StateWriter& writer) const {
    writer.u8(static_cast<std::uint8_t>(mode));
    writer.id(target);
    writer.f32(x); writer.f32(z);
    writer.boolean(has_point);
}

void PlayStack::serialize(StateWriter& writer) const {
    writer.u8(depth);
    for (std::uint8_t i=0U; i<depth; ++i) {
        writer.u8(frames[i].play);
        writer.u8(static_cast<std::uint8_t>(frames[i].state));
    }
}

std::uint8_t select_member_root_play(std::uint8_t unit_class,bool class4,bool status_one) noexcept {
    switch (unit_class) {
    case 0x42U: case 0x46U: return 3U;
    case 0x41U: case 0x43U: case 0x45U: return 5U;
    default: break;
    }
    if (class4) return 1U;
    if (status_one) return 0x0BU;
    return 5U;
}

bool play_supported(std::uint8_t play) noexcept { return !script_of(play).empty(); }

PlayScope play_scope(std::uint8_t play) noexcept {
    switch (play) {
    // Member roots (func_800EA2A4 kind 2): 1, 3, 5, 11. Role tables add 8, 13,
    // 16, 17, 28. Pushed by member scripts: 4, 6, 9, 12, 19..23, 27.
    case 1U: case 3U: case 4U: case 5U: case 6U: case 8U: case 9U: case 11U: case 12U:
    case 13U: case 16U: case 17U: case 19U: case 20U: case 21U: case 22U: case 23U:
    case 27U: case 28U:
        return PlayScope::member;
    // Owner roots (kind 1): 2, 10, 12, 15, 25, 26; play 15 pushes 14; play 7
    // only assigns squad plays (func_800E6FD8). 12 is shared and listed above.
    case 2U: case 7U: case 10U: case 14U: case 15U: case 25U: case 26U:
        return PlayScope::owner;
    // 18 and 24: no root, role-table entry or script step names them.
    case 18U: case 24U:
        return PlayScope::unreachable;
    default:
        return PlayScope::invalid;
    }
}

namespace {

struct PlayContext {
    CampaignAiUnitState& state;
    const UnitRecord& unit;
    const UnitPool& units;
    std::span<const CandidatePerception> candidates;
    const ExternalParameters& external;
    bool owner_computer;
    CampaignAiCommand& command;
    MemberOwnerFields& owner;
    const OwnerDifficulty& difficulty;
    const CampaignAiConfig& config;
    RetailRandom& rng;
    std::int32_t tick;
    PlayStatus status{PlayStatus::running};
    std::uint8_t unsupported{};
    // Action parameter list (context +0x20.., count +0x40; func_800E9A00 /
    // func_800E99D4).
    std::array<float,4> params{};
    std::uint8_t param_count{};

    void push_param(float value) noexcept { if (param_count<params.size()) params[param_count++]=value; }
    void clear_params() noexcept { param_count=0U; }

    // Linked unit of owner +0x1A8: STRONG INFERENCE, the objective owner's
    // primary unit (func_800A03B8 indexes units by the same small id).
    [[nodiscard]] const UnitRecord* linked_unit() const noexcept {
        if (owner.objective>=owner_capacity) return nullptr;
        for (std::size_t slot=0U; slot<unit_capacity; ++slot) {
            const EntityId id=units.slot_id(slot);
            if (!id.valid()) continue;
            const UnitRecord* u=units.get(id);
            if (u!=nullptr && u->primary && u->owner==owner.objective) return u;
        }
        return nullptr;
    }

    [[nodiscard]] float parameter(std::uint16_t key) noexcept {
        switch (key) {
        case 0x1000U: return state.skill0;
        case 0x1001U: return state.skill1;
        case 0x1002U: return state.contact;
        case 0x1003U: return external.p1003_hold;
        case 0x1004U: return state.move_near;
        case 0x1005U: return state.move_far;
        case 0x1006U: return state.aim_near;
        case 0x1007U: return state.aim_far;
        case 0x1100U: return external.p1100;
        case 0x1102U: return external.p1102;
        case 0x1103U: return external.p1103;
        // func_800E8E5C / func_800E8E84: owner +0x1A8 != 0xFF.
        case 0x1104U: return owner.objective!=0xFFU ? 1.0F : 0.0F;
        case 0x1105U: return owner.objective==0xFFU ? 1.0F : 0.0F;
        // func_800E8EB4 / func_800E8F1C: linked unit team vs context team (weights 1.0).
        case 0x1106U: { const UnitRecord* u=linked_unit(); return u!=nullptr && u->team==unit.team ? 1.0F : 0.0F; }
        case 0x1107U: { const UnitRecord* u=linked_unit(); return u!=nullptr && u->team!=unit.team ? 1.0F : 0.0F; }
        case 0x1108U: return external.p1108;
        case 0x1109U: return external.p1109;
        case 0x110AU: return external.p110A;
        case 0x110BU: return external.p110B;
        case 0x110DU: return external.p110D;
        case 0x110EU: return external.p110E;
        case 0x1110U: return external.p1110;
        case 0x1111U: return external.p1111;
        case 0x1113U: return external.p1113;
        case 0x1116U: return external.p1116;
        case 0x1117U: return external.p1117;
        // func_800E96C0: unit +0x360 deadline: 0 unset, 0.5 pending, 1.0 elapsed.
        case 0x111BU:
            if (state.deadline==0) return 0.0F;
            return tick>=state.deadline ? deadline_full : deadline_half;
        case 0x210EU: return external.p210E;
        case 0x2111U: return external.p2111;
        case 0x2113U: return external.p2113;
        case 0x2116U: return external.p2116;
        case 0x2117U: return external.p2117;
        // func_800E7734: random_float_range(1.0); one generator step per read.
        case 0x8100U: return rng.next_float(1.0F);
        case 0x8103U: return external.p8103;
        default: return 0.0F;
        }
    }

    // choose_weighted_rule (func_800EA93C), including the retail loop bound
    // that never scores the last eligible rule when two or more are eligible.
    [[nodiscard]] std::int32_t choose(const Policy& policy) noexcept {
        std::array<bool,5> eligible{};
        std::uint16_t last=0U;
        std::uint16_t count=0U;
        for (std::uint16_t i=0U; i<policy.count; ++i) {
            const Rule& r=policy.rules[i];
            bool ok=true;
            if (r.condition_count!=absent) {
                for (std::uint8_t k=0U; k<r.condition_count; ++k) {
                    if (parameter(r.conditions[k].key)<r.conditions[k].value) { ok=false; break; }
                }
            }
            eligible[i]=ok;
            if (ok) { ++count; last=i; }
        }
        if (count==0U) return -1;
        if (count==1U) return policy.rules[last].result;
        std::uint16_t best=0U;
        float best_score=0.0F;
        bool found=false;
        for (std::uint16_t i=0U; i<last; ++i) {
            if (!eligible[i]) continue;
            const Rule& r=policy.rules[i];
            float score=0.0F;
            if (r.weight_count!=absent) {
                for (std::uint8_t k=0U; k<r.weight_count; ++k) {
                    score+=parameter(r.weights[k].key)*r.weights[k].value;
                }
            }
            if (!found || best_score<score) { best=i; best_score=score; found=true; }
        }
        return found ? policy.rules[best].result : -1;
    }

    void set_profile(EngagementProfile profile) noexcept {
        state.thresholds=apply_engagement_profile(profile,state.skill0,unit.health,unit.max_health);
    }

    // func_800E75D4: unit +0x35E = (u32)(params[0]*1800).
    void heading_from_param() noexcept {
        state.thresholds.close_range=float_to_u16_field(params[0]*heading_param_scale);
    }
    // func_800E7064: owner +0x1A0 from the parameter list.
    void owner_value_from_param() noexcept {
        if (param_count==0U) owner.action_value=difficulty.value_32c;
        else if (param_count==1U) owner.action_value=params[0]*action_value_scale;
    }
    // func_800E3980 enabled=1 (move goal mode 6) toward an owner-record point.
    void move_to(const std::optional<std::array<float,2>>& point) noexcept {
        state.route.reset();
        if (!point) { state.move=AiGoal{GoalMode::point,no_entity,0.0F,0.0F,false}; return; }
        state.move=AiGoal{GoalMode::point,no_entity,(*point)[0],(*point)[1],true};
    }
    // func_800E3480: clear the move goal.
    void reset_move() noexcept { state.move=AiGoal{}; state.route.reset(); }
    // func_800E6C5C -> func_800E4A18.
    void release_owner() noexcept { owner.release_pending=true; command.owner_release_request=true; }
    // func_800E7168.
    void random_metric(std::uint8_t metric) noexcept {
        if (config.difficulty_level>=2U) return;
        float chance=0.0F;
        if (param_count==0U) {
            std::uint8_t level=0U;
            if (metric==8U) level=config.metric_levels[0];
            else if (metric==3U) level=config.metric_levels[1];
            else if (metric==2U) level=config.metric_levels[2];
            chance=static_cast<float>(level)/metric_level_divisor;
        } else if (param_count==1U) {
            chance=params[0];
        }
        if (!(rng.next_float(metric_random_range)<chance)) return;
        if (metric!=8U) {
            command.ammo_grant_slot=metric;
            state.last_metric=metric;
        } else {
            command.marker_request=true;
        }
    }
    // func_800E70DC: start = p0 + now; end = start + seconds_to_ticks((p1-p0)*rand).
    void schedule_owner_timer() noexcept {
        const auto lo=static_cast<std::int32_t>(params[0]);
        const auto span=static_cast<std::int32_t>(params[1])-lo;
        const std::int32_t start=lo+tick;
        const float seconds=static_cast<float>(span)*rng.next_float(timer_random_range);
        owner.timer_start=start;
        owner.timer_end=start+static_cast<std::int32_t>(seconds*car::ticks_per_second);
    }
    // func_800E6B3C (ai_step_select_target_unit). The keep-route-target
    // branch (kind 0x1B objects) is not ported: keep is always 0.
    void resolve_target() noexcept {
        const ScoredTarget r=select_best_scored(candidates,target_score_weights,owner_computer);
        if (!r.found) state.contact=0.0F;
        else state.aim=AiGoal{GoalMode::unit,r.unit,0.0F,0.0F,false};
    }
    // func_800E6D58: owner +0x1AA = nearest linked candidate inside the
    // member's route-segment ellipse; the current aim target is kept while its
    // link metric is under 600.
    void select_owner_target_member() noexcept {
        if (state.aim.mode==GoalMode::unit) {
            for (const CandidatePerception& c : candidates) {
                if (c.unit==state.aim.target && c.link_metric<keep_target_metric) {
                    owner.target_member=static_cast<std::uint8_t>(c.unit.slot);
                    return;
                }
            }
        }
        const float sx=external.segment_x, sz=external.segment_z;
        const float ex=sx+external.segment_dx*external.segment_length;
        const float ez=sz+external.segment_dz*external.segment_length;
        float best_metric=search_metric;
        std::uint8_t best=0xFFU;
        for (const CandidatePerception& c : candidates) {
            if (!c.linked || !(c.link_metric<best_metric)) continue;
            const UnitRecord* u=units.get(c.unit);
            if (u==nullptr) continue;
            const float d=xz_distance(u->x,u->z,sx,sz)+xz_distance(u->x,u->z,ex,ez);
            if (d<external.segment_length*segment_slack) {
                best=static_cast<std::uint8_t>(c.unit.slot);
                best_metric=c.link_metric;
            }
        }
        // The carrier fallback (unit +0xB4 == 1, +0xC4) is not ported.
        owner.target_member=best;
    }

    std::int32_t call(Fn fn) noexcept {
        switch (fn) {
        case Fn::none: return -1;
        // Play 1.
        case Fn::f_EABB0:
            push_param(0.0F); heading_from_param(); clear_params();
            return choose(policy_d80);
        case Fn::f_EABFC:
            state.aim=AiGoal{};                      // func_800E6BF0
            push_param(0.2F); owner_value_from_param(); clear_params();
            return -1;
        case Fn::f_EAC48: select_owner_target_member(); return choose(policy_db4);
        case Fn::f_EAC80: resolve_target(); owner_value_from_param(); return -1;
        case Fn::f_EACB4: select_owner_target_member(); return choose(policy_ddc);
        // Play 3.
        case Fn::f_EAD4C: return choose(policy_e64);
        // Play 4.
        case Fn::f_EAD74: return choose(policy_p04);
        case Fn::f_EAD9C: set_profile(EngagementProfile::state2); return -1;
        case Fn::f_EAE30: set_profile(EngagementProfile::state3); return -1;
        case Fn::f_EAEA8: set_profile(EngagementProfile::state4); return -1;
        case Fn::f_EAF14: set_profile(EngagementProfile::state5); return -1;
        case Fn::f_EAF6C: set_profile(EngagementProfile::state6); return -1;
        // Play 5.
        case Fn::f_EAFC4: {
            const ScoredTarget r=select_best_scored(candidates,secondary_score_weights,owner_computer);
            if (r.found) state.owner_suggestion=r.unit;
            return -1;
        }
        case Fn::f_EAFE4: return choose(policy_p05);
        case Fn::f_EB00C: resolve_target(); return -1;
        case Fn::f_EB02C: return choose(policy_p06);
        case Fn::f_EB054: return choose(policy_p07);
        case Fn::f_EB07C: state.move=AiGoal{}; return -1;
        case Fn::f_EB0E8: return choose(policy_p08);
        case Fn::f_EB110: command.owner_command_cleared=true; return -1;
        // Play 6.
        case Fn::f_EB130: return choose(policy_p09);
        case Fn::f_EB158: state.aim=AiGoal{}; return -1;
        case Fn::f_EB178: return choose(policy_p10);
        case Fn::f_EB1A0: resolve_target(); return -1;
        case Fn::f_EB1C0: return choose(policy_p11);
        // Play 8 (attack the owner objective).
        case Fn::f_EB248:
            state.aim=AiGoal{};                      // func_800E6BF0
            move_to(owner.objective_point);          // func_800E6970
            push_param(0.0F); heading_from_param(); clear_params();
            return choose(policy_390);
        case Fn::f_EB2A4: return choose(policy_3e0);
        case Fn::f_EB2CC:
            random_metric(8U); random_metric(3U); random_metric(2U);
            state.flag_2ba=true;                     // func_800E7380
            return -1;
        case Fn::f_EB310: return choose(policy_424);
        case Fn::f_EB338: return choose(policy_458);
        case Fn::f_EB360: move_to(owner.anchor); return -1; // func_800E69A4 + func_800E6C80
        case Fn::f_EB394: return choose(policy_480);
        case Fn::f_EB3BC: reset_move(); release_owner(); return -1;
        // Play 9.
        case Fn::f_EB3F0: return choose(policy_p17);
        case Fn::f_EB418: return choose(policy_p18);
        case Fn::f_EB440: command.owner_event=true; state.move=AiGoal{}; return -1;
        case Fn::f_EB47C: return choose(policy_p19);
        // Play 11.
        case Fn::f_EB504:
            push_param(0.0F); heading_from_param(); clear_params();
            reset_move();
            state.flag_2ba=false;                    // func_800E7394
            return -1;
        // Play 13 (capture/hold the owner objective).
        case Fn::f_EB554:
            push_param(100.0F);
            state.thresholds.close_range=float_to_u16_field(params[0]); // func_800E76D0
            clear_params();
            return choose(policy_658);
        case Fn::f_EB5A0: move_to(owner.objective_point); return -1;
        case Fn::f_EB5C0: return choose(policy_6a8);
        case Fn::f_EB5E8:
            push_param(0.5F); random_metric(8U); clear_params();
            random_metric(2U);
            state.flag_2ba=true;
            // func_800E6BBC: aim goal (enabled=0, mode 2) at the objective.
            if (owner.objective_point) {
                state.aim=AiGoal{GoalMode::object_list,no_entity,(*owner.objective_point)[0],
                                 (*owner.objective_point)[1],true};
            } else {
                state.aim=AiGoal{GoalMode::object_list,no_entity,0.0F,0.0F,false};
            }
            return -1;
        case Fn::f_EB640: return choose(policy_6dc);
        case Fn::f_EB668: reset_move(); release_owner(); return -1;
        // Play 16 (timed return to the owner, then self-destruct).
        case Fn::f_EBAE0: {
            command.owner_linked_value_cleared=true; // func_800E73A4
            push_param(0.0F); heading_from_param(); clear_params();
            move_to(owner.anchor);                   // func_800E69A4
            // func_800E6C80 (goal refresh) runs in the controller each tick.
            push_param(0.39996F);
            const auto delay=static_cast<std::int32_t>(params[0]*deadline_seconds_scale/deadline_divisor);
            state.deadline=tick+delay;               // func_800E73E0
            clear_params();
            return -1;
        }
        case Fn::f_EBB58: return choose(policy_0e8);
        case Fn::f_EBB80:
            state.deadline=0;                        // func_800E7448
            command.self_destruct=true;              // func_800E73BC -> func_800E4494
            return -1;
        // Play 17.
        case Fn::f_EBBB4: return -1;
        // Play 19 (escort dispatcher).
        case Fn::f_EBC1C: return choose(policy_1d0);
        case Fn::f_EBC44: return choose(policy_210);
        case Fn::f_EBC6C: return choose(policy_248);
        case Fn::f_EBC94: return choose(policy_28c);
        // Plays 20 / 21 (ordered escort).
        case Fn::f_EBCBC: return choose(policy_310);
        case Fn::f_EBDB4: return choose(policy_41c);
        case Fn::f_EBCE4: case Fn::f_EBDDC: owner.flag_active=!owner.flag_requested; return -1;
        case Fn::f_EBD2C: case Fn::f_EBE24: owner.flag_active=owner.flag_requested; return -1;
        case Fn::f_EBD04: return choose(policy_354);
        case Fn::f_EBD4C: return choose(policy_398);
        case Fn::f_EBDFC: return choose(policy_460);
        case Fn::f_EBE44: return choose(policy_4a4);
        case Fn::f_EBD74: case Fn::f_EBE6C:
            push_param(0.0F); owner_value_from_param(); clear_params();
            return -1;
        // Play 22 (random escort side with a timer).
        case Fn::f_EBEAC: return choose(policy_518);
        case Fn::f_EBED4:
            owner.flag_active=!owner.flag_requested;
            push_param(2.0F); push_param(10.0F); schedule_owner_timer(); clear_params();
            return -1;
        case Fn::f_EBF28: return choose(policy_55c);
        case Fn::f_EBF50:
            owner.flag_active=owner.flag_requested;
            push_param(2.0F); push_param(10.0F); schedule_owner_timer(); clear_params();
            return -1;
        case Fn::f_EBFA4: return choose(policy_5a0);
        // Play 23 (guard).
        case Fn::f_EBFCC: return choose(policy_614);
        case Fn::f_EBFF4: state.aim=AiGoal{}; return -1;
        case Fn::f_EC014: return choose(policy_648);
        case Fn::f_EC03C: resolve_target(); return -1;
        case Fn::f_EC05C: return choose(policy_670);
        // Play 27.
        case Fn::f_EC35C: move_to(owner.anchor); return -1;
        }
        return -1;
    }

    bool push(std::uint8_t play) noexcept {
        const std::span<const Step> script=script_of(play);
        if (script.empty()) {
            status=PlayStatus::unsupported_play;
            unsupported=play;
            return false;
        }
        if (state.stack.depth>=play_stack_capacity) {
            status=PlayStatus::stack_overflow;
            return false;
        }
        state.stack.frames[state.stack.depth]=PlayFrame{play,script[0].next};
        ++state.stack.depth;
        return true; // no header carries a run routine
    }

    [[nodiscard]] const Step* step_of(const PlayFrame& frame) const noexcept {
        const std::span<const Step> script=script_of(frame.play);
        if (frame.state<1 || static_cast<std::size_t>(frame.state)>=script.size()) return nullptr;
        return &script[static_cast<std::size_t>(frame.state)];
    }

    // ai_play_stack_update (func_800EA478).
    void update(std::uint8_t root) noexcept {
        PlayStack& stack=state.stack;
        // Retail (func_800EA478) also pushes the root when the top frame's play
        // is 0. No port frame ever holds play 0 (push and force_member_root_play
        // reject it), so depth==0 alone is equivalent. CONFIRMED by invariant.
        if (stack.depth==0U && !push(root)) return;
        for (std::uint8_t i=0U; i<stack.depth; ++i) {
            if (script_of(stack.frames[i].play).empty()) {
                status=PlayStatus::unsupported_play;
                unsupported=stack.frames[i].play;
                return;
            }
        }
        std::uint8_t i=0U;
        const auto count=static_cast<std::uint8_t>(stack.depth-1U);
        for (; i<count; ++i) {
            PlayFrame& frame=stack.frames[i];
            const Step* s=step_of(frame);
            if (s==nullptr || s->monitor==Fn::none) continue;
            const std::int32_t next=call(s->monitor);
            if (next==-1 || next==frame.state) continue;
            stack.depth=static_cast<std::uint8_t>(i+1U); // unwind: headers have no exit routine
            frame.state=static_cast<std::int8_t>(next);
            break;
        }
        PlayFrame& frame=stack.frames[i];
        if (frame.state==-2) {
            --stack.depth;
            if (stack.depth==0U) return;
            PlayFrame& parent=stack.frames[stack.depth-1U];
            const Step* ps=step_of(parent);
            if (ps!=nullptr && ps->next!=-1) parent.state=ps->next;
            return;
        }
        const std::int8_t state_now=frame.state;
        const Step* s=step_of(frame);
        if (s==nullptr) {
            status=PlayStatus::unsupported_play;
            unsupported=frame.play;
            return;
        }
        std::int8_t next_state=s->next==-1 ? state_now : s->next;
        if (s->run!=Fn::none) {
            const std::int32_t r=call(s->run);
            if (r!=-1 && r!=state_now) {
                frame.state=static_cast<std::int8_t>(r);
                return;
            }
            if (r!=-1) next_state=state_now;
        }
        if (s->play==0U) {
            frame.state=next_state;
            return;
        }
        push(s->play);
    }
};

} // namespace

// ---------------------------------------------------------------------------
// Controller.

void CampaignAiUnitState::serialize(StateWriter& writer) const {
    writer.id(unit);
    writer.f32(skill0); writer.f32(skill1); writer.f32(contact);
    writer.f32(move_near); writer.f32(move_far); writer.f32(aim_near); writer.f32(aim_far);
    writer.u16(thresholds.health_threshold); writer.u16(thresholds.close_range);
    move.serialize(writer);
    aim.serialize(writer);
    stack.serialize(writer);
    writer.id(owner_suggestion);
    writer.f32(tracker.distance); writer.f32(tracker.delta); writer.boolean(tracker.approaching);
    writer.s32(last_fire_tick);
    writer.s32(stuck_deadline);
    writer.boolean(had_contact);
    writer.s32(deadline);
    writer.boolean(flag_2ba);
    writer.u8(last_metric);
    writer.boolean(route.has_value());
    if (route) {
        writer.id(route->unit); writer.u32(route->serial); writer.boolean(route->reachable);
        writer.f32(route->waypoint_x); writer.f32(route->waypoint_z);
        writer.f32(route->nav_distance); writer.u8(route->nav_state);
    }
    writer.u32(pending_serial);
}

void CampaignAiController::set_owner_difficulty(std::uint8_t owner,
                                                const OwnerDifficulty& difficulty) noexcept {
    if (owner<owner_capacity) owners_[owner]=difficulty;
}

bool CampaignAiController::assign_move_goal(EntityId unit,const AiGoal& goal) noexcept {
    if (unit.slot>=unit_capacity) return false;
    CampaignAiUnitState& s=state_[unit.slot];
    if (s.unit!=unit) return false;
    s.move=goal;
    s.route.reset();
    return true;
}

void MemberOwnerFields::serialize(StateWriter& writer) const {
    writer.u8(objective);
    writer.boolean(objective_point.has_value());
    if (objective_point) { writer.f32((*objective_point)[0]); writer.f32((*objective_point)[1]); }
    writer.boolean(anchor.has_value());
    if (anchor) { writer.f32((*anchor)[0]); writer.f32((*anchor)[1]); }
    writer.u8(target_member);
    writer.f32(action_value);
    writer.boolean(flag_active);
    writer.boolean(flag_requested);
    writer.s32(timer_start);
    writer.s32(timer_end);
    writer.boolean(release_pending);
}

PlayAssignStatus CampaignAiController::assign_member_play(EntityId unit,std::uint8_t play) noexcept {
    if (unit.slot>=unit_capacity) return PlayAssignStatus::unknown_unit;
    CampaignAiUnitState& s=state_[unit.slot];
    if (s.unit!=unit) return PlayAssignStatus::unknown_unit;
    if (play_scope(play)!=PlayScope::member) return PlayAssignStatus::not_member_play;
    if (s.stack.depth>0U && s.stack.frames[0].play==play) return PlayAssignStatus::unchanged;
    // func_800EA1FC(context, 0) then func_800EA1B8: headers have no exit
    // routine, so the unwind only drops frames.
    s.stack=PlayStack{};
    s.stack.frames[0]=PlayFrame{play,1};
    s.stack.depth=1U;
    return PlayAssignStatus::ok;
}

ForceRootPlayStatus CampaignAiController::force_member_root_play_status(EntityId unit,std::uint8_t play) noexcept {
    if (play==0U || play>28U) return ForceRootPlayStatus::invalid_play;
    if (!unit.valid() || unit.slot>=unit_capacity) return ForceRootPlayStatus::unknown_unit;
    CampaignAiUnitState& s=state_[unit.slot];
    // A different generation in this slot is stale; retrying never succeeds.
    if (s.unit.valid() && s.unit.slot==unit.slot && s.unit.generation!=unit.generation) {
        return ForceRootPlayStatus::unknown_unit;
    }
    // Not adopted yet (update() resets and seeds the slot on first sight, so
    // writing the stack now would be overwritten). The caller retries.
    if (s.unit!=unit) return ForceRootPlayStatus::pending_update;
    // Play 0 is rejected above, so frames[0] never holds play 0; update()'s
    // depth==0 root push is therefore equivalent to retail's push condition.
    s.stack=PlayStack{};
    s.stack.frames[0]=PlayFrame{play,1};
    s.stack.depth=1U;
    return ForceRootPlayStatus::ok;
}

bool CampaignAiController::force_member_root_play(EntityId unit,std::uint8_t play) noexcept {
    return force_member_root_play_status(unit,play)==ForceRootPlayStatus::ok;
}

std::uint8_t CampaignAiController::member_goal_mode(EntityId unit) const noexcept {
    if (unit.slot>=unit_capacity || state_[unit.slot].unit!=unit) return 0U;
    return static_cast<std::uint8_t>(state_[unit.slot].move.mode);
}

std::uint8_t CampaignAiController::member_root_play(EntityId unit) const noexcept {
    if (unit.slot>=unit_capacity) return 0U;
    const CampaignAiUnitState& s=state_[unit.slot];
    if (s.unit!=unit || s.stack.depth==0U) return 0U;
    return s.stack.frames[0].play;
}

void CampaignAiController::set_owner_objective(std::uint8_t owner,std::uint8_t objective,
                                               std::optional<std::array<float,2>> objective_point) noexcept {
    if (owner>=owner_capacity) return;
    owner_fields_[owner].objective=objective;
    owner_fields_[owner].objective_point=objective_point;
}

void CampaignAiController::set_owner_anchor(std::uint8_t owner,
                                            std::optional<std::array<float,2>> anchor) noexcept {
    if (owner<owner_capacity) owner_fields_[owner].anchor=anchor;
}

bool CampaignAiController::apply_route_response(const RouteResponse& response) noexcept {
    if (response.unit.slot>=unit_capacity) return false;
    CampaignAiUnitState& s=state_[response.unit.slot];
    if (s.unit!=response.unit || s.pending_serial==0U || response.serial!=s.pending_serial) return false;
    s.route=response;
    return true;
}

CampaignAiOutput CampaignAiController::update(const UnitPool& units,
                                              const std::array<OwnerRecord,owner_capacity>& owners,
                                              const AngleTable& angles,RetailRandom& rng,
                                              const CampaignAiFrame& frame,
                                              std::span<CampaignAiCommand> commands,
                                              std::span<RouteRequest> routes) {
    CampaignAiOutput output{};
    bool budget=frame.route_budget;
    const std::int32_t tick=frame.tick;
    const float scale=config_.distance_scale*0.5F;
    const ExternalParameters zero_parameters{};

    for (std::size_t slot=0U; slot<car::round_robin_units; ++slot) {
        const EntityId id=units.slot_id(slot);
        CampaignAiUnitState& s=state_[slot];
        const UnitRecord* unit=id.valid() ? units.get(id) : nullptr;
        if (s.unit!=id) {
            s=CampaignAiUnitState{};
            s.unit=id;
            if (unit!=nullptr && unit->control==UnitControl::computer) {
                // initialize_unit_ai_state (func_800E4104).
                const OwnerDifficulty difficulty=unit->owner<owner_capacity && owners_[unit->owner]
                                                     ? *owners_[unit->owner] : OwnerDifficulty{};
                s.skill0=roll_unit_skill(difficulty.skill_a,rng);
                s.skill1=roll_unit_skill(difficulty.skill_b,rng);
                if ((unit->flags&retail::flag_campaign_shielded)!=0U) {
                    s.stack.frames[0]=PlayFrame{28U,1};
                    s.stack.depth=1U;
                }
            }
        }
        const ExternalParameters& external=slot<frame.parameters.size() ? frame.parameters[slot]
                                                                         : zero_parameters;
        // func_800E63F4 skip: inactive or status-1 records pass the turn on.
        if (unit==nullptr || !unit->alive || external.status_one) {
            if (slot==round_robin_) round_robin_=static_cast<std::uint8_t>((slot+1U)%car::round_robin_units);
            continue;
        }
        const bool turn=slot==round_robin_ && budget;
        if (turn) round_robin_=static_cast<std::uint8_t>((round_robin_+1U)%car::round_robin_units);
        if (unit->control!=UnitControl::computer) continue;

        std::array<CandidatePerception,unit_capacity> candidate_buffer{};
        const std::size_t candidate_count=perceive_candidates(units,id,tick,config_.perception,
                                                              candidate_buffer);
        const std::span<const CandidatePerception> candidates{candidate_buffer.data(),candidate_count};
        const auto find_candidate=[&](EntityId target) -> const CandidatePerception* {
            for (const CandidatePerception& c : candidates) if (c.unit==target) return &c;
            return nullptr;
        };

        // Contact slot 0x1002 rises when a link appears (ai_run_update_phase).
        bool any_contact=false;
        for (const CandidatePerception& c : candidates) any_contact=any_contact || (c.linked && c.targetable);
        if (any_contact && !s.had_contact) s.contact=1.0F;
        s.had_contact=any_contact;

        CampaignAiCommand scratch{};
        CampaignAiCommand& command=output.commands<commands.size() ? commands[output.commands] : scratch;
        command=CampaignAiCommand{};
        command.command.unit=id;

        // ai_update_goal (func_800E5FA8) for the aim goal.
        float aim_distance=car::goal_far_distance;
        if (s.aim.mode==GoalMode::unit) {
            const CandidatePerception* c=find_candidate(s.aim.target);
            const UnitRecord* t=units.get(s.aim.target);
            if (c==nullptr || t==nullptr || !c->active || !c->linked || !c->targetable) {
                s.aim=AiGoal{};
            } else {
                s.aim.x=t->x; s.aim.z=t->z; s.aim.has_point=true;
                aim_distance=xz_distance(unit->x,unit->z,t->x,t->z);
            }
        } else if (s.aim.mode==GoalMode::object_list && s.aim.has_point) {
            aim_distance=xz_distance(unit->x,unit->z,s.aim.x,s.aim.z);
        }
        // Move goal: request a route only on this unit's budgeted turn.
        float move_distance=car::goal_far_distance;
        bool request=false;
        const auto nav_distance=[&](float direct) {
            if (s.route && s.route->reachable) return s.route->nav_distance;
            return config_.route_fallback==RouteFallback::direct_line_temporary ? direct
                                                                               : car::goal_far_distance;
        };
        switch (s.move.mode) {
        case GoalMode::none: break;
        case GoalMode::unit:
        case GoalMode::chase: {
            const UnitRecord* t=units.get(s.move.target);
            if (t==nullptr || !t->alive) { s.move=AiGoal{}; s.route.reset(); break; }
            s.move.x=t->x; s.move.z=t->z; s.move.has_point=true;
            request=turn;
            move_distance=nav_distance(xz_distance(unit->x,unit->z,t->x,t->z));
            break;
        }
        case GoalMode::object_list:
            // Aim-only form; never a move goal.
            s.move=AiGoal{}; s.route.reset();
            break;
        case GoalMode::evasion: {
            // ai_update_goal_unit.c:269-289 (mode 10). A dead target clears the
            // goal (CONFIRMED). Retail routes toward the target's live x/z
            // through a local (tmp2) via extend_route_three_steps (CONFIRMED).
            // TEMPORARY POLICY: the port stores it in s.move and uses the
            // normal turn request; move_distance uses Euclidean xz_distance
            // where retail uses unit->navDistance (shared with chase). When the target is the link partner
            // (D_8033B44A ? aux->first : aux->second) retail clears only
            // goal->point (CONFIRMED). TEMPORARY POLICY: the pairwise link table
            // and goal aux are not ported, so the partner is unknown; this unit
            // itself stands in as the partner (never routes toward itself).
            const UnitRecord* t=units.get(s.move.target);
            if (t==nullptr || !t->alive) { s.move=AiGoal{}; s.route.reset(); break; }
            if (s.move.target==id) { s.move.has_point=false; break; }
            s.move.x=t->x; s.move.z=t->z; s.move.has_point=true;
            request=turn;
            move_distance=nav_distance(xz_distance(unit->x,unit->z,s.move.x,s.move.z));
            break;
        }
        case GoalMode::point:
        case GoalMode::point_alt: {
            // An objective whose point is unresolved holds (no route, no steering).
            if (!s.move.has_point) break;
            const float d=xz_distance(unit->x,unit->z,s.move.x,s.move.z);
            const bool final_leg=s.route && s.route->nav_state==1U;
            if (!(d<car::goal_point_outer && (d<car::goal_point_inner || final_leg))) request=turn;
            move_distance=nav_distance(d);
            break;
        }
        case GoalMode::point_route:
        case GoalMode::follow:
            request=turn;
            s.move.has_point=true;
            move_distance=nav_distance(xz_distance(unit->x,unit->z,s.move.x,s.move.z));
            break;
        case GoalMode::path: {
            const float d=nav_distance(xz_distance(unit->x,unit->z,s.move.x,s.move.z));
            if (d<car::goal_mode9_arrive) { s.move=AiGoal{}; s.route.reset(); break; }
            s.move.has_point=true;
            move_distance=d;
            break;
        }
        }
        if (request && output.routes<routes.size()) {
            RouteRequest& r=routes[output.routes++];
            r=RouteRequest{id,s.move.mode,unit->x,unit->z,s.move.x,s.move.z,++serial_};
            if (serial_==0U) r.serial=++serial_;
            s.pending_serial=r.serial;
            budget=false;
        }
        s.move_near=move_distance<=scale ? 1.0F : scale/move_distance;
        s.move_far=move_distance/car::goal_far_distance;
        s.aim_near=aim_distance<=scale ? 1.0F : scale/aim_distance;
        s.aim_far=aim_distance/car::goal_far_distance;

        // Stuck timer (budgeted turn only).
        if (turn) {
            const auto mode=static_cast<std::uint8_t>(s.move.mode);
            if (mode>=10U || mode<8U || s.aim.mode!=GoalMode::none) {
                s.stuck_deadline=0;
            } else if (s.stuck_deadline==0) {
                s.stuck_deadline=tick+static_cast<std::int32_t>(car::stuck_seconds*car::ticks_per_second);
            } else if (s.stuck_deadline<tick) {
                command.self_destruct=true;
            }
        }

        // Play stack.
        const bool owner_computer=unit->owner<owner_capacity && owners[unit->owner].computer;
        const OwnerDifficulty play_difficulty=unit->owner<owner_capacity && owners_[unit->owner]
                                                  ? *owners_[unit->owner] : OwnerDifficulty{};
        MemberOwnerFields scratch_owner{};
        MemberOwnerFields& member_owner=unit->owner<owner_capacity ? owner_fields_[unit->owner] : scratch_owner;
        PlayContext play{s,*unit,units,candidates,external,owner_computer,command,member_owner,
                         play_difficulty,config_,rng,tick};
        play.update(select_member_root_play(external.unit_class,
                                            (unit->flags&enemy_retail::flag_ai_class4)!=0U,
                                            external.status_one));
        command.top_play=s.stack.depth>0U ? s.stack.frames[s.stack.depth-1U].play : 0U;
        if (play.status==PlayStatus::unsupported_play) {
            command.status=CampaignAiStatus::unsupported_play;
            command.unsupported_play=play.unsupported;
        } else if (play.status==PlayStatus::stack_overflow) {
            command.status=CampaignAiStatus::stack_overflow;
        } else if (unit->owner>=owner_capacity || !owners_[unit->owner]) {
            command.status=CampaignAiStatus::owner_difficulty_missing;
        }

        if (config_.move_goal==MoveGoalPolicy::chase_aim_target_temporary &&
            (s.move.mode==GoalMode::none || s.move.mode==GoalMode::chase)) {
            if (s.aim.mode==GoalMode::unit) {
                if (s.move.target!=s.aim.target) {
                    s.move=AiGoal{GoalMode::chase,s.aim.target,s.aim.x,s.aim.z,true};
                    s.route.reset();
                    s.tracker=DriveTracker{};
                }
            } else if (s.move.mode==GoalMode::chase) {
                s.move=AiGoal{};
                s.route.reset();
            }
        }

        // Layer 5: ai_update_unit_command (func_800E30F8).
        const OwnerDifficulty difficulty=unit->owner<owner_capacity && owners_[unit->owner]
                                             ? *owners_[unit->owner] : OwnerDifficulty{};
        const UnitRecord* target=s.aim.mode==GoalMode::unit ? units.get(s.aim.target) : nullptr;
        command.command.target=target!=nullptr ? s.aim.target : no_entity;
        std::optional<std::array<float,2>> steer;
        if (s.move.mode!=GoalMode::none && s.move.has_point) {
            if (s.route && s.route->reachable) steer=std::array<float,2>{s.route->waypoint_x,s.route->waypoint_z};
            else if (config_.route_fallback==RouteFallback::direct_line_temporary)
                steer=std::array<float,2>{s.move.x,s.move.z};
        }
        EnemyFirePolicy fire_policy{};
        const bool class4=(unit->flags&enemy_retail::flag_ai_class4)!=0U;
        float rate=difficulty.rate_primary;
        if (class4) rate=difficulty.rate_class4;
        else if (config_.retail_kind2 && unit->kind==*config_.retail_kind2) rate=difficulty.rate_kind2;
        fire_policy.cooldown_ticks=fire_cooldown_ticks(rate);
        fire_policy.mission_minutes=config_.mission_minutes;
        fire_policy.engagement_range=std::numeric_limits<float>::max();
        EnemyDriveTuning tuning{};
        tuning.speed=difficulty.speed;
        tuning.close_range=static_cast<float>(s.thresholds.close_range);

        DriveInput& input=command.command.input;
        const auto try_fire=[&](std::int32_t error) {
            if (target==nullptr) return;
            const FireDecision fire=decide_enemy_fire(*unit,*target,error,aim_distance,tick,
                                                      s.last_fire_tick,fire_policy,rng);
            command.command.fire_decided=fire.fire || fire.special;
            input.fire=input.fire_pressed=fire.fire;
            input.special=input.special_pressed=fire.special;
            if (fire.stamp_last_fire) s.last_fire_tick=tick;
        };
        const bool in_range=target!=nullptr && aim_distance<=enemy_retail::engagement_range;
        const bool expected=s.aim.mode==GoalMode::unit && s.move.mode==GoalMode::chase;
        float park_distance=car::goal_far_distance;
        if (s.move.mode==GoalMode::point || s.move.mode==GoalMode::point_route ||
            s.move.mode==GoalMode::chase) {
            park_distance=xz_distance(unit->x,unit->z,s.move.x,s.move.z);
        }
        const bool close=park_distance<static_cast<float>(s.thresholds.close_range);
        const bool drive=steer.has_value() && !close && (!in_range || !expected);
        if (unit->kind==retail::UnitKind::tank) {
            if (drive) {
                input=compute_enemy_drive(*unit,(*steer)[0],(*steer)[1],angles,tuning,&s.tracker);
                if (target!=nullptr) {
                    const EnemyAim aim=compute_enemy_aim(*unit,target->x,target->z);
                    if (aim.in_fire_window) try_fire(aim.angle_error);
                }
            } else if (target!=nullptr) {
                const EnemyAim aim=compute_enemy_aim(*unit,target->x,target->z);
                input.rotation=aim.hull_rotation;
                if (aim.in_fire_window) try_fire(aim.angle_error);
            }
        } else {
            if (target!=nullptr) {
                const EnemyAim aim=compute_enemy_aim(*unit,target->x,target->z);
                input.aim=aim.aim;
                if (aim.in_fire_window) try_fire(aim.angle_error);
            }
            if (drive) {
                const DriveInput d=compute_enemy_drive(*unit,(*steer)[0],(*steer)[1],angles,tuning,
                                                       &s.tracker);
                input.throttle=d.throttle;
                input.rotation=d.rotation;
            }
        }
        if (output.commands<commands.size()) ++output.commands;
    }
    return output;
}

void CampaignAiController::serialize(StateWriter& writer) const {
    writer.u8(round_robin_);
    writer.u32(serial_);
    for (const std::optional<OwnerDifficulty>& owner : owners_) {
        writer.boolean(owner.has_value());
        if (owner) owner->serialize(writer);
    }
    for (const MemberOwnerFields& owner : owner_fields_) owner.serialize(writer);
    for (const CampaignAiUnitState& s : state_) s.serialize(writer);
}

} // namespace battletanx
