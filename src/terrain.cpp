#include "battletanx/terrain.hpp"

#include <algorithm>
#include <cmath>

namespace battletanx {

std::optional<float> terrain_triangle_height(
    const TerrainTriangle& triangle,float x,float z) noexcept {
    const auto& a=triangle.vertices[0];
    const auto& b=triangle.vertices[1];
    const auto& c=triangle.vertices[2];
    const float denominator=(b.z-c.z)*(a.x-c.x)+(c.x-b.x)*(a.z-c.z);
    if (std::abs(denominator)<0.000001F) return std::nullopt;
    const float first=((b.z-c.z)*(x-c.x)+(c.x-b.x)*(z-c.z))/denominator;
    const float second=((c.z-a.z)*(x-c.x)+(a.x-c.x)*(z-c.z))/denominator;
    const float third=1.0F-first-second;
    constexpr float tolerance=-0.0001F;
    if (first<tolerance || second<tolerance || third<tolerance) return std::nullopt;
    return first*a.y+second*b.y+third*c.y;
}

TerrainSurface TerrainSurface::build(const WorldLevel& world,const ModelAssets& assets,
                                     const AngleTable& angles) {
    TerrainSurface surface;
    for (const auto& item:world.items) {
        if (item.type!=0 || item.shape<0) continue;
        const auto& set=assets.set(static_cast<std::size_t>(item.shape));
        if (set.lod_count==0U) continue;
        const float sine=angles.sine(item.angle);
        const float cosine=angles.cosine(item.angle);
        for (const auto& part:set.lods[0]) {
            const auto& mesh=assets.meshes()[part.mesh];
            for (std::size_t index=0U; index+2U<mesh.indices.size(); index+=3U) {
                TerrainTriangle triangle;
                for (std::size_t corner=0U; corner<3U; ++corner) {
                    const auto& vertex=mesh.vertices[mesh.indices[index+corner]];
                    triangle.vertices[corner]={
                        item.x+vertex.x*cosine+vertex.z*sine,
                        vertex.y,
                        item.z-vertex.x*sine+vertex.z*cosine};
                }
                surface.triangles_.push_back(triangle);
            }
        }
    }
    return surface;
}

std::optional<float> TerrainSurface::height(float x,float z) const noexcept {
    std::optional<float> result;
    for (const auto& triangle:triangles_) {
        const auto candidate=terrain_triangle_height(triangle,x,z);
        if (candidate && (!result || *candidate>*result)) result=candidate;
    }
    return result;
}

} // namespace battletanx
