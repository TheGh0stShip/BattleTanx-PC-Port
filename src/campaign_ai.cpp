#include "battletanx/campaign_ai.hpp"

#include "battletanx/big_endian.hpp"

#include <cmath>
#include <cstdlib>
#include <limits>

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

enum class PolicyId : std::uint8_t { none,p04,p05,p06,p07,p08,p09,p10,p11,p17,p18,p19 };

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

const Policy& policy_of(PolicyId id) noexcept {
    switch (id) {
    case PolicyId::p04: return policy_p04;
    case PolicyId::p05: return policy_p05;
    case PolicyId::p06: return policy_p06;
    case PolicyId::p07: return policy_p07;
    case PolicyId::p08: return policy_p08;
    case PolicyId::p09: return policy_p09;
    case PolicyId::p10: return policy_p10;
    case PolicyId::p11: return policy_p11;
    case PolicyId::p17: return policy_p17;
    case PolicyId::p18: return policy_p18;
    case PolicyId::p19: return policy_p19;
    case PolicyId::none: break;
    }
    return policy_p09;
}

enum class Action : std::uint8_t {
    none,
    policy,             // run = choose_weighted_rule(run_policy)
    secondary_target,   // func_800EAFC4 -> func_800E69D4
    profile2,           // func_800EAD9C
    profile3,           // func_800EAE30
    profile4,           // func_800EAEA8
    profile5,           // func_800EAF14
    profile6,           // func_800EAF6C
    resolve_target,     // func_800EB00C / func_800EB1A0 -> func_800E6B3C
    reset_link,         // func_800EB158 -> func_800E6BF0
    emit_reset,         // func_800EB440
    query_point,        // func_800EB07C
    clear_owner_command,// func_800EB110
};

struct Step {
    Action run;
    PolicyId run_policy;
    PolicyId monitor;
    std::uint8_t play;
    std::int8_t next;
};

constexpr Step step(Action run,PolicyId monitor,std::uint8_t play,std::int8_t next,
                    PolicyId run_policy=PolicyId::none) {
    return Step{run,run_policy,monitor,play,next};
}

// Index 0 is the header record {play, initial state}; every ported play
// starts in state 1 and has no entry/exit handler.
constexpr std::array<Step,7> play4{{
    step(Action::none,PolicyId::none,4U,1),
    step(Action::policy,PolicyId::none,0U,-1,PolicyId::p04),
    step(Action::profile2,PolicyId::none,0U,-2),
    step(Action::profile3,PolicyId::none,0U,-2),
    step(Action::profile4,PolicyId::none,0U,-2),
    step(Action::profile5,PolicyId::none,0U,-2),
    step(Action::profile6,PolicyId::none,0U,-2),
}};
constexpr std::array<Step,7> play5{{
    step(Action::none,PolicyId::none,5U,1),
    step(Action::secondary_target,PolicyId::none,4U,2),
    step(Action::none,PolicyId::p05,6U,1),
    step(Action::resolve_target,PolicyId::p06,6U,1),
    step(Action::none,PolicyId::p07,6U,1),
    step(Action::query_point,PolicyId::p08,6U,1),
    step(Action::clear_owner_command,PolicyId::none,0U,1),
}};
constexpr std::array<Step,4> play6{{
    step(Action::none,PolicyId::none,6U,1),
    step(Action::policy,PolicyId::none,0U,-1,PolicyId::p09),
    step(Action::reset_link,PolicyId::p10,9U,-2),
    step(Action::resolve_target,PolicyId::p11,9U,-2),
}};
constexpr std::array<Step,3> play9{{
    step(Action::none,PolicyId::none,9U,1),
    step(Action::policy,PolicyId::p18,12U,-2,PolicyId::p17),
    step(Action::emit_reset,PolicyId::p19,12U,-1),
}};
constexpr std::array<Step,2> play12{{
    step(Action::none,PolicyId::none,12U,1),
    step(Action::none,PolicyId::none,0U,1),
}};
constexpr std::array<Step,2> play28{{
    step(Action::none,PolicyId::none,28U,1),
    step(Action::none,PolicyId::none,12U,-1),
}};

std::span<const Step> script_of(std::uint8_t play) noexcept {
    switch (play) {
    case 4U: return play4;
    case 5U: return play5;
    case 6U: return play6;
    case 9U: return play9;
    case 12U: return play12;
    case 28U: return play28;
    default: return {};
    }
}

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

namespace {

struct PlayContext {
    CampaignAiUnitState& state;
    const UnitRecord& unit;
    std::span<const CandidatePerception> candidates;
    const ExternalParameters& external;
    bool owner_computer;
    CampaignAiCommand& command;
    PlayStatus status{PlayStatus::running};
    std::uint8_t unsupported{};

