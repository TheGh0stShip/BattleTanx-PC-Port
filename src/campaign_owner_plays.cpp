#include "battletanx/campaign_owner_plays.hpp"

namespace battletanx::owner_plays {
namespace {

namespace rv=retail_values;

// ---------------------------------------------------------------------------
// Role tables (gAiPlaySlots_*, ai_play_tables.c). Each run ends at the entry
// before the {0, NULL} terminator; role 10 has no terminator in its own run
// and reads the zero pad that follows (gAiPlaySlotPad), the same thing.
using S=SlotScorer;
constexpr std::array<RoleSlot,6> role1{{{0x10,S::on_slot_play},{0x10,S::on_slot_play},{0x10,S::on_slot_play},
    {0x10,S::on_slot_play},{0x01,S::current_or_class4},{0x05,S::first_candidate}}};
constexpr std::array<RoleSlot,6> role2{{{0x10,S::on_slot_play},{0x10,S::on_slot_play},{0x10,S::on_slot_play},
    {0x10,S::on_slot_play},{0x01,S::current_or_class4},{0x0D,S::first_candidate}}};
constexpr std::array<RoleSlot,8> role3{{{0x10,S::on_slot_play},{0x10,S::on_slot_play},{0x10,S::on_slot_play},
    {0x10,S::on_slot_play},{0x01,S::current_or_class4},{0x08,S::objective_distance},{0x05,S::member_mode},
    {0x08,S::first_candidate}}};
constexpr std::array<RoleSlot,11> role4{{{0x10,S::on_slot_play},{0x10,S::on_slot_play},{0x10,S::on_slot_play},
    {0x10,S::on_slot_play},{0x01,S::current_or_class4},{0x05,S::member_mode},{0x03,S::keep_current},
    {0x03,S::keep_current},{0x03,S::keep_current},{0x03,S::keep_current},{0x05,S::first_candidate}}};
constexpr std::array<RoleSlot,8> role5{{{0x1C,S::on_play_status_two},{0x0B,S::on_play_status_two},
    {0x10,S::on_slot_play},{0x10,S::on_slot_play},{0x10,S::on_slot_play},{0x10,S::on_slot_play},
    {0x01,S::current_or_class4},{0x05,S::first_candidate}}};
constexpr std::array<RoleSlot,10> role6{{{0x10,S::on_slot_play},{0x10,S::on_slot_play},{0x10,S::on_slot_play},
    {0x10,S::on_slot_play},{0x10,S::on_slot_play},{0x10,S::on_slot_play},{0x10,S::on_slot_play},
    {0x10,S::on_slot_play},{0x01,S::current_or_class4},{0x11,S::first_candidate}}};
constexpr std::array<RoleSlot,5> role7{{{0x10,S::on_slot_play},{0x10,S::on_slot_play},{0x10,S::on_slot_play},
    {0x10,S::on_slot_play},{0x03,S::first_candidate}}};
constexpr std::array<RoleSlot,6> role8{{{0x10,S::on_slot_play},{0x10,S::on_slot_play},{0x10,S::on_slot_play},
    {0x10,S::on_slot_play},{0x05,S::origin_proximity},{0x03,S::first_candidate}}};
constexpr std::array<RoleSlot,7> role9{{{0x10,S::on_slot_play},{0x10,S::on_slot_play},{0x10,S::on_slot_play},
    {0x10,S::on_slot_play},{0x05,S::origin_proximity},{0x05,S::origin_proximity},{0x03,S::first_candidate}}};
constexpr std::array<RoleSlot,8> role10{{{0x10,S::on_slot_play},{0x10,S::on_slot_play},{0x10,S::on_slot_play},
    {0x10,S::on_slot_play},{0x05,S::origin_proximity},{0x05,S::origin_proximity},{0x05,S::origin_proximity},
    {0x03,S::first_candidate}}};

// ---------------------------------------------------------------------------
// Weighted policies (choose_weighted_rule, func_800EA93C).
struct OwnerTerm {
    std::uint16_t key{};
    float value{};
};

struct OwnerOption {
    std::int8_t action{};
    std::span<const OwnerTerm> requirements{}; // empty = no requirement pointer
    std::span<const OwnerTerm> weights{};      // empty = no weight pointer
};

constexpr float eps=rv::term_epsilon;

// Play 15 monitors for states 1,2,4,5,6,7: D_80136930, D_80136A90, D_80136C18,
// D_80136D78, D_80136ED8, D_80137038 (identical option data). A sixth
// option {7, 0x2107/0x810F, 0x8113/0x210B/0x8100} follows each list but the
// chooser reads only five, so "hunt owner 3" (state 7) is never chosen.
constexpr std::array<OwnerTerm,1> hunt_r3{{{0x210F,eps}}};
constexpr std::array<OwnerTerm,1> hunt_r2{{{0x2111,eps}}};
constexpr std::array<OwnerTerm,2> hunt_r4{{{0x2104,eps},{0x810C,eps}}};
constexpr std::array<OwnerTerm,3> hunt_w4{{{0x8110,0.9F},{0x2108,0.5F},{0x8100,0.1F}}};
constexpr std::array<OwnerTerm,2> hunt_r5{{{0x2105,eps},{0x810D,eps}}};
constexpr std::array<OwnerTerm,3> hunt_w5{{{0x8111,0.9F},{0x2109,0.5F},{0x8100,0.1F}}};
constexpr std::array<OwnerTerm,2> hunt_r6{{{0x2106,eps},{0x810E,eps}}};
constexpr std::array<OwnerTerm,3> hunt_w6{{{0x8112,0.9F},{0x210A,0.5F},{0x8100,0.1F}}};
const std::array<OwnerOption,5> hunt_policy{{
    {3,hunt_r3,{}},{2,hunt_r2,{}},{4,hunt_r4,hunt_w4},{5,hunt_r5,hunt_w5},{6,hunt_r6,hunt_w6}}};

// Play 15 state 3 monitor: D_80136B00.
constexpr std::array<OwnerTerm,1> regroup_r1{{{0x2110,eps}}};
const std::array<OwnerOption,1> regroup_policy{{{1,regroup_r1,{}}}};

// Play 14 state 1: D_80136774.
constexpr std::array<OwnerTerm,1> child_r0{{{0x210F,eps}}};
constexpr std::array<OwnerTerm,1> child_r1{{{0x2117,1.0F}}};
constexpr std::array<OwnerTerm,1> child_r2{{{0x2113,1.0F}}};
const std::array<OwnerOption,4> child_policy{{{3,child_r0,{}},{4,child_r1,{}},{3,child_r2,{}},{2,{},{}}}};

// Plays 25/26 timers on 0x8101 (D_80137708/30/58, D_801377D0/F8, D_80137820).
constexpr std::array<OwnerTerm,1> t25_1{{{0x8101,0.03333F}}};
constexpr std::array<OwnerTerm,1> t25_2{{{0x8101,0.06666F}}};
constexpr std::array<OwnerTerm,1> t25_3{{{0x8101,0.09999F}}};
constexpr std::array<OwnerTerm,1> t26_1{{{0x8101,0.016665F}}};
constexpr std::array<OwnerTerm,1> t26_2{{{0x8101,0.049995F}}};
constexpr std::array<OwnerTerm,1> t26_3{{{0x8101,0.083325F}}};
const std::array<OwnerOption,1> p25_1{{{2,t25_1,{}}}};
const std::array<OwnerOption,1> p25_2{{{3,t25_2,{}}}};
const std::array<OwnerOption,1> p25_3{{{4,t25_3,{}}}};
const std::array<OwnerOption,1> p26_1{{{2,t26_1,{}}}};
const std::array<OwnerOption,1> p26_2{{{3,t26_2,{}}}};
const std::array<OwnerOption,1> p26_3{{{4,t26_3,{}}}};

enum class OwnerPolicy : std::uint8_t { none, hunt, regroup, child, t25_1, t25_2, t25_3, t26_1, t26_2, t26_3 };

std::span<const OwnerOption> policy_options(OwnerPolicy policy) noexcept {
    switch (policy) {
    case OwnerPolicy::hunt: return hunt_policy;
    case OwnerPolicy::regroup: return regroup_policy;
    case OwnerPolicy::child: return child_policy;
    case OwnerPolicy::t25_1: return p25_1;
    case OwnerPolicy::t25_2: return p25_2;
    case OwnerPolicy::t25_3: return p25_3;
    case OwnerPolicy::t26_1: return p26_1;
    case OwnerPolicy::t26_2: return p26_2;
    case OwnerPolicy::t26_3: return p26_3;
    case OwnerPolicy::none: break;
    }
    return {};
}

// ---------------------------------------------------------------------------
// Owner play scripts (gAiScriptData). Index 0 is the header.
enum class Run : std::uint8_t {
    none,
    set_role,                    // func_800EACEC (4) / func_800EB4A4 (5)
    assign,                      // func_800EAD2C / func_800EB4E4
    role_assign,                 // role = arg, then assign
    clear_objective_role1,       // func_800EB7AC
    objective_release,           // func_800EB990/A00/A70: objective = arg, release
    objective_clear_release,     // func_800EB920: objective 0, clear_word_pair, release
    release_then_policy,         // func_800EB69C
    role_assign_then_policy,     // func_800EC0D4.. / func_800EC218..
};

struct OwnerStep {
    Run run{Run::none};
    std::uint8_t arg{};
    OwnerPolicy run_policy{OwnerPolicy::none};
    OwnerPolicy monitor{OwnerPolicy::none};
    std::uint8_t push{};
    std::int8_t next{-1};
};

// header.next carries the initial state.
constexpr std::array<OwnerStep,3> script2{{{Run::none,0,OwnerPolicy::none,OwnerPolicy::none,0,1},
    {Run::set_role,4,OwnerPolicy::none,OwnerPolicy::none,0,2},{Run::assign,0,OwnerPolicy::none,OwnerPolicy::none,0,-1}}};
constexpr std::array<OwnerStep,3> script10{{{Run::none,0,OwnerPolicy::none,OwnerPolicy::none,0,1},
    {Run::set_role,5,OwnerPolicy::none,OwnerPolicy::none,0,2},{Run::assign,0,OwnerPolicy::none,OwnerPolicy::none,0,-1}}};
constexpr std::array<OwnerStep,2> script12{{{Run::none,0,OwnerPolicy::none,OwnerPolicy::none,0,1},
    {Run::none,0,OwnerPolicy::none,OwnerPolicy::none,0,1}}};
constexpr std::array<OwnerStep,5> script14{{{Run::none,0,OwnerPolicy::none,OwnerPolicy::none,0,1},
    {Run::release_then_policy,0,OwnerPolicy::child,OwnerPolicy::none,0,-1},
    {Run::role_assign,2,OwnerPolicy::none,OwnerPolicy::none,0,-2},
    {Run::role_assign,3,OwnerPolicy::none,OwnerPolicy::none,0,-2},
    {Run::role_assign,1,OwnerPolicy::none,OwnerPolicy::none,0,-2}}};
constexpr std::array<OwnerStep,8> script15{{{Run::none,0,OwnerPolicy::none,OwnerPolicy::none,0,1},
    {Run::clear_objective_role1,0,OwnerPolicy::none,OwnerPolicy::hunt,12,-1},
    {Run::role_assign,1,OwnerPolicy::none,OwnerPolicy::hunt,12,-1},
    {Run::role_assign,3,OwnerPolicy::none,OwnerPolicy::regroup,12,-1},
    {Run::objective_clear_release,0,OwnerPolicy::none,OwnerPolicy::hunt,14,-1},
    {Run::objective_release,1,OwnerPolicy::none,OwnerPolicy::hunt,14,-1},
    {Run::objective_release,2,OwnerPolicy::none,OwnerPolicy::hunt,14,-1},
    {Run::objective_release,3,OwnerPolicy::none,OwnerPolicy::hunt,14,-1}}};
constexpr std::array<OwnerStep,5> script25{{{Run::none,0,OwnerPolicy::none,OwnerPolicy::none,0,1},
    {Run::role_assign_then_policy,8,OwnerPolicy::t25_1,OwnerPolicy::none,0,-1},
    {Run::role_assign_then_policy,9,OwnerPolicy::t25_2,OwnerPolicy::none,0,-1},
    {Run::role_assign_then_policy,10,OwnerPolicy::t25_3,OwnerPolicy::none,0,-1},
    {Run::role_assign,1,OwnerPolicy::none,OwnerPolicy::none,0,-1}}};
constexpr std::array<OwnerStep,5> script26{{{Run::none,0,OwnerPolicy::none,OwnerPolicy::none,0,1},
    {Run::role_assign_then_policy,8,OwnerPolicy::t26_1,OwnerPolicy::none,0,-1},
    {Run::role_assign_then_policy,9,OwnerPolicy::t26_2,OwnerPolicy::none,0,-1},
    {Run::role_assign_then_policy,10,OwnerPolicy::t26_3,OwnerPolicy::none,0,-1},
    {Run::role_assign,1,OwnerPolicy::none,OwnerPolicy::none,0,-1}}};

std::span<const OwnerStep> script_of(std::uint8_t play) noexcept {
    switch (play) {
    case 2: return script2;
    case 10: return script10;
    case 12: return script12;
    case 14: return script14;
    case 15: return script15;
    case 25: return script25;
    case 26: return script26;
    default: return {};
    }
}

const OwnerStep* step_of(std::uint8_t play,std::int8_t state) noexcept {
    const auto script=script_of(play);
    if (state<0) return nullptr;
    const auto index=static_cast<std::size_t>(state);
    return index<script.size() ? &script[index] : nullptr;
}

// ai_horizontal_length (func_800E0BC4): octagonal approximation, not
// Euclidean. Constants D_8007442C/30/34/38 are 3.0, 8.0, 3.0, 8.0. CONFIRMED.
float horizontal_length(float a,float b) noexcept {
    if (!(a>0.0F)) a=-a;
    if (!(b>0.0F)) b=-b;
    return a<b ? b+(a*3.0F)/8.0F : a+(b*3.0F)/8.0F;
}

float chain_weight(const OwnerWorld& world,std::size_t owner) noexcept {
    if (owner>=world.owner_count || owner>=rv::owner_records) return 0.0F;
    const auto& facts=world.owners[owner];
    if (!facts.chain_active) return 0.0F;
    float total=0.0F;
    for (std::uint8_t i=0U;i<facts.chain_nodes;++i) total+=rv::chain_node_weight;
    return total;
}

float flagged_weight(const OwnerWorld& world,std::size_t owner) noexcept {
    if (owner>=world.owner_count || owner>=rv::owner_records) return 0.0F;
    return world.owners[owner].has_flagged_member ? rv::flagged_member_weight : 0.0F;
}

float team_lacks(const OwnerWorld& world,std::uint16_t value) noexcept {
    // Bounds-check policy: retail loops count=types[3] halfwords and would read
    // the count field itself past 3; valid team data never has more than 3.
    const std::size_t count=world.team_list_count<world.team_list.size() ? world.team_list_count : world.team_list.size();
    for (std::size_t i=0U;i<count;++i) {
        if (world.team_list[i]==value) return 0.0F;
    }
    return 1.0F;
}

struct Evaluator {
    ParameterContext context{};
    bool unported{};

