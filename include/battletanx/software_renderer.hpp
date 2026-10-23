#pragma once

#include "battletanx/model.hpp"
#include "battletanx/render_math.hpp"

#include <array>
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
    CameraVector eye{};
    std::uint64_t animation_tick{};
    std::vector<DrawItem> items;
};

std::size_t select_model_lod(std::uint8_t lod_count,float distance_squared) noexcept;
std::int8_t decode_world_animation_phase(std::uint8_t kind) noexcept;
std::uint32_t alpha_blend(std::uint32_t source,std::uint32_t destination) noexcept;
std::size_t clipped_view_volume_vertex_count(
    const std::array<std::array<float,4>,3>& triangle) noexcept;

class SoftwareRenderer {
  public:
    SoftwareRenderer(int width,int height);
    void render(const DrawList& list,const ModelAssets& assets);
    [[nodiscard]] std::span<const std::uint32_t> pixels() const noexcept { return color_; }
    [[nodiscard]] std::span<std::uint32_t> overlay_pixels() noexcept { return color_; }

  private:
    int width_{};
    int height_{};
    std::vector<std::uint32_t> color_;
    std::vector<float> depth_;
};

} // namespace battletanx
