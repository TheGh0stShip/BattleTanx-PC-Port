#pragma once

#include "battletanx/angle.hpp"
#include "battletanx/level.hpp"
#include "battletanx/model.hpp"

#include <array>
#include <optional>
#include <vector>

namespace battletanx {

struct TerrainVertex { float x{},y{},z{}; };
struct TerrainTriangle { std::array<TerrainVertex,3> vertices; };

std::optional<float> terrain_triangle_height(
    const TerrainTriangle& triangle,float x,float z) noexcept;

class TerrainSurface {
  public:
    static TerrainSurface build(const WorldLevel& world,const ModelAssets& assets,
                                const AngleTable& angles);
    [[nodiscard]] std::optional<float> height(float x,float z) const noexcept;
    [[nodiscard]] std::size_t triangle_count() const noexcept { return triangles_.size(); }

  private:
    std::vector<TerrainTriangle> triangles_;
};

} // namespace battletanx