    float get(std::uint16_t key) noexcept {
        const auto value=owner_parameter(context,key);
        if (!value) {
            unported=true;
            return 0.0F;
        }
        return *value;
    }
};

// choose_weighted_rule. Retail quirks kept: a requirement passes when the
// parameter is not below the value; with two or more eligible options only
// options before the last eligible one are weighed (i < last), the first of
// equal scores wins, and an option with no weight pointer scores 0.
std::int8_t choose_owner_option(std::span<const OwnerOption> options,Evaluator& evaluator) noexcept {
    std::array<bool,rv::policy_option_limit> eligible{};
    std::size_t last=0U;
    std::size_t count=0U;
    const std::size_t limit=options.size()<rv::policy_option_limit ? options.size() : rv::policy_option_limit;
    for (std::size_t i=0U;i<limit;++i) {
        const auto& option=options[i];
        bool pass=true;
        for (const auto& term : option.requirements) {
            if (evaluator.get(term.key)<term.value) {
                pass=false;
                break;
            }
        }
        eligible[i]=pass;
        if (pass) {
            ++count;
            last=i;
        }
    }
    if (count==0U) return -1;
    if (count==1U) return options[last].action;
    std::size_t best=0U;
    float best_score=0.0F;
    bool found=false;
    for (std::size_t i=0U;i<last;++i) {
        if (!eligible[i]) continue;
        float score=0.0F;
        for (const auto& term : options[i].weights) score+=evaluator.get(term.key)*term.value;
        if (!found || best_score<score) {
            best=i;
            best_score=score;
            found=true;
        }
    }
    return found ? options[best].action : std::int8_t{-1};
}

struct Runner {
    OwnerPlayState& state;
    const OwnerPlayInput& input;
    RetailRandom& rng;
    OwnerPlayResult& result;

