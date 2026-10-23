#include "battletanx/angle.hpp"
#include "battletanx/bundle.hpp"
#include "battletanx/camera.hpp"
#include "battletanx/controller_pak.hpp"
#include "battletanx/fixed_tick.hpp"
#include "battletanx/lzari.hpp"
#include "battletanx/level.hpp"
#include "battletanx/model.hpp"
#include "battletanx/platform.hpp"
#include "battletanx/resource.hpp"
#include "battletanx/rom.hpp"
#include "battletanx/software_renderer.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <exception>
#include <iostream>
#include <optional>
#include <string_view>
#include <thread>

namespace {
void run_platform_diagnostic(const battletanx::WorldLevel& world,
                             const battletanx::NavigationLevel& navigation,
                             const battletanx::AngleTable& angles,
                             const battletanx::InputBindings& bindings,
                             const battletanx::ModelAssets& assets) {
    auto platform=battletanx::Platform::create("BattleTanx native source port");
    battletanx::SoftwareRenderer renderer(
        battletanx::native_width,battletanx::native_height);
    std::array<battletanx::ControllerPad,4> previous{};
    std::array<battletanx::ControllerPad,4> current{};
    constexpr std::uint32_t ticks_per_second=30U;
    const auto clock_start=std::chrono::steady_clock::now();
    auto tank=battletanx::spawn_player_tank(world,navigation);
    battletanx::PlayerProjectilePool projectiles{};
    std::uint64_t game_tick=0U;
    bool verified_native_frame=false;
    while (platform->pump(current)) {
        const auto pad=battletanx::translate_player_input(current[0],previous[0]);
        auto input=battletanx::translate_drive_input(pad,bindings);
        previous=current;
        const auto elapsed=std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::steady_clock::now()-clock_start).count();
        const auto target_tick=battletanx::fixed_rate_tick(
            static_cast<std::uint64_t>(elapsed),ticks_per_second);
        while (game_tick<target_tick) {
            battletanx::update_player_tank(tank,input,world,navigation,angles);
            battletanx::try_fire_player_projectile(
                projectiles,tank,input,world,navigation,angles,
                static_cast<std::int32_t>(game_tick));
            battletanx::update_player_projectiles(projectiles,world,navigation,angles);
            ++game_tick;
            input.fire_pressed=false;
            input.special_pressed=false;
        }
        battletanx::DrawList draw;
        const auto camera=battletanx::build_flat_tank_camera(tank,angles);
        draw.view=battletanx::look_at_matrix(camera.eye,camera.target,camera.up);
        draw.projection=battletanx::perspective_matrix(
            camera.vertical_fov_degrees,camera.aspect_ratio,
            camera.near_plane,camera.far_plane);
        draw.items.reserve(world.items.size()+4U+projectiles.size());
        for (const auto& item:world.items) {
            const std::uint8_t layer=item.type==0 ? 0U : item.type==0x1C ? 6U : 2U;
            draw.items.push_back({static_cast<std::uint32_t>(item.shape),
                battletanx::yaw_translation_matrix(item.angle,item.x,0.0F,item.z,angles),
                layer,static_cast<std::int8_t>(item.kind&15U)});
        }
        const auto heading=static_cast<std::uint16_t>(
            tank.heading_degrees*(65536.0F/360.0F));
        const auto turret=static_cast<std::uint16_t>(
            tank.turret_degrees*(65536.0F/360.0F));
        const auto body=battletanx::yaw_translation_matrix(
            heading,tank.x,0.0F,tank.z,angles);
        const auto turret_transform=battletanx::yaw_translation_matrix(
            turret,tank.x,0.0F,tank.z,angles);
        for (const std::uint32_t slot:{0x0BU,0x0CU,0x0EU}) {
            draw.items.push_back({assets.set_base()+slot,body,1U,0});
        }
        draw.items.push_back({assets.set_base()+0x0DU,turret_transform,1U,0});
        for (const auto& projectile:projectiles) {
            if (!projectile.active) continue;
            draw.items.push_back({assets.set_base()+0x24U,
                battletanx::yaw_translation_matrix(
                    turret,projectile.x,36.0F,projectile.z,angles),3U,0});
        }
        renderer.render(draw,assets);
        if (!verified_native_frame) {
            const auto pixels=renderer.pixels();
            const auto visible=static_cast<std::size_t>(std::count_if(
                pixels.begin(),pixels.end(),[](std::uint32_t pixel) {
                    return pixel!=0x00101820U;
                }));
            if (visible<100U) throw std::runtime_error("native model frame is empty");
            std::cout << "Rendered native model frame: " << visible
                      << " visible pixels\n";
            verified_native_frame=true;
        }
        platform->present(renderer.pixels(),battletanx::native_width,battletanx::native_height);
        const auto next_tick_nanoseconds=static_cast<std::int64_t>(
            ((game_tick+1U)*UINT64_C(1000000000)+ticks_per_second-1U)/ticks_per_second);
        std::this_thread::sleep_until(
            clock_start+std::chrono::nanoseconds(next_tick_nanoseconds));
    }
}
} // namespace

