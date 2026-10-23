#include "battletanx/angle.hpp"
#include "battletanx/big_endian.hpp"
#include "battletanx/bundle.hpp"
#include "battletanx/camera.hpp"
#include "battletanx/controller_pak.hpp"
#include "battletanx/gameplay.hpp"
#include "battletanx/fixed_tick.hpp"
#include "battletanx/geometry.hpp"
#include "battletanx/host_input.hpp"
#include "battletanx/lzari.hpp"
#include "battletanx/material.hpp"
#include "battletanx/model.hpp"
#include "battletanx/level.hpp"
#include "battletanx/rom.hpp"
#include "battletanx/resource.hpp"
#include "battletanx/render_math.hpp"
#include "battletanx/sha256.hpp"
#include "battletanx/software_renderer.hpp"
#include "battletanx/texture.hpp"
#include "battletanx/terrain.hpp"

#include <array>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string_view>
#include <vector>

namespace {
void put_u16(std::vector<std::uint8_t>& bytes,std::size_t offset,std::uint16_t value) {
    bytes[offset]=static_cast<std::uint8_t>(value>>8U);
    bytes[offset+1U]=static_cast<std::uint8_t>(value);
}
void put_u32(std::vector<std::uint8_t>& bytes,std::size_t offset,std::uint32_t value) {
    bytes[offset]=static_cast<std::uint8_t>(value>>24U);
    bytes[offset+1U]=static_cast<std::uint8_t>(value>>16U);
    bytes[offset+2U]=static_cast<std::uint8_t>(value>>8U);
    bytes[offset+3U]=static_cast<std::uint8_t>(value);
}
} // namespace

#define CHECK(expression) \
    do { \
        if (!(expression)) { \
            std::cerr << "check failed at " << __FILE__ << ':' << __LINE__ \
                      << ": " #expression "\n"; \
            return 1; \
        } \
    } while (false)