    Evaluator evaluator() const noexcept {
        Evaluator e{};
        e.context=ParameterContext{.owner=input.owner,.objective=state.objective,.world=input.world,.rng=&rng};
        return e;
    }

    std::int8_t policy(OwnerPolicy id) noexcept {
        auto e=evaluator();
        const auto action=choose_owner_option(policy_options(id),e);
        if (e.unported) result.status=OwnerPlayStatus::parameter_unported;
        return action;
    }

    void set_role(std::uint8_t role) noexcept {
        if (state.role!=role) result.role_changed=true;
        state.role=role;
    }

    // ai_set_owner_objective_from_param (func_800E6FF8): id >= 5 -> 0xFF.
    void set_objective(std::uint8_t id) noexcept {
        const std::uint8_t value=id<rv::objective_limit ? id : rv::no_objective;
        if (state.objective!=value) result.objective_changed=true;
        state.objective=value;
    }

    void assign() noexcept {
        std::optional<Point2> point{};
        // TEMPORARY POLICY: set_objective accepts 0..4 (func_800E6FF8 limit 5),
        // but only ids below owner_records (4) resolve a point; objective 4
        // reads as "no point". Unreachable: ported scripts set only 0xFF,0..3.
        if (input.world!=nullptr && state.objective<rv::owner_records) point=input.world->objective_points[state.objective];
        result.assignment=assign_member_plays(state.role,input.active,input.members,point);
        result.assigned=true;
    }

