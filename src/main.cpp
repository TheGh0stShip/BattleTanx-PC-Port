#include "battletanx/bundle.hpp"
#include "battletanx/lzari.hpp"
#include "battletanx/rom.hpp"

#include <exception>
#include <iostream>

int main(int argc, char** argv) {
    if (argc != 2) {
        std::cerr << "usage: BattleTanx /path/to/BattleTanx.z64\n";
        return 2;
    }

    try {
        const auto rom = battletanx::Rom::load_verified(argv[1]);
        std::cout << "Verified BattleTanx " << rom.header().game_code
                  << " Rev " << static_cast<unsigned>(rom.header().revision)
                  << " (" << rom.header().title << ")\n";
        constexpr std::size_t bundle_start=0x49DCD0U;
        constexpr std::size_t bundle_end=0x4AC150U;
        constexpr std::size_t bundle_capacity=0x25800U;
        const auto decoded=battletanx::lzari_decompress(
            rom.bytes().subspan(bundle_start,bundle_end-bundle_start),bundle_capacity);
        const auto bundle=battletanx::Bundle::parse(decoded);
        std::cout << "Decoded retail resource bundle: " << decoded.size()
                  << " bytes, " << bundle.entries().size() << " entries\n";
        std::cout << "Native runtime foundation is ready; gameplay bring-up is in progress.\n";
    } catch (const std::exception& error) {
        std::cerr << "error: " << error.what() << '\n';
        return 1;
    }
    return 0;
}

