#pragma once

#include "battletanx/frontend_model.hpp"

#include <string>
#include <vector>

namespace battletanx::frontend {

struct MenuRowView {
    std::string label;
    std::string value;
    bool selected{};
    bool enabled{true};
};

struct MenuView {
    std::string title;
    std::vector<MenuRowView> rows;
    std::string footer;
};

[[nodiscard]] MenuView make_menu_view(const FrontendState& state);

} // namespace battletanx::frontend