    std::int8_t run(const OwnerStep& step) noexcept {
        switch (step.run) {
        case Run::none: return -1;
        case Run::set_role: set_role(step.arg); return -1;
        case Run::assign: assign(); return -1;
        case Run::role_assign: set_role(step.arg); assign(); return -1;
        case Run::clear_objective_role1:
            set_objective(rv::no_objective);
            set_role(1U);
            assign();
            result.release_target=true;
            return -1;
        case Run::objective_release:
            set_objective(step.arg);
            result.release_target=true;
            return -1;
        case Run::objective_clear_release:
            set_objective(0U);
            result.clear_word_pair=true;
            result.release_target=true;
            return -1;
        case Run::release_then_policy:
            result.release_target=true;
            return policy(step.run_policy);
        case Run::role_assign_then_policy:
            set_role(step.arg);
            assign();
            return policy(step.run_policy);
        }
        return -1;
    }

    bool push(std::uint8_t play) noexcept {
        const auto script=script_of(play);
        if (script.empty()) {
            result.status=OwnerPlayStatus::unsupported_play;
            result.unsupported_play=play;
            return false;
        }
        if (state.stack.depth>=state.stack.frames.size()) {
            result.status=OwnerPlayStatus::stack_overflow;
            return false;
        }
        state.stack.frames[state.stack.depth]=OwnerPlayFrame{.play=play,.state=script[0].next};
        ++state.stack.depth;
        return true; // every owner-play header run is NULL
    }

