#include "battletanx/angle.hpp"
#include "battletanx/big_endian.hpp"
#include "battletanx/bundle.hpp"
#include "battletanx/gameplay.hpp"
#include "battletanx/geometry.hpp"
#include "battletanx/lzari.hpp"
#include "battletanx/rom.hpp"
#include "battletanx/resource.hpp"
#include "battletanx/sha256.hpp"

#include <array>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <string_view>

#define CHECK(expression) \
    do { \
        if (!(expression)) { \
            std::cerr << "check failed at " << __FILE__ << ':' << __LINE__ \
                      << ": " #expression "\n"; \
            return 1; \
        } \
    } while (false)

int main() {
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

        const battletanx::RotatedRect axis_aligned{20,10,-2,2,-1,1,0};
        const auto corners=battletanx::rotated_rect_corners(axis_aligned,angles);
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
        const auto rotated=battletanx::rotated_rect_corners(quarter_turn,angles);
        CHECK(std::abs(rotated[0].x-2.0F)<0.000001F);
        CHECK(std::abs(rotated[0].z+1.0F)<0.000001F);
        CHECK(std::abs(rotated[2].x+2.0F)<0.000001F);
        CHECK(std::abs(rotated[2].z-1.0F)<0.000001F);
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