int main(int argc, char** argv) {
    const bool show_window=argc==3 && std::string_view(argv[1])=="--window";
    if ((argc!=2 && !show_window) || (show_window && argc!=3)) {
        std::cerr << "usage: BattleTanx [--window] /path/to/BattleTanx.z64\n";
        return 2;
    }

    try {
        const auto pak_path=battletanx::Platform::controller_pak_path();
        auto pak=battletanx::load_controller_pak_record(pak_path);
        if (!pak) {
            battletanx::write_controller_pak_record(
                pak_path,battletanx::ControllerPakRecord{});
            pak=battletanx::load_controller_pak_record(pak_path);
        }
        if (!pak) throw std::runtime_error("failed to mount virtual Controller Pak");
        std::cout << "Mounted verified Controller Pak image at "
                  << pak_path.string() << '\n';
        const auto rom = battletanx::Rom::load_verified(argv[show_window?2:1]);
        std::cout << "Verified BattleTanx " << rom.header().game_code
                  << " Rev " << static_cast<unsigned>(rom.header().revision)
                  << " (" << rom.header().title << ")\n";
        std::size_t decoded_bytes=0U;
        std::size_t decoded_entries=0U;
        for (const auto& descriptor : battletanx::resource_descriptors()) {
            const auto decoded=battletanx::load_resource(rom,descriptor);
            const auto bundle=battletanx::Bundle::parse(decoded);
            decoded_bytes+=decoded.size();
            decoded_entries+=bundle.entries().size();
        }
        std::cout << "Validated " << battletanx::resource_descriptors().size()
                  << " retail resource bundles: " << decoded_bytes
                  << " decoded bytes, " << decoded_entries << " entries\n";
        constexpr std::size_t angle_table_offset=0xC9620U;
        constexpr std::size_t angle_table_size=battletanx::AngleTable::value_count*4U;
        const auto angles=battletanx::AngleTable::from_big_endian(
            rom.bytes().subspan(angle_table_offset,angle_table_size));
        const auto bindings=battletanx::load_default_input_bindings(rom);
        if (std::abs(angles.sine(0x4000U)-1.0F)>0.0001F ||
            std::abs(angles.cosine(0U)-1.0F)>0.0001F) {
            throw std::runtime_error("retail angle table failed cardinal-direction validation");
        }
        std::cout << "Loaded retail 16-bit angle table: "
                  << battletanx::AngleTable::value_count << " samples\n";
        std::optional<battletanx::WorldLevel> preview_world;
        std::optional<battletanx::NavigationLevel> preview_navigation;
        std::size_t world_groups=0U;
        std::size_t world_items=0U;
        std::size_t navigation_triangles=0U;
        battletanx::ModelGeometryAudit model_audit{};
        for (const auto& descriptor : battletanx::level_stream_descriptors()) {
            auto world=battletanx::load_world_level(rom,descriptor);
            const auto level_audit=battletanx::audit_model_geometry(rom,world);
            model_audit.part_references+=level_audit.part_references;
            model_audit.material_blobs+=level_audit.material_blobs;
            model_audit.geometry_blobs+=level_audit.geometry_blobs;
            model_audit.vertices+=level_audit.vertices;
            model_audit.triangles+=level_audit.triangles;
            world_groups+=world.groups.size();
            world_items+=world.items.size();
            if (descriptor.index==1) preview_world=std::move(world);
            if (descriptor.navigation) {
                auto navigation=battletanx::load_navigation_level(rom,descriptor);
                navigation_triangles+=navigation.triangles.size();
                if (descriptor.index==1) preview_navigation=std::move(navigation);
            }
        }
        std::cout << "Validated 22 world streams and 18 navigation streams: "
                  << world_groups << " groups, " << world_items << " placements, "
                  << navigation_triangles << " navigation triangles\n";
        std::cout << "Validated " << model_audit.geometry_blobs
                  << " level-local model geometry references: " << model_audit.vertices
                  << " vertices, " << model_audit.triangles << " triangles\n";
        const auto preview_assets=battletanx::ModelAssets::load(rom,*preview_world);
        for (const std::size_t slot:{0x0BU,0x0CU,0x0DU,0x0EU,0x24U,0x52U}) {
            if (preview_assets.set_base()+slot>=preview_assets.sets().size() ||
                preview_assets.set(preview_assets.set_base()+slot).lod_count==0U) {
                throw std::runtime_error("level-one native model slot is absent");
            }
        }
        std::cout << "Decoded level-one native model cache: "
                  << preview_assets.meshes().size() << " meshes, "
                  << preview_assets.materials().size() << " materials\n";
        std::cout << "Native runtime foundation is ready; gameplay bring-up is in progress.\n";
        if (show_window) {
            run_platform_diagnostic(
                *preview_world,*preview_navigation,angles,bindings,preview_assets);
        }
    } catch (const std::exception& error) {
        std::cerr << "error: " << error.what() << '\n';
        return 1;
    }
    return 0;
}

