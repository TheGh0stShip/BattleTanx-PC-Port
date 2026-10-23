#include "battletanx/angle.hpp"
#include "battletanx/bundle.hpp"
#include "battletanx/lzari.hpp"
#include "battletanx/platform.hpp"
#include "battletanx/resource.hpp"
#include "battletanx/rom.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <exception>
#include <iostream>
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

void run_platform_diagnostic() {
    auto platform=battletanx::Platform::create("BattleTanx native source port");
    std::array<std::uint32_t,battletanx::native_width*battletanx::native_height> pixels{};
    std::array<battletanx::ControllerPad,4> previous{};
    std::array<battletanx::ControllerPad,4> current{};
    const auto frame_period=std::chrono::microseconds(16667);
    while (platform->pump(current)) {
        const auto frame_start=std::chrono::steady_clock::now();
        const auto input=battletanx::translate_player_input(current[0],previous[0]);
        previous=current;
        pixels.fill(0x00101820U);
        fill_rectangle(pixels,24,24,battletanx::native_width-48,4,0x00608090U);
        fill_rectangle(pixels,24,battletanx::native_height-28,
                       battletanx::native_width-48,4,0x00608090U);
        fill_rectangle(pixels,24,24,4,battletanx::native_height-48,0x00608090U);
        fill_rectangle(pixels,battletanx::native_width-28,24,4,
                       battletanx::native_height-48,0x00608090U);
        const int center_x=battletanx::native_width/2+input.stick_x;
        const int center_y=battletanx::native_height/2-input.stick_y;
        fill_rectangle(pixels,center_x-8,center_y-8,16,16,
                       input.held!=0U ? 0x00F0B040U : 0x004080C0U);
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
        if (std::abs(angles.sine(0x4000U)-1.0F)>0.0001F ||
            std::abs(angles.cosine(0U)-1.0F)>0.0001F) {
            throw std::runtime_error("retail angle table failed cardinal-direction validation");
        }
        std::cout << "Loaded retail 16-bit angle table: "
                  << battletanx::AngleTable::value_count << " samples\n";
        std::cout << "Native runtime foundation is ready; gameplay bring-up is in progress.\n";
        if (show_window) run_platform_diagnostic();
    } catch (const std::exception& error) {
        std::cerr << "error: " << error.what() << '\n';
        return 1;
    }
    return 0;
}

