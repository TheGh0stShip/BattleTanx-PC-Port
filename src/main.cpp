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
        std::cout << "Native runtime foundation is ready; gameplay bring-up is in progress.\n";
    } catch (const std::exception& error) {
        std::cerr << "error: " << error.what() << '\n';
        return 1;
    }
    return 0;
}

