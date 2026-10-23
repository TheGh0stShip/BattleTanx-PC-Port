#include "battletanx/angle.hpp"
#include "battletanx/bundle.hpp"
#include "battletanx/controller_pak.hpp"
#include "battletanx/lzari.hpp"
#include "battletanx/level.hpp"
#include "battletanx/platform.hpp"
#include "battletanx/resource.hpp"
#include "battletanx/rom.hpp"

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
void fill_rectangle(std::array<std::uint32_t,battletanx::native_width*battletanx::native_height>& pixels,
                    int x,int y,int width,int height,std::uint32_t color) {
    const int left=std::clamp(x,0,battletanx::native_width);
    const int top=std::clamp(y,0,battletanx::native_height);
    const int right=std::clamp(x+width,0,battletanx::native_width);
    const int bottom=std::clamp(y+height,0,battletanx::native_height);
    for (int row=top; row<bottom; ++row) {
        std::fill(pixels.begin()+row*battletanx::native_width+left,
                  pixels.begin()+row*battletanx::native_width+right,color);
    }
}

void draw_line(std::array<std::uint32_t,battletanx::native_width*battletanx::native_height>& pixels,
               int x0,int y0,int x1,int y1,std::uint32_t color) {
    const int dx=std::abs(x1-x0);
    const int sx=x0<x1 ? 1 : -1;
    const int dy=-std::abs(y1-y0);
    const int sy=y0<y1 ? 1 : -1;
    int error=dx+dy;
    for (;;) {
        fill_rectangle(pixels,x0,y0,2,2,color);
        if (x0==x1 && y0==y1) break;
        const int doubled=2*error;
        if (doubled>=dy) { error+=dy; x0+=sx; }
        if (doubled<=dx) { error+=dx; y0+=sy; }
    }
}

void run_platform_diagnostic(const battletanx::WorldLevel& world,
                             const battletanx::NavigationLevel& navigation,
                             const battletanx::AngleTable& angles,
                             const battletanx::InputBindings& bindings) {
    auto platform=battletanx::Platform::create("BattleTanx native source port");
    std::array<std::uint32_t,battletanx::native_width*battletanx::native_height> pixels{};
    std::array<battletanx::ControllerPad,4> previous{};
    std::array<battletanx::ControllerPad,4> current{};
    const auto frame_period=std::chrono::microseconds(33333);
    auto tank=battletanx::spawn_player_tank(world,navigation);
    battletanx::PlayerProjectilePool projectiles{};
    std::int32_t game_tick=0;
    constexpr float view_scale=0.12F;
    const auto screen_x=[&](float value) {
        return battletanx::native_width/2+static_cast<int>((value-tank.x)*view_scale);
    };
    const auto screen_y=[&](float value) {
        return battletanx::native_height/2-static_cast<int>((value-tank.z)*view_scale);
    };
    while (platform->pump(current)) {
        const auto frame_start=std::chrono::steady_clock::now();
        const auto pad=battletanx::translate_player_input(current[0],previous[0]);
        const auto input=battletanx::translate_drive_input(pad,bindings);
        previous=current;
        battletanx::update_player_tank(tank,input,world,navigation,angles);
        battletanx::try_fire_player_projectile(
            projectiles,tank,input,world,navigation,angles,game_tick);
        battletanx::update_player_projectiles(projectiles,world,navigation,angles);
        ++game_tick;
        pixels.fill(0x00101820U);
        fill_rectangle(pixels,24,24,battletanx::native_width-48,4,0x00608090U);
        fill_rectangle(pixels,24,battletanx::native_height-28,
                       battletanx::native_width-48,4,0x00608090U);
        fill_rectangle(pixels,24,24,4,battletanx::native_height-48,0x00608090U);
        fill_rectangle(pixels,battletanx::native_width-28,24,4,
                       battletanx::native_height-48,0x00608090U);
        for (const auto& item : world.items) {
            if (!std::isfinite(item.x) || !std::isfinite(item.z)) continue;
            const int x=screen_x(item.x);
            const int y=screen_y(item.z);
            fill_rectangle(pixels,x-1,y-1,3,3,0x0060C070U);
        }
        if (tank.navigation_triangle!=0U) {
            const auto& corners=navigation.triangles[tank.navigation_triangle].corners;
            for (std::size_t index=0; index<corners.size(); ++index) {
                const auto& a=navigation.vertices[corners[index].vertex];
                const auto& b=navigation.vertices[corners[(index+1U)%corners.size()].vertex];
                draw_line(pixels,screen_x(a.x),screen_y(a.z),screen_x(b.x),screen_y(b.z),
                          0x00408090U);
            }
        }
        const auto heading=static_cast<std::uint16_t>(
            tank.heading_degrees*(65536.0F/360.0F));
        const int tank_x=screen_x(tank.x);
        const int tank_y=screen_y(tank.z);
        const int nose_x=screen_x(tank.x+angles.sine(heading)*80.0F);
        const int nose_y=screen_y(tank.z+angles.cosine(heading)*80.0F);
        const auto turret=static_cast<std::uint16_t>(
            tank.turret_degrees*(65536.0F/360.0F));
        const int turret_x=screen_x(tank.x+angles.sine(turret)*105.0F);
        const int turret_y=screen_y(tank.z+angles.cosine(turret)*105.0F);
        fill_rectangle(pixels,tank_x-5,tank_y-5,10,10,0x0040E080U);
        draw_line(pixels,tank_x,tank_y,nose_x,nose_y,0x0080A050U);
        draw_line(pixels,tank_x,tank_y,turret_x,turret_y,0x00F0D040U);
        for (const auto& projectile:projectiles) {
            if (!projectile.active) continue;
            fill_rectangle(pixels,screen_x(projectile.x)-2,screen_y(projectile.z)-2,
                           5,5,0x00F08030U);
        }
        platform->present(pixels,battletanx::native_width,battletanx::native_height);
        std::this_thread::sleep_until(frame_start+frame_period);
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
        for (const auto& descriptor : battletanx::level_stream_descriptors()) {
            auto world=battletanx::load_world_level(rom,descriptor);
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
        std::cout << "Native runtime foundation is ready; gameplay bring-up is in progress.\n";
        if (show_window) {
            run_platform_diagnostic(*preview_world,*preview_navigation,angles,bindings);
        }
    } catch (const std::exception& error) {
        std::cerr << "error: " << error.what() << '\n';
        return 1;
    }
    return 0;
}