    [[nodiscard]] float parameter(std::uint16_t key) const noexcept {
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
        case 0x110AU: return external.p110A;
        case 0x110BU: return external.p110B;
        case 0x110DU: return external.p110D;
        case 0x110EU: return external.p110E;
        case 0x1116U: return external.p1116;
        case 0x1117U: return external.p1117;
        case 0x2111U: return external.p2111;
        case 0x8103U: return external.p8103;
        default: return 0.0F;
        }
    }

    // choose_weighted_rule (func_800EA93C), including the retail loop bound
    // that never scores the last eligible rule when two or more are eligible.
    [[nodiscard]] std::int32_t choose(const Policy& policy) const noexcept {
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

    std::int32_t run(const Step& s) noexcept {
        switch (s.run) {
        case Action::none: return -1;
        case Action::policy: return choose(policy_of(s.run_policy));
        case Action::secondary_target: {
            const ScoredTarget r=select_best_scored(candidates,secondary_score_weights,owner_computer);
            if (r.found) state.owner_suggestion=r.unit;
            return -1;
        }
        case Action::profile2: set_profile(EngagementProfile::state2); return -1;
        case Action::profile3: set_profile(EngagementProfile::state3); return -1;
        case Action::profile4: set_profile(EngagementProfile::state4); return -1;
        case Action::profile5: set_profile(EngagementProfile::state5); return -1;
        case Action::profile6: set_profile(EngagementProfile::state6); return -1;
        case Action::resolve_target: {
            // The keep-route-target branch (kind 0x1B objects) is not ported, so
            // keep is always 0 and an empty result clears slot 0x1002.
            const ScoredTarget r=select_best_scored(candidates,target_score_weights,owner_computer);
            if (!r.found) {
                state.contact=0.0F;
            } else {
                state.aim=AiGoal{GoalMode::unit,r.unit,0.0F,0.0F,false};
            }
            return -1;
        }
        case Action::reset_link:
            state.aim=AiGoal{};
            return -1;
        case Action::emit_reset:
            command.owner_event=true;
            state.move=AiGoal{};
            return -1;
        case Action::query_point:
            state.move=AiGoal{};
            return -1;
        case Action::clear_owner_command:
            command.owner_command_cleared=true;
            return -1;
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
        // Ported headers carry no entry handler.
        return true;
    }

    // ai_play_stack_update (func_800EA478).
    void update(std::uint8_t root) noexcept {
        PlayStack& stack=state.stack;
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
            const Step& s=script_of(frame.play)[static_cast<std::size_t>(frame.state)];
            if (s.monitor==PolicyId::none) continue;
            const std::int32_t next=choose(policy_of(s.monitor));
            if (next==-1 || next==frame.state) continue;
            stack.depth=static_cast<std::uint8_t>(i+1U); // ported plays have no exit handler
            frame.state=static_cast<std::int8_t>(next);
            break;
        }
        PlayFrame& frame=stack.frames[i];
        const std::span<const Step> script=script_of(frame.play);
        if (frame.state==-2) {
            --stack.depth;
            if (stack.depth==0U) return;
            PlayFrame& parent=stack.frames[stack.depth-1U];
            const std::int8_t next=script_of(parent.play)[static_cast<std::size_t>(parent.state)].next;
            if (next!=-1) parent.state=next;
            return;
        }
        const std::int8_t state_now=frame.state;
        const Step& s=script[static_cast<std::size_t>(state_now)];
        std::int8_t next_state=s.next==-1 ? state_now : s.next;
        if (s.run!=Action::none) {
            const std::int32_t r=run(s);
            if (r!=-1 && r!=state_now) {
                frame.state=static_cast<std::int8_t>(r);
                return;
            }
            if (r!=-1) next_state=state_now;
        }
        if (s.play==0U) {
            frame.state=next_state;
            return;
        }
        push(s.play);
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
        // func_800E63F4 skip: inactive records pass the round-robin turn on.
        if (unit==nullptr || !unit->alive) {
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
        case GoalMode::point:
        case GoalMode::point_alt: {
            const float d=xz_distance(unit->x,unit->z,s.move.x,s.move.z);
            const bool final_leg=s.route && s.route->nav_state==1U;
            if (!(d<car::goal_point_outer && (d<car::goal_point_inner || final_leg))) request=turn;
            s.move.has_point=true;
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
        const ExternalParameters& external=slot<frame.parameters.size() ? frame.parameters[slot]
                                                                         : zero_parameters;
        PlayContext play{s,*unit,candidates,external,owner_computer,command};
        play.update(select_member_root_play(0U,(unit->flags&enemy_retail::flag_ai_class4)!=0U,false));
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
    for (const CampaignAiUnitState& s : state_) s.serialize(writer);
}

} // namespace battletanx
