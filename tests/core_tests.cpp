#include "battletanx/big_endian.hpp"
#include "battletanx/rom.hpp"
#include "battletanx/sha256.hpp"

#include <array>
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

