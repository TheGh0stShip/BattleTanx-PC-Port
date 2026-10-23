#include "battletanx/frontend_view.hpp"
#include "runtime_test_support.hpp"

using namespace battletanx;
using namespace battletanx::frontend;

namespace {
FrontendStep press(FrontendState& state,std::uint32_t buttons) {
    return update_frontend(state,{.pressed=buttons,.raw_pressed=0U});
}
}

RUNTIME_TEST(title_and_setup_views_follow_the_frontend_state) {
    FrontendState state=make_frontend_state();
    (void)apply_pak_probe(state,PakProbe::ok_no_save,nullptr);
    auto view=make_menu_view(state);
    REQUIRE(view.title=="BATTLETANX");
    REQUIRE(view.rows.size()==2U);
    REQUIRE(view.rows[0].label=="START" && view.rows[0].selected);
    REQUIRE(!view.rows[1].enabled);
    (void)press(state,buttons::confirm);
    view=make_menu_view(state);
    REQUIRE(view.title=="GAME SETUP");
    REQUIRE(view.rows.size()==7U);
    REQUIRE(view.rows[0].label=="PLAYERS" && view.rows[0].value=="1");
    REQUIRE(view.rows[1].value=="CAMPAIGN");
    REQUIRE(!view.rows[5].enabled);
    REQUIRE(view.rows[6].selected);
    return true;
}

RUNTIME_TEST(options_view_exposes_values_and_selection) {
    FrontendState state=make_frontend_state();
    state.screen=Screen::options;
    state.record.sfx_volume=3;
    state.record.music_volume=5;
    state.record.difficulty=CampaignDifficulty::hard;
    state.record.powerups.raw=1U;
    const auto view=make_menu_view(state);
    REQUIRE(view.title=="OPTIONS");
    REQUIRE(view.rows.size()==8U);
    REQUIRE(view.rows[0].selected && view.rows[0].value=="3");
    REQUIRE(view.rows[1].value=="5");
    REQUIRE(view.rows[2].value=="HARD");
    REQUIRE(view.rows[3].value=="ON");
    REQUIRE(view.rows[7].label=="EXIT");
    return true;
}

RUNTIME_TEST(controller_view_uses_the_saved_binding_words) {
    FrontendState state=make_frontend_state();
    state.screen=Screen::controller_preset;
    state.controls.preset_saved=0;
    state.controls.preset_selected=0;
    state.controls.words=frontend_setup::preset_bindings[0];
    const auto view=make_menu_view(state);
    REQUIRE(view.title=="CONTROLLER SETUP");
    REQUIRE(view.rows.size()==10U);
    REQUIRE(view.rows[0].label=="CONTROL 1");
    REQUIRE(view.rows[0].value=="Z");
    REQUIRE(view.rows[8].label=="PRESET" && view.rows[8].value=="1");
    REQUIRE(view.rows[9].label=="DONE");
    REQUIRE(!view.footer.empty());
    return true;
}

int main() { return runtime_test::run_all(); }