int main() {
    CHECK(battletanx::fixed_rate_tick(0U,30U)==0U);
    CHECK(battletanx::fixed_rate_tick(33333333U,30U)==0U);
    CHECK(battletanx::fixed_rate_tick(33333334U,30U)==1U);
    CHECK(battletanx::fixed_rate_tick(1000000000U,30U)==30U);
    CHECK(battletanx::fixed_rate_tick(1500000000U,30U)==45U);
    CHECK(battletanx::fixed_rate_tick(UINT64_MAX,0U)==0U);
    CHECK(battletanx::fixed_rate_tick(UINT64_MAX,30U)>UINT64_C(553000000000));
    CHECK(battletanx::select_model_lod(1U,9999999.0F)==0U);
    CHECK(battletanx::select_model_lod(2U,90000.0F)==0U);
    CHECK(battletanx::select_model_lod(2U,90001.0F)==1U);
    CHECK(battletanx::select_model_lod(3U,1000000.0F)==0U);
    CHECK(battletanx::select_model_lod(3U,1000001.0F)==1U);
    CHECK(battletanx::select_model_lod(3U,2890001.0F)==2U);
    CHECK(battletanx::decode_world_animation_phase(0x03U)==3);
    CHECK(battletanx::decode_world_animation_phase(0xAEU)==-2);
    CHECK(battletanx::decode_world_animation_phase(0xFFU)==-1);
    CHECK(battletanx::clipped_view_volume_vertex_count(
        {{{0.0F,0.0F,0.0F,1.0F},{0.5F,0.0F,0.0F,1.0F},
          {0.0F,0.5F,0.0F,1.0F}}})==3U);
    CHECK(battletanx::clipped_view_volume_vertex_count(
        {{{0.0F,0.0F,-2.0F,1.0F},{0.5F,0.0F,0.0F,1.0F},
          {0.0F,0.5F,0.0F,1.0F}}})==4U);
    CHECK(battletanx::clipped_view_volume_vertex_count(
        {{{0.0F,0.0F,-2.0F,1.0F},{0.5F,0.0F,-2.0F,1.0F},
          {0.0F,0.5F,-2.0F,1.0F}}})==0U);
    CHECK(battletanx::clipped_view_volume_vertex_count(
        {{{2.0F,0.0F,0.0F,1.0F},{0.0F,0.5F,0.0F,1.0F},
          {0.0F,-0.5F,0.0F,1.0F}}})==4U);
    CHECK(battletanx::clipped_view_volume_vertex_count(
        {{{0.0F,0.0F,2.0F,1.0F},{0.5F,0.0F,2.0F,1.0F},
          {0.0F,0.5F,2.0F,1.0F}}})==0U);
    {
        const battletanx::TerrainTriangle triangle{{{{0.0F,10.0F,0.0F},
                                                      {10.0F,20.0F,0.0F},
                                                      {0.0F,30.0F,10.0F}}}};
        const auto height=battletanx::terrain_triangle_height(triangle,2.5F,2.5F);
        CHECK(height && std::abs(*height-17.5F)<0.00001F);
        const auto sample=battletanx::terrain_triangle_sample(triangle,2.5F,2.5F);
        CHECK(sample && std::abs(sample->height-17.5F)<0.00001F);
        CHECK(sample->normal[1]>0.0F);
        CHECK(!battletanx::terrain_triangle_height(triangle,20.0F,20.0F));
    }
    {
        const auto projection=battletanx::perspective_matrix(
            37.0F,4.0F/3.0F,16.0F,5000.0F);
        CHECK(std::abs(projection[0]-2.24151F)<0.0001F);
        CHECK(std::abs(projection[5]-2.98868F)<0.0001F);
        CHECK(std::abs(projection[10]+1.00642F)<0.0001F);
        CHECK(std::abs(projection[11]+32.1027F)<0.001F);
        std::array<float,battletanx::AngleTable::value_count> angle_samples{};
        angle_samples[64U]=1.0F;
        const battletanx::AngleTable test_angles(angle_samples);
        const auto aligned=battletanx::terrain_yaw_translation_matrix(
            0U,10.0F,20.0F,30.0F,{0.0F,1.0F,0.0F},test_angles);
        CHECK(aligned[0]==1.0F && aligned[5]==1.0F && aligned[10]==1.0F);
        CHECK(aligned[3]==10.0F && aligned[7]==20.0F && aligned[11]==30.0F);
        const auto sloped=battletanx::terrain_yaw_translation_matrix(
            0U,0.0F,0.0F,0.0F,{0.0F,1.0F,1.0F},test_angles);
        CHECK(std::abs(sloped[1])<0.00001F);
        CHECK(std::abs(sloped[5]-0.7071067F)<0.0001F);
        CHECK(std::abs(sloped[9]-0.7071067F)<0.0001F);
    }

    {
        std::array<std::uint8_t,8> pool{};
        const battletanx::BlobView blob(pool,{2U,4U});
        CHECK(blob.bytes().size()==4U && blob.at(1U,2U).size()==2U);
        bool rejected=false;
        try { static_cast<void>(battletanx::BlobView(pool,{7U,2U})); }
        catch (const std::out_of_range&) { rejected=true; }
        CHECK(rejected);

        std::vector<std::uint8_t> geometry(0x48U);
        put_u32(geometry,0U,0x04000C2FU);
        put_u32(geometry,4U,0x18U);
        put_u32(geometry,8U,0xBF000000U);
        put_u32(geometry,12U,0x00000204U);
        put_u32(geometry,16U,0xB8000000U);
        put_u16(geometry,0x18U,10U);
        put_u16(geometry,0x1AU,20U);
        put_u16(geometry,0x1CU,static_cast<std::uint16_t>(-30));
        put_u16(geometry,0x20U,64U);
        put_u16(geometry,0x22U,128U);
        geometry[0x24U]=255U;
        geometry[0x25U]=128U;
        geometry[0x26U]=64U;
        geometry[0x27U]=255U;
        const auto mesh=battletanx::decode_geometry_blob(geometry);
        CHECK(mesh.vertices.size()==3U && mesh.indices.size()==3U);
        CHECK(mesh.vertices[0].x==10.0F && mesh.vertices[0].y==20.0F &&
              mesh.vertices[0].z==-30.0F);
        CHECK(mesh.vertices[0].u==1.0F && mesh.vertices[0].v==2.0F);
        CHECK(mesh.indices[0]==0U && mesh.indices[1]==1U && mesh.indices[2]==2U);
        const auto scaled_mesh=battletanx::decode_geometry_blob(geometry,1.92F);
        CHECK(scaled_mesh.vertices[0].r==255U && scaled_mesh.vertices[0].g==245U &&
              scaled_mesh.vertices[0].b==122U && scaled_mesh.vertices[0].a==255U);
        geometry[15U]=6U;
        rejected=false;
        try { static_cast<void>(battletanx::decode_geometry_blob(geometry)); }
        catch (const std::runtime_error&) { rejected=true; }
        CHECK(rejected);
    }

    {
        constexpr std::array<std::uint8_t,4> rgba16{0xF8U,0x01U,0x07U,0xC1U};
        const auto rgba=battletanx::decode_rgba16_texture(rgba16,2U,1U);
        CHECK(rgba.pixels[0]==0xFFFF0000U && rgba.pixels[1]==0xFF00FF00U);
        constexpr std::array<std::uint8_t,1> transparent_red{0xF8U};
        bool rejected=false;
        try { static_cast<void>(
            battletanx::decode_rgba16_texture(transparent_red,1U,1U)); }
        catch (const std::runtime_error&) { rejected=true; }
        CHECK(rejected);
        constexpr std::array<std::uint8_t,2> transparent_red_full{0xF8U,0x00U};
        CHECK(battletanx::decode_rgba16_texture(
            transparent_red_full,1U,1U).pixels[0]==0x00FF0000U);

        std::array<std::uint8_t,32> palette{};
        palette[0]=0x00U;
        palette[1]=0x01U;
        palette[2]=0xFFU;
        palette[3]=0xFFU;
        constexpr std::array<std::uint8_t,2> indices{0x01U,0x10U};
        const auto ci=battletanx::decode_ci4_texture(indices,palette,2U,2U);
        CHECK(ci.pixels[0]==0xFF000000U && ci.pixels[1]==0xFFFFFFFFU &&
              ci.pixels[2]==0xFFFFFFFFU && ci.pixels[3]==0xFF000000U);
        constexpr std::array<std::uint8_t,1> intensities{0xF0U};
        const auto i4=battletanx::decode_i4_texture(intensities,2U,1U);
        CHECK(i4.pixels[0]==0xFFFFFFFFU && i4.pixels[1]==0xFF000000U);
        constexpr std::array<std::uint8_t,2> rgba32{0U,0U};
        rejected=false;
        try { static_cast<void>(battletanx::decode_rgba32_texture(rgba32,1U,1U)); }
        catch (const std::runtime_error&) { rejected=true; }
        CHECK(rejected);
        rejected=false;
        try { static_cast<void>(battletanx::decode_i8_texture({},2048U,1U)); }
        catch (const std::runtime_error&) { rejected=true; }
        CHECK(rejected);
    }

    {
        std::vector<std::uint8_t> material(24U);
        put_u32(material,0U,0xB900031DU);
        put_u32(material,4U,0x00553078U);
        put_u32(material,8U,0xFCFFFFFFU);
        put_u32(material,12U,0xFFFDF6FBU);
        put_u32(material,16U,0xB8000000U);
        const auto decoded=battletanx::decode_material_blob(material,0U);
        CHECK(decoded.alpha==battletanx::AlphaMode::edge_test);
        CHECK(decoded.combine==battletanx::CombineMode::primitive);
        CHECK(!decoded.texture);
        material[8U]=0xAAU;
        bool rejected=false;
        try { static_cast<void>(battletanx::decode_material_blob(material,0U)); }
        catch (const std::runtime_error&) { rejected=true; }
        CHECK(rejected);

        battletanx::NativeMaterial animated;
        animated.tile_left=7.0F;
        animated.tile_top=9.0F;
        animated.animation_frames={
            {0xF200400CU,0U},
            {0xF2008010U,0U}};
        const auto fixed=battletanx::resolve_material_tile_origin(animated,1,0U);
        CHECK(fixed.left==2.0F && fixed.top==4.0F);
        const auto looping=battletanx::resolve_material_tile_origin(animated,-1,2U);
        CHECK(looping.left==1.0F && looping.top==3.0F);
        const auto half_speed=battletanx::resolve_material_tile_origin(animated,-2,3U);
        CHECK(half_speed.left==2.0F && half_speed.top==4.0F);
    }

    {
        battletanx::ControllerPakRecord record;
        record.bytes[4]=1U;
        record.bytes[0]=0U;
        record.bytes[1]=0U;
        record.bytes[2]=0U;
        record.bytes[3]=1U;
        CHECK(battletanx::controller_pak_checksum(record)==1U);
        CHECK(battletanx::controller_pak_checksum_valid(record));
        record.bytes[0xFFU]=0xFEU;
        CHECK(!battletanx::controller_pak_checksum_valid(record));

        const auto path=std::filesystem::temp_directory_path()/
                        "battletanx-controller-pak-test.mpk";
        std::filesystem::remove(path);
        CHECK(!battletanx::load_controller_pak_record(path));
        battletanx::write_controller_pak_record(path,record);
        CHECK(std::filesystem::file_size(path)==32U*1024U);
        const auto loaded=battletanx::load_controller_pak_record(path);
        CHECK(loaded && loaded->bytes[4]==1U && loaded->bytes[0xFFU]==0xFEU);
        CHECK(battletanx::controller_pak_checksum_valid(*loaded));
        {
            std::fstream image(path,std::ios::binary|std::ios::in|std::ios::out);
            image.seekp(5U*256U+4U);
            image.put(static_cast<char>(0x7FU));
        }
        bool corrupt_rejected=false;
        try { static_cast<void>(battletanx::load_controller_pak_record(path)); }
        catch (const std::runtime_error&) { corrupt_rejected=true; }
        CHECK(corrupt_rejected);
        std::filesystem::remove(path);
    }

    {
        const auto descriptors=battletanx::level_stream_descriptors();
        CHECK(descriptors.size()==22U);
        CHECK(descriptors.front().index==0 && descriptors.back().index==27);
        std::size_t navigation_count=0U;
        for (const auto& descriptor : descriptors) {
            CHECK(descriptor.world.start<descriptor.world.end);
            CHECK(descriptor.world.end<=battletanx::Rom::expected_size);
            if (descriptor.navigation) {
                ++navigation_count;
                CHECK(descriptor.navigation->start<descriptor.navigation->end);
                CHECK(descriptor.navigation->end<=battletanx::Rom::expected_size);
            }
        }
        CHECK(navigation_count==18U);

        std::vector<std::uint8_t> world(0xBD0U);
        constexpr std::array<std::uint32_t,9> offsets{
            0x24U,0x304U,0x318U,0x334U,0xBBCU,0xBBCU,0xBC0U,0xBD0U,0xBD0U};
        for (std::size_t index=0; index<offsets.size(); ++index) {
            put_u32(world,index*4U,offsets[index]);
        }
        put_u32(world,0x24U+0x20U,0x12345678U);
        put_u32(world,0x24U+0x2DCU,0U);
        put_u32(world,0x24U+0x27CU,1U);
        put_u32(world,0x24U+0x280U,0xFFFFFFFFU);
        put_u16(world,0x304U,1U);
        put_u16(world,0x306U,0U);
        world[0x318U]=0U;
        world[0x319U]=7U;
        put_u32(world,0x320U,3U);
        put_u32(world,0x324U,0x3F800000U);
        put_u32(world,0x328U,0xC0200000U);
        put_u16(world,0x32CU,0x4000U);
        put_u16(world,0x32EU,0xFFFFU);
        put_u16(world,0x330U,0U);
        for (std::size_t shape=0; shape<182U; ++shape) {
            world[0x334U+shape*0xCU]=1U;
            put_u16(world,0x336U+shape*0xCU,0U);
        }
        put_u16(world,0x338U,0xFFF6U);
        put_u16(world,0x33AU,0xFFECU);
        put_u16(world,0x33CU,30U);
        put_u16(world,0x33EU,40U);
        world[0xBBCU]=1U;
        put_u16(world,0xBBEU,0U);
        put_u32(world,0xBC0U,0U);
        put_u16(world,0xBC4U,0U);
        put_u16(world,0xBC6U,8U);
        put_u32(world,0xBC8U,0U);
        put_u32(world,0xBCCU,8U);
        const auto parsed=battletanx::parse_world_level(world);
        CHECK(parsed.flags==0x12345678U);
        CHECK(parsed.groups.size()==1U && parsed.groups[0].item_count==1U);
        CHECK(parsed.items.size()==1U && parsed.items[0].kind==7U);
        CHECK(parsed.items[0].x==1.0F && parsed.items[0].z==-2.5F);
        CHECK(parsed.shapes.size()==182U && parsed.shapes[0].first_range==0U);
        CHECK(parsed.shapes[0].min_x==-10 && parsed.shapes[0].max_z==40);
        CHECK(parsed.model_ranges.size()==1U && parsed.model_ranges[0].part_count==1U);
        CHECK(parsed.model_parts.size()==1U && parsed.model_parts[0].geometry_size==8U);

        std::vector<std::uint8_t> navigation(0x162U);
        put_u32(navigation,0U,static_cast<std::uint32_t>(navigation.size()));
        put_u32(navigation,4U,static_cast<std::uint32_t>(navigation.size()));
        put_u16(navigation,8U,4U);
        put_u16(navigation,0xAU,2U);
        put_u32(navigation,0xCU,0x28U);
        put_u32(navigation,0x10U,0x48U);
        put_u32(navigation,0x14U,0x48U);
        put_u32(navigation,0x18U,0x60U);
        put_u32(navigation,0x1CU,0x62U);
        put_u32(navigation,0x20U,0x02000000U);
        put_u32(navigation,0x30U,0x00000000U);
        put_u32(navigation,0x34U,0x00000000U);
        put_u32(navigation,0x38U,0x00000000U);
        put_u32(navigation,0x3CU,0x40C00000U);
        put_u32(navigation,0x40U,0x40C00000U);
        put_u32(navigation,0x44U,0x00000000U);
        put_u16(navigation,0x54U,1U);
        put_u16(navigation,0x58U,2U);
        put_u16(navigation,0x5CU,3U);
        navigation[0x61U]=5U;
        navigation[0x62U+5U]=1U;
        auto nav=battletanx::parse_navigation_level(navigation);
        CHECK(nav.vertices.size()==4U && nav.vertices[2].x==0.0F && nav.vertices[2].z==6.0F);
        CHECK(nav.triangles.size()==2U && nav.triangles[1].kind==5U);
        CHECK(nav.triangles[1].corners[2].vertex==3U && nav.kind_enabled[5]==1U);
        const auto center=battletanx::navigation_triangle_centroid(nav,1U);
        CHECK(center.x==2.0F && center.z==2.0F);
        const auto midpoint=battletanx::navigation_edge_midpoint(nav,3U);
        CHECK(midpoint.x==0.0F && midpoint.z==3.0F);
        CHECK(battletanx::navigation_point_inside_triangle(nav,{1.0F,1.0F},1U));
        CHECK(!battletanx::navigation_point_inside_triangle(nav,{5.0F,5.0F},1U));
        CHECK(battletanx::find_nearest_navigation_triangle(nav,{2.0F,2.0F})==0U);
        nav.kind_enabled[5]=0U;
        CHECK(battletanx::find_nearest_navigation_triangle(nav,{2.0F,2.0F})==1U);
        const auto contained=battletanx::trace_navigation_segment(
            nav,1U,{2.0F,2.0F},{1.0F,1.0F});
        CHECK(contained.stop==battletanx::NavigationTraceStop::target);
        CHECK(contained.triangle==1U && contained.edges.empty());
        const auto boundary=battletanx::trace_navigation_segment(
            nav,1U,{2.0F,2.0F},{7.0F,7.0F});
        CHECK(boundary.stop==battletanx::NavigationTraceStop::boundary);
        CHECK(boundary.triangle==1U && boundary.edges.size()==1U);
        auto neighbor=nav.triangles[1];
        neighbor.kind=6U;
        nav.triangles.push_back(neighbor);
        nav.triangles[1].corners[boundary.edges[0]%3U].adjacent=6U;
        nav.kind_enabled[6]=1U;
        const auto blocked=battletanx::trace_navigation_segment(
            nav,1U,{2.0F,2.0F},{7.0F,7.0F});
        CHECK(blocked.stop==battletanx::NavigationTraceStop::blocked);
        CHECK(blocked.triangle==2U && blocked.edges.size()==1U);
    }

    {
        const auto resources=battletanx::resource_descriptors();
        CHECK(resources.size()==21U);
        for (const auto& resource : resources) {
            CHECK(!resource.name.empty());
            CHECK(resource.rom_start<resource.rom_end);
            CHECK(resource.rom_end<=battletanx::Rom::expected_size);
            CHECK(resource.capacity>0U);
        }
        CHECK(battletanx::find_resource("bundle_49DCD0").rom_start==0x49DCD0U);
        bool rejected=false;
        try { static_cast<void>(battletanx::find_resource("missing")); }
        catch (const std::runtime_error&) { rejected=true; }
        CHECK(rejected);
    }

    {
        std::array<float,battletanx::AngleTable::value_count> values{};
        values[64]=1.0F;
        values[192]=-1.0F;
        const battletanx::AngleTable angles(values);
        CHECK(std::abs(angles.sine(0U))<0.000001F);
        CHECK(std::abs(angles.sine(0x4000U)-1.0F)<0.000001F);
        CHECK(std::abs(angles.cosine(0U)-1.0F)<0.000001F);
        CHECK(std::abs(angles.cosine(0x4000U))<0.000001F);
        CHECK(std::abs(angles.sine(0x14000U)-1.0F)<0.000001F);
        const auto yaw=battletanx::yaw_translation_matrix(
            0x4000U,10.0F,0.0F,20.0F,angles);
        CHECK(std::abs(yaw[2]-1.0F)<0.000001F &&
              std::abs(yaw[10])<0.000001F);
        CHECK(yaw[3]==10.0F && yaw[11]==20.0F);

        battletanx::PlayerTank camera_tank;
        const auto camera=battletanx::build_flat_tank_camera(camera_tank,angles);
        CHECK(std::abs(camera.eye.x)<0.000001F && camera.eye.y==84.0F);
        CHECK(std::abs(camera.eye.z+242.4F)<0.0001F);
        CHECK(camera.target.x==0.0F && camera.target.y==-630.0F &&
              camera.target.z==4227.0F);
        CHECK(camera.vertical_fov_degrees==37.0F && camera.near_plane==16.0F &&
              camera.far_plane==5000.0F);
        const auto tilted_camera=battletanx::build_tank_camera(
            camera_tank,angles,{0.0F,1.0F,1.0F});
        CHECK(std::abs(tilted_camera.up.x)<0.00001F);
        CHECK(std::abs(tilted_camera.up.y-0.7071067F)<0.0001F);
        CHECK(std::abs(tilted_camera.up.z-0.7071067F)<0.0001F);
        battletanx::WorldLevel camera_world;
        battletanx::WorldItem camera_wall;
        camera_wall.type=1;
        camera_wall.shape=0;
        camera_wall.z=-200.0F;
        camera_world.items.push_back(camera_wall);
        battletanx::WorldShape camera_wall_shape;
        camera_wall_shape.min_x=-10;
        camera_wall_shape.max_x=10;
        camera_wall_shape.min_z=-10;
        camera_wall_shape.max_z=10;
        camera_world.shapes.push_back(camera_wall_shape);
        const auto clamped_camera=battletanx::build_tank_camera(
            camera_tank,angles,{0.0F,1.0F,0.0F},camera_world);
        CHECK(clamped_camera.eye.z>camera.eye.z);

        const battletanx::RotatedRect axis_aligned{20,10,-2,2,-1,1,0};
        std::array<battletanx::PlanarPoint,4> corners{};
        battletanx::rotated_rect_corners(axis_aligned,angles,corners);
        const auto near=[](float actual,float expected) {
            return std::abs(actual-expected)<0.000001F;
        };
        if (!near(corners[0].x,9.0F) || !near(corners[0].z,18.0F)) {
            std::cerr << "axis-aligned input: " << axis_aligned.origin_x << ','
                      << axis_aligned.origin_z << ' ' << axis_aligned.min_u << ','
                      << axis_aligned.max_u << ' ' << axis_aligned.min_v << ','
                      << axis_aligned.max_v << " angle " << axis_aligned.angle
                      << " sin/cos " << angles.sine(axis_aligned.angle) << ','
                      << angles.cosine(axis_aligned.angle) << '\n';
            std::cerr << "axis-aligned corners:";
            for (const auto& corner : corners) {
                std::cerr << " (" << corner.x << ',' << corner.z << ')';
            }
            std::cerr << '\n';
        }
        CHECK(near(corners[0].x,9.0F) && near(corners[0].z,18.0F));
        CHECK(near(corners[1].x,11.0F) && near(corners[1].z,18.0F));
        CHECK(near(corners[2].x,11.0F) && near(corners[2].z,22.0F));
        CHECK(near(corners[3].x,9.0F) && near(corners[3].z,22.0F));

        const battletanx::RotatedRect quarter_turn{0,0,-2,2,-1,1,0x4000U};
        std::array<battletanx::PlanarPoint,4> rotated{};
        battletanx::rotated_rect_corners(quarter_turn,angles,rotated);
        CHECK(std::abs(rotated[0].x-2.0F)<0.000001F);
        CHECK(std::abs(rotated[0].z+1.0F)<0.000001F);
        CHECK(std::abs(rotated[2].x+2.0F)<0.000001F);
        CHECK(std::abs(rotated[2].z-1.0F)<0.000001F);
        const battletanx::OrientedRect overlap_a{0.0F,0.0F,-2.0F,2.0F,-1.0F,1.0F,0};
        const battletanx::OrientedRect overlap_b{2.5F,0.0F,-1.0F,1.0F,-1.0F,1.0F,
                                                 0x4000U};
        const battletanx::OrientedRect separate{5.0F,0.0F,-1.0F,1.0F,-1.0F,1.0F,0};
        CHECK(battletanx::oriented_rects_overlap(overlap_a,overlap_b,angles));
        CHECK(!battletanx::oriented_rects_overlap(overlap_a,separate,angles));
        CHECK(battletanx::segment_intersects_oriented_rect(
            {-3.0F,0.0F},{3.0F,0.0F},overlap_a,angles));
        CHECK(!battletanx::segment_intersects_oriented_rect(
            {-3.0F,4.0F},{3.0F,4.0F},overlap_a,angles));
        const auto hit_fraction=battletanx::segment_oriented_rect_hit_fraction(
            {-3.0F,0.0F},{3.0F,0.0F},overlap_a,angles);
        CHECK(hit_fraction && std::abs(*hit_fraction-(1.0F/6.0F))<0.00001F);

        battletanx::NavigationLevel drive_nav;
        drive_nav.vertices={{},{-1000.0F,-1000.0F},{-1000.0F,1000.0F},
                            {1000.0F,-1000.0F}};
        battletanx::NavigationTriangle drive_triangle;
        drive_triangle.corners[0].vertex=1U;
        drive_triangle.corners[1].vertex=2U;
        drive_triangle.corners[2].vertex=3U;
        drive_nav.triangles.resize(2U);
        drive_nav.triangles[1]=drive_triangle;
        battletanx::WorldLevel drive_world;
        battletanx::WorldItem spawn;
        spawn.type=25;
        spawn.x=-400.0F;
        spawn.z=-400.0F;
        spawn.angle=0x4000U;
        drive_world.items.push_back(spawn);
        auto tank=battletanx::spawn_player_tank(drive_world,drive_nav);
        CHECK(tank.navigation_triangle==1U);
        CHECK(tank.x==-400.0F && tank.z==-400.0F && tank.heading_degrees==90.0F);
        const float start_x=tank.x;
        battletanx::DriveInput drive{};
        drive.throttle=80.0F;
        battletanx::update_player_tank(tank,drive,drive_world,drive_nav,angles);
        CHECK(tank.x>start_x && tank.navigation_triangle==1U);
        CHECK(tank.track_left==-40 && tank.track_right==-40);
        CHECK(tank.track_phase_left==0U && tank.track_phase_right==0U);
        battletanx::update_player_tank(tank,drive,drive_world,drive_nav,angles);
        CHECK(tank.track_phase_left==216U && tank.track_phase_right==216U);
        battletanx::WorldLevel collision_world;
        battletanx::WorldItem wall;
        wall.type=1;
        wall.shape=0;
        wall.z=95.0F;
        collision_world.items.push_back(wall);
        battletanx::WorldShape wall_shape;
        wall_shape.min_x=-10;
        wall_shape.max_x=10;
        wall_shape.min_z=-10;
        wall_shape.max_z=10;
        collision_world.shapes.push_back(wall_shape);
        battletanx::PlayerTank blocked_tank;
        blocked_tank.navigation_triangle=1U;
        battletanx::update_player_tank(
            blocked_tank,drive,collision_world,drive_nav,angles);
        CHECK(blocked_tank.z==0.0F && blocked_tank.velocity_z<0.0F);
        battletanx::PlayerProjectilePool wall_projectiles{};
        wall_projectiles[0]={0.0F,0.0F,0.0F,110.0F,1U,112U,true};
        battletanx::update_player_projectiles(
            wall_projectiles,collision_world,drive_nav,angles);
        CHECK(!wall_projectiles[0].active && wall_projectiles[0].frames_remaining==0U);
        battletanx::PlayerProjectilePool projectiles{};
        battletanx::DriveInput fire{};
        fire.fire=true;
        fire.fire_pressed=true;
        CHECK(battletanx::try_fire_player_projectile(
            projectiles,tank,fire,drive_world,drive_nav,angles,0));
        CHECK(projectiles[0].active && projectiles[0].frames_remaining==112U);
        CHECK(tank.primary_last_tick==0 && tank.primary_ammo==69);
        const float projectile_x=projectiles[0].x;
        battletanx::update_player_projectiles(projectiles,drive_world,drive_nav,angles);
        CHECK(projectiles[0].active && projectiles[0].x>projectile_x &&
              projectiles[0].frames_remaining==111U);
        CHECK(!battletanx::try_fire_player_projectile(
            projectiles,tank,fire,drive_world,drive_nav,angles,30));
        CHECK(battletanx::try_fire_player_projectile(
            projectiles,tank,fire,drive_world,drive_nav,angles,31));
        CHECK(projectiles[0].active && projectiles[1].active);
        CHECK(tank.primary_last_tick==31 && tank.primary_ammo==68);
        for (auto& active_projectile:projectiles) active_projectile.active=true;
        CHECK(!battletanx::try_fire_player_projectile(
            projectiles,tank,fire,drive_world,drive_nav,angles,62));
        CHECK(tank.primary_last_tick==31 && tank.primary_ammo==68);
    }

    {
        battletanx::PlanarMotion motion{};
        motion.acceleration_scale=2.0F;
        motion.turn_scale=0.5F;
        motion.x=10.0F;
        motion.z=-4.0F;
        motion.velocity_x=0.25F;
        motion.velocity_z=-1.0F;
        motion.heading_degrees=359.0F;
        motion.turn_rate=4.0F;
        motion.acceleration_x=0.0F;
        motion.acceleration_z=1.0F;
        motion.turn_acceleration=4.0F;
        battletanx::advance_planar_motion(motion,0.5F);
        CHECK(motion.velocity_x==0.0F);
        CHECK(motion.velocity_z==0.0F);
        CHECK(motion.x==10.0F && motion.z==-4.0F);
        CHECK(motion.turn_rate==5.0F);
        CHECK(motion.heading_degrees==1.5F);

        motion.heading_degrees=-725.0F;
        motion.turn_rate=0.0F;
        battletanx::advance_planar_motion(motion,0.0F);
        CHECK(motion.heading_degrees==355.0F);
        motion.heading_degrees=360.0F;
        battletanx::advance_planar_motion(motion,0.0F);
        CHECK(motion.heading_degrees==360.0F);
    }

    {
        battletanx::HostControllerState state{};
        state.left_x=32767;
        state.left_y=-32768;
        state.right_x=-20000;
        state.right_y=20000;
        state.left_trigger=20000;
        state.buttons[static_cast<std::size_t>(battletanx::HostButton::face_south)]=true;
        state.buttons[static_cast<std::size_t>(battletanx::HostButton::start)]=true;
        state.buttons[static_cast<std::size_t>(battletanx::HostButton::dpad_up)]=true;
        state.buttons[static_cast<std::size_t>(battletanx::HostButton::right_shoulder)]=true;
        const auto pad=battletanx::map_host_controller(state);
        CHECK(pad.stick_x==127 && pad.stick_y==127);
        CHECK(pad.buttons==0xB816U);

        state={};
        state.left_x=5999;
        state.left_y=-5999;
        state.right_x=16000;
        state.right_y=-16000;
        const auto deadzone=battletanx::map_host_controller(state);
        CHECK(deadzone.stick_x==0 && deadzone.stick_y==0);
        CHECK(deadzone.buttons==0U);
    }

    {
        constexpr battletanx::ControllerPad previous{0x0001U,-10,20};
        constexpr battletanx::ControllerPad current{0x0102U,50,-60};
        const auto input=battletanx::translate_player_input(current,previous);
        CHECK(input.stick_x==50 && input.stick_y==-60);
        CHECK(input.held==0x0303U);
        CHECK(input.pressed==0x0202U);
        CHECK(input.released==0U);

        const auto released=battletanx::translate_player_input({},current);
        CHECK(released.held==0U && released.pressed==0U);
        CHECK(released.released==0x0303U);

        battletanx::InputBindings bindings;
        bindings.words={0x2000U,0x8000U,0x4000U,0x20U,0x8U,0x4U,
                        0x100000U,0x100000U,0x400000U,0x400000U,
                        0x200000U,0x200000U,1U,2U,0x200000U,
                        0x200000U,0x100000U};
        const auto controls=battletanx::translate_drive_input(
            battletanx::PlayerInput{80,-60,0x2000U,0x2000U,0U},bindings);
        CHECK(controls.throttle==-60.0F && controls.rotation==80.0F);
        CHECK(controls.aim==0.0F && controls.fire && controls.fire_pressed);
        const auto deadzone_controls=battletanx::translate_drive_input(
            battletanx::PlayerInput{3,-3,0U,0U,0U},bindings);
        CHECK(deadzone_controls.throttle==0.0F && deadzone_controls.rotation==0.0F);
    }

    {
        constexpr std::array<std::uint8_t,24> prefix{
            0,0,0,3, 0,0,0,24, 0,0,0,32, 0,0,0,40,
            0,0,0,0, 0,0,0,0};
        std::array<std::uint8_t,48> storage{};
        for (std::size_t index=0; index<prefix.size(); ++index) storage[index]=prefix[index];
        const auto bundle=battletanx::Bundle::parse(storage);
        CHECK(bundle.payload_offset()==24U);
        CHECK(bundle.entries().size()==3U);
        CHECK(bundle.entries()[0].offset==24U && bundle.entries()[0].size==8U);
        CHECK(bundle.entries()[2].offset==40U && bundle.entries()[2].size==8U);
    }

    {
        constexpr std::array<std::uint8_t,4> empty_stream{};
        CHECK(battletanx::lzari_decompress(empty_stream).empty());
        bool rejected=false;
        try {
            constexpr std::array<std::uint8_t,4> oversized{0x7F,0xFF,0xFF,0xFF};
            static_cast<void>(battletanx::lzari_decompress(oversized,1024U));
        } catch (const std::runtime_error&) { rejected=true; }
        CHECK(rejected);
    }

    {
        constexpr std::array<std::uint8_t, 8> bytes{0x80,0x37,0x12,0x40,0xFF,0xFE,0x00,0x01};
        const battletanx::BigEndianView view(bytes);
        CHECK(view.u32(0) == 0x80371240U);
        CHECK(view.u16(4) == 0xFFFEU);
        CHECK(view.s16(4) == -2);
        CHECK(view.u32(4) == 0xFFFE0001U);
        bool rejected = false;
        try { static_cast<void>(view.u32(6)); }
        catch (const std::out_of_range&) { rejected = true; }
        CHECK(rejected);
    }

    {
        constexpr std::string_view abc = "abc";
        const auto bytes = std::span(
            reinterpret_cast<const std::uint8_t*>(abc.data()), abc.size());
        CHECK(battletanx::hex_digest(battletanx::sha256(bytes)) ==
              "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
    }

    {
        std::array<std::uint8_t, 0x40> header{};
        header[0]=0x80; header[1]=0x37; header[2]=0x12; header[3]=0x40;
        constexpr std::string_view title = "BATTLETANX";
        for (std::size_t index=0; index<title.size(); ++index) header[0x20U+index]=title[index];
        header[0x3B]='N'; header[0x3C]='B'; header[0x3D]='X'; header[0x3E]='E';
        const auto parsed = battletanx::Rom::inspect_header(header);
        CHECK(parsed.title == "BATTLETANX");
        CHECK(parsed.game_code == "NBXE");
        CHECK(parsed.revision == 0U);
    }

    return 0;
}

