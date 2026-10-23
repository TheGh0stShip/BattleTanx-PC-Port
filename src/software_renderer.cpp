#include "battletanx/software_renderer.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <stdexcept>

namespace battletanx {
namespace {

struct RasterVertex {
    float x{},y{},z{},inverse_w{};
    float u_over_w{},v_over_w{};
    float r_over_w{},g_over_w{},b_over_w{},a_over_w{};
};

std::array<float,4> transform(const Matrix4& matrix,float x,float y,float z,float w) {
    return {matrix[0]*x+matrix[1]*y+matrix[2]*z+matrix[3]*w,
            matrix[4]*x+matrix[5]*y+matrix[6]*z+matrix[7]*w,
            matrix[8]*x+matrix[9]*y+matrix[10]*z+matrix[11]*w,
            matrix[12]*x+matrix[13]*y+matrix[14]*z+matrix[15]*w};
}

float edge(float ax,float ay,float bx,float by,float px,float py) {
    return (bx-px)*(ay-py)-(by-py)*(ax-px);
}

int wrapped_coordinate(float coordinate,std::uint32_t size,TextureWrap wrap) {
    int value=static_cast<int>(std::floor(coordinate));
    const int extent=static_cast<int>(size);
    if (wrap==TextureWrap::clamp) return std::clamp(value,0,extent-1);
    if (wrap==TextureWrap::repeat) {
        value%=extent;
        return value<0 ? value+extent : value;
    }
    const int period=extent*2;
    value%=period;
    if (value<0) value+=period;
    return value<extent ? value : period-1-value;
}

std::uint32_t texel(const NativeMaterial& material,int x,int y) {
    const auto& image=*material.texture;
    const int wrapped_x=wrapped_coordinate(static_cast<float>(x),image.width,material.wrap_s);
    const int wrapped_y=wrapped_coordinate(static_cast<float>(y),image.height,material.wrap_t);
    return image.pixels[static_cast<std::size_t>(wrapped_y)*image.width+
                        static_cast<std::size_t>(wrapped_x)];
}

std::uint32_t sample(const NativeMaterial& material,float u,float v) {
    if (!material.texture) return material.primitive_rgba;
    u-=material.tile_left;
    v-=material.tile_top;
    const int x=static_cast<int>(std::floor(u));
    const int y=static_cast<int>(std::floor(v));
    if (!material.bilinear) return texel(material,x,y);
    const float fraction_x=u-static_cast<float>(x);
    const float fraction_y=v-static_cast<float>(y);
    const std::array<std::uint32_t,4> samples{
        texel(material,x,y),texel(material,x+1,y),
        texel(material,x,y+1),texel(material,x+1,y+1)};
    std::uint32_t output=0U;
    for (const unsigned shift:{0U,8U,16U,24U}) {
        const float top=((samples[0]>>shift)&0xFFU)*(1.0F-fraction_x)+
                        ((samples[1]>>shift)&0xFFU)*fraction_x;
        const float bottom=((samples[2]>>shift)&0xFFU)*(1.0F-fraction_x)+
                           ((samples[3]>>shift)&0xFFU)*fraction_x;
        const auto value=static_cast<std::uint32_t>(
            std::clamp(top*(1.0F-fraction_y)+bottom*fraction_y,0.0F,255.0F));
        output|=value<<shift;
    }
    return output;
}

std::uint32_t channel(std::uint32_t color,unsigned shift) {
    return (color>>shift)&0xFFU;
}

std::uint32_t multiply_color(std::uint32_t texture,std::uint32_t shade) {
    const auto product=[&](unsigned shift) {
        return channel(texture,shift)*channel(shade,shift)/255U;
    };
    return (product(24U)<<24U)|(product(16U)<<16U)|(product(8U)<<8U)|product(0U);
}

std::uint32_t pack_color(float red,float green,float blue,float alpha) {
    const auto byte=[](float value) {
        return static_cast<std::uint32_t>(std::clamp(value,0.0F,255.0F));
    };
    return (byte(alpha)<<24U)|(byte(red)<<16U)|(byte(green)<<8U)|byte(blue);
}

std::uint32_t combine(const NativeMaterial& material,std::uint32_t texture,
                      std::uint32_t shade) {
    switch (material.combine) {
    case CombineMode::texture_times_shade: return multiply_color(texture,shade);
    case CombineMode::primitive: return material.primitive_rgba;
    case CombineMode::texture: return texture;
    case CombineMode::primitive_environment_lerp:
        return multiply_color(texture,material.primitive_rgba);
    }
    return texture;
}

} // namespace

SoftwareRenderer::SoftwareRenderer(int width,int height)
    : width_(width),height_(height),
      color_(static_cast<std::size_t>(width)*height),
      depth_(static_cast<std::size_t>(width)*height) {
    if (width<=0 || height<=0) throw std::runtime_error("render target size is invalid");
}

std::size_t select_model_lod(std::uint8_t lod_count,float distance_squared) noexcept {
    if (lod_count<=1U) return 0U;
    if (lod_count==2U) return distance_squared>90000.0F ? 1U : 0U;
    if (distance_squared>2890000.0F) return 2U;
    return distance_squared>1000000.0F ? 1U : 0U;
}

void SoftwareRenderer::render(const DrawList& list,const ModelAssets& assets) {
    std::fill(color_.begin(),color_.end(),0x00101820U);
    std::fill(depth_.begin(),depth_.end(),std::numeric_limits<float>::infinity());
    const Matrix4 view_projection=multiply_matrix(list.projection,list.view);
    constexpr std::array<std::uint8_t,7> layer_order{0U,6U,5U,1U,2U,3U,4U};
    for (const std::uint8_t layer:layer_order) {
        for (const auto& item:list.items) {
            if (item.layer!=layer) continue;
            const auto& set=assets.set(item.set);
            if (set.lod_count==0U) continue;
            const Matrix4 transform_matrix=multiply_matrix(view_projection,item.model);
            const float distance_x=item.model[3]-list.eye.x;
            const float distance_y=item.model[7]-list.eye.y;
            const float distance_z=item.model[11]-list.eye.z;
            const std::size_t lod=select_model_lod(
                set.lod_count,distance_x*distance_x+distance_y*distance_y+
                              distance_z*distance_z);
            for (const auto& part:set.lods[lod]) {
                const auto& mesh=assets.meshes()[part.mesh];
                const auto& material=assets.materials()[part.material];
                for (std::size_t triangle=0U; triangle+2U<mesh.indices.size(); triangle+=3U) {
                    std::array<RasterVertex,3> vertices{};
                    bool clipped=false;
                    for (std::size_t corner=0U; corner<3U; ++corner) {
                        const auto& source=mesh.vertices[mesh.indices[triangle+corner]];
                        const auto clip=transform(
                            transform_matrix,source.x,source.y,source.z,1.0F);
                        if (!std::isfinite(clip[3]) || clip[3]<=0.000001F ||
                            clip[2]<-clip[3] || clip[2]>clip[3]) {
                            clipped=true;
                            break;
                        }
                        const float inverse_w=1.0F/clip[3];
                        vertices[corner]={
                            (clip[0]*inverse_w*0.5F+0.5F)*width_,
                            (0.5F-clip[1]*inverse_w*0.5F)*height_,
                            clip[2]*inverse_w,inverse_w,
                            source.u*inverse_w,source.v*inverse_w,
                            source.r*inverse_w,source.g*inverse_w,
                            source.b*inverse_w,source.a*inverse_w};
                    }
                    if (clipped) continue;
                    const float area=edge(vertices[0].x,vertices[0].y,
                                           vertices[1].x,vertices[1].y,
                                           vertices[2].x,vertices[2].y);
                    if (std::abs(area)<=0.000001F ||
                        (material.cull==CullMode::back && area>0.0F)) continue;
                    const int minimum_x=std::max(0,static_cast<int>(std::floor(std::min(
                        {vertices[0].x,vertices[1].x,vertices[2].x}))));
                    const int maximum_x=std::min(width_-1,static_cast<int>(std::ceil(std::max(
                        {vertices[0].x,vertices[1].x,vertices[2].x}))));
                    const int minimum_y=std::max(0,static_cast<int>(std::floor(std::min(
                        {vertices[0].y,vertices[1].y,vertices[2].y}))));
                    const int maximum_y=std::min(height_-1,static_cast<int>(std::ceil(std::max(
                        {vertices[0].y,vertices[1].y,vertices[2].y}))));
                    for (int y=minimum_y; y<=maximum_y; ++y) {
                        for (int x=minimum_x; x<=maximum_x; ++x) {
                            const float pixel_x=static_cast<float>(x)+0.5F;
                            const float pixel_y=static_cast<float>(y)+0.5F;
                            const float weight0=edge(vertices[1].x,vertices[1].y,
                                vertices[2].x,vertices[2].y,pixel_x,pixel_y)/area;
                            const float weight1=edge(vertices[2].x,vertices[2].y,
                                vertices[0].x,vertices[0].y,pixel_x,pixel_y)/area;
                            const float weight2=1.0F-weight0-weight1;
                            if (weight0<0.0F || weight1<0.0F || weight2<0.0F) continue;
                            const float depth=weight0*vertices[0].z+
                                weight1*vertices[1].z+weight2*vertices[2].z;
                            const std::size_t pixel=static_cast<std::size_t>(y)*width_+x;
                            if (material.depth_test && !(depth<depth_[pixel])) continue;
                            const float reciprocal=1.0F/(weight0*vertices[0].inverse_w+
                                weight1*vertices[1].inverse_w+weight2*vertices[2].inverse_w);
                            const auto interpolate=[&](float RasterVertex::*member) {
                                return (weight0*vertices[0].*member+weight1*vertices[1].*member+
                                        weight2*vertices[2].*member)*reciprocal;
                            };
                            const float u=interpolate(&RasterVertex::u_over_w);
                            const float v=interpolate(&RasterVertex::v_over_w);
                            const std::uint32_t shade=pack_color(
                                interpolate(&RasterVertex::r_over_w),
                                interpolate(&RasterVertex::g_over_w),
                                interpolate(&RasterVertex::b_over_w),
                                interpolate(&RasterVertex::a_over_w));
                            const std::uint32_t output=combine(
                                material,sample(material,u,v),shade);
                            if (material.alpha==AlphaMode::edge_test && (output>>24U)<128U) continue;
                            if (material.depth_test) depth_[pixel]=depth;
                            color_[pixel]=output&0x00FFFFFFU;
                        }
                    }
                }
            }
        }
    }
}

} // namespace battletanx