    // ai_play_stack_update (func_800EA478) for a kind-1 context.
    void update() noexcept {
        auto& stack=state.stack;
        if (stack.depth==0U || stack.frames[stack.depth-1U].play==0U) {
            if (!push(select_owner_root_play(input.game_mode,input.computer))) return;
        }
        const std::size_t count=stack.depth-1U;
        std::size_t i=0U;
        for (;i<count;++i) {
            auto& frame=stack.frames[i];
            const OwnerStep* step=step_of(frame.play,frame.state);
            if (step==nullptr || step->monitor==OwnerPolicy::none) continue;
            const std::int8_t next=policy(step->monitor);
            if (next==-1 || next==frame.state) continue;
            stack.depth=static_cast<std::uint8_t>(i+1U); // popped headers have no monitor
            frame.state=next;
            break;
        }
        auto& frame=stack.frames[i];
        if (frame.state==-2) {
            --stack.depth;
            if (i==0U) return; // a root never returns -2 in the ported scripts
            auto& parent=stack.frames[i-1U];
            const OwnerStep* parent_step=step_of(parent.play,parent.state);
            if (parent_step!=nullptr && parent_step->next!=-1) parent.state=parent_step->next;
            return;
        }
        const OwnerStep* step=step_of(frame.play,frame.state);
        if (step==nullptr) {
            result.status=OwnerPlayStatus::unsupported_play;
            result.unsupported_play=frame.play;
            return;
        }
        std::int8_t next_state=frame.state;
        if (step->run==Run::none) {
            if (step->next!=-1) next_state=step->next;
        } else {
            const std::int8_t r=run(*step);
            if (r==-1) {
                if (step->next!=-1) next_state=step->next;
            } else if (r!=frame.state) {
                frame.state=r;
                return;
            }
        }
        if (step->push==0U) {
            frame.state=next_state;
            return;
        }
        (void)push(step->push); // retail leaves the pushing frame's state unchanged
    }
};

} // namespace

std::uint8_t select_owner_root_play(std::int32_t game_mode,bool computer) noexcept {
    if (computer) {
        switch (game_mode) {
        case 0: case 1: case 3: case 6: return 15U;
        case 2: return 25U;
        case 4: case 5: case 7: case 9: return 2U;
        default: return 12U;
        }
    }
    return game_mode==2 ? 26U : 10U;
}

bool owner_play_supported(std::uint8_t play) noexcept { return !script_of(play).empty(); }

std::span<const RoleSlot> role_slots(std::uint8_t role) noexcept {
    switch (role) {
    case 1: return role1;
    case 2: return role2;
    case 3: return role3;
    case 4: return role4;
    case 5: return role5;
    case 6: return role6;
    case 7: return role7;
    case 8: return role8;
    case 9: return role9;
    case 10: return role10;
    default: return {};
    }
}

float score_member(SlotScorer scorer,const ScoreInput& input) noexcept {
    if (input.member==nullptr) return 0.0F;
    const MemberView& m=*input.member;
    const bool same=m.root_play==input.slot_play;
    switch (scorer) {
    case SlotScorer::first_candidate: return 0.0F;
    case SlotScorer::on_slot_play:
    case SlotScorer::keep_current: return same ? 1.0F : 0.0F;
    case SlotScorer::current_or_class4:
        if (same) return 1.0F;
        return (m.flags&4U)!=0U ? 1.0F : 0.0F;
    case SlotScorer::member_mode:
        if (same) return 1.0F;
        switch (m.mode) {
        case 2U: return (m.flags&4U)!=0U ? 0.1F : 0.8F;
        case 0U: return 0.9F;
        default: break;
        }
        return 0.5F;
    case SlotScorer::objective_distance: {
        if (same) return 1.0F;
        if (!input.objective_point) return 0.0F;
        const float d=horizontal_length(m.x-input.objective_point->x,m.z-input.objective_point->z);
        if (d<rv::objective_near) return d/rv::objective_scale+0.5F;
        switch (m.mode) {
        case 2U: return (m.flags&4U)!=0U ? 0.1F : 0.2F;
        case 0U: return 0.3F;
        case 1U: return 0.4F;
        default: break;
        }
        return 0.0F;
    }
    case SlotScorer::on_play_status_two: return same && m.status_two ? 1.0F : 0.0F;
    case SlotScorer::origin_proximity: {
        if (m.status_one) return 0.0F;
        float score=(m.state==8 || m.state==9) ? 0.5F : 0.0F;
        const float proximity=0.5F-horizontal_length(m.z,m.x)/rv::origin_scale;
        if (0.0F<proximity) score+=proximity;
        return score;
    }
    }
    return 0.0F;
}

AssignResult assign_member_plays(std::uint8_t role,bool owner_active,
                                 std::span<const MemberView,rv::squad_members> members,
                                 std::optional<Point2> objective_point) noexcept {
    AssignResult result{};
    if (!owner_active) {
        result.status=AssignStatus::owner_inactive;
        return result;
    }
    std::array<bool,rv::squad_members> open{};
    std::size_t remaining=0U;
    for (std::size_t n=0U;n<members.size();++n) {
        const auto& m=members[n];
        open[n]=m.in_use && m.unit_class!=rv::excluded_unit_class && m.state!=rv::excluded_member_state;
        if (open[n]) ++remaining;
    }
    const auto slots=role_slots(role);
    if (slots.empty()) {
        result.status=AssignStatus::role_without_table;
        return result;
    }
    if (remaining==0U) {
        result.status=AssignStatus::no_members;
        return result;
    }
    std::size_t slot=0U;
    while (remaining!=0U) {
        const RoleSlot& rule=slots[slot];
        std::optional<std::size_t> chosen{};
        float best=0.0F;
        for (std::size_t i=0U;i<members.size();++i) {
            if (!open[i]) continue;
            if (rule.scorer==SlotScorer::first_candidate) {
                chosen=i;
                break;
            }
            const float value=score_member(rule.scorer,ScoreInput{.member=&members[i],.slot_play=rule.play,
                                                                  .objective_point=objective_point});
            if (best<value) {
                best=value;
                chosen=i;
                if (1.0F<=best) break;
            }
        }
        if (chosen) {
            const auto& m=members[*chosen];
            if (m.root_play!=rule.play) {
                result.changes[result.change_count]=MemberPlayAssignment{
                    .unit=m.unit,.member_slot=static_cast<std::uint8_t>(*chosen),.play=rule.play};
                ++result.change_count;
            }
            open[*chosen]=false;
            --remaining;
            ++result.matched;
        }
        const bool can_advance=slot+1U<slots.size();
        if (can_advance) {
            ++slot;
        } else if (!chosen) {
            // Retail loops forever here; every retail table ends in a NULL
            // scorer, so this is unreachable with the shipped data.
            result.status=AssignStatus::stalled;
            return result;
        }
    }
    return result;
}

float normalized_elapsed_time(std::int32_t elapsed_ticks) noexcept {
    if (elapsed_ticks>=rv::elapsed_normalizer) return 1.0F;
    return static_cast<float>(elapsed_ticks)/static_cast<float>(rv::elapsed_normalizer);
}

std::optional<float> owner_parameter(const ParameterContext& context,std::uint16_t key) noexcept {
    if (context.world==nullptr) return std::nullopt;
    const OwnerWorld& w=*context.world;
    const std::uint8_t objective=context.objective;
    switch (key) {
    case 0x2104: case 0x2105: case 0x2106: case 0x2107:
        return team_lacks(w,static_cast<std::uint16_t>(key-0x2104U));
    case 0x2108: case 0x2109: case 0x210A: case 0x210B:
        return objective==static_cast<std::uint8_t>(key-0x2108U) ? 1.0F : 0.0F;
    case 0x210F: return flagged_weight(w,context.owner);
    case 0x2110: return flagged_weight(w,context.owner)>0.0F ? 0.0F : rv::flagged_member_weight;
    case 0x2111: {
        float total=0.0F;
        for (std::uint8_t i=0U;i<w.foreign_links;++i) total+=rv::chain_node_weight;
        return total;
    }
    case 0x2113: { // func_800E8B6C
        float weight=0.0F;
        if (objective!=rv::no_objective && objective<rv::owner_records && w.owners[objective].chain_active) {
            const auto& chain_state=w.owners[objective].chain_state;
            // TEMPORARY POLICY: retail dereferences the list unconditionally;
            // a missing list reads as "other state".
            if (chain_state && *chain_state==0) weight=rv::chain_state0_weight;
            else if (chain_state && *chain_state==1) weight=rv::chain_state1_weight;
        }
        return rv::inverse_base-weight;
    }
    case 0x2117: return rv::inverse_base-chain_weight(w,objective); // func_800E8A48
    case 0x8100:
        if (context.rng==nullptr) return std::nullopt;
        return context.rng->next_float(rv::random_range);
    case 0x8101: return normalized_elapsed_time(w.elapsed_ticks);
    case 0x810C: case 0x810D: case 0x810E: case 0x810F: {
        const std::size_t owner=static_cast<std::size_t>(key-0x810CU);
        return chain_weight(w,owner)+flagged_weight(w,owner);
    }
    case 0x8110: case 0x8111: case 0x8112: return chain_weight(w,static_cast<std::size_t>(key-0x8110U));
    case 0x8113: return flagged_weight(w,1U); // func_800E808C (squad slot 1 active)
    default: return std::nullopt;
    }
}

void OwnerPlayStack::serialize(StateWriter& writer) const {
    // Only live frames (below depth) are hashed; stale frames left above
    // depth after a pop must not change the hash. Matches campaign_ai PlayStack.
    writer.u8(depth);
    const std::size_t live=depth<frames.size() ? depth : frames.size();
    for (std::size_t i=0U;i<live;++i) {
        writer.u8(frames[i].play);
        writer.u8(static_cast<std::uint8_t>(frames[i].state));
    }
}

void OwnerPlayState::serialize(StateWriter& writer) const {
    stack.serialize(writer);
    writer.u8(role);
    writer.u8(objective);
}

OwnerPlayResult update_owner_plays(OwnerPlayState& state,const OwnerPlayInput& input,RetailRandom& rng) noexcept {
    OwnerPlayResult result{};
    if (!input.active) {
        result.status=OwnerPlayStatus::inactive;
        return result;
    }
    Runner runner{state,input,rng,result};
    runner.update();
    return result;
}

std::optional<OwnerPlayTuning> owner_tuning_from_wave(const CampaignWaveDefinition& wave,std::uint8_t skill_group,
                                                      std::uint8_t owner) noexcept {
    if (owner>=campaign_owner_slots || wave.owner_count<0 ||
        static_cast<std::uint32_t>(owner)>=static_cast<std::uint32_t>(wave.owner_count) ||
        skill_group>=campaign_skill_groups) return std::nullopt;
    const auto& setup=wave.owners[owner];
    const auto& skill=setup.skills[skill_group];
    return OwnerPlayTuning{.computer=setup.computer_controlled,.team=setup.team,
                           .skill_a=skill.aggressiveness,.skill_b=skill.intelligence};
}

} // namespace battletanx::owner_plays
