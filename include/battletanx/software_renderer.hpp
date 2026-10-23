#pragma once

#include "battletanx/model.hpp"
#include "battletanx/render_math.hpp"

#include <cstdint>
#include <span>
#include <vector>

namespace battletanx {

struct DrawItem {
    std::uint32_t set{};
    Matrix4 model{identity_matrix()};
    std::uint8_t layer{};
    std::int8_t animation_phase{};
};

struct DrawList {
    Matrix4 view{identity_matrix()};
    Matrix4 projection{identity_matrix()};
    std::vector<DrawItem> items;
};

class SoftwareRenderer {
  public:
    SoftwareRenderer(int width,int height);
    void render(const DrawList& list,const ModelAssets& assets);
    [[nodiscard]] std::span<const std::uint32_t> pixels() const noexcept { return color_; }

  private:
    int width_{};
    int height_{};
    std::vector<std::uint32_t> color_;
    std::vector<float> depth_;
};

} // namespace battletanx
