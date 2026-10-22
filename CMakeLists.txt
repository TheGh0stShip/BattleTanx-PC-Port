cmake_minimum_required(VERSION 3.20)
project(BattleTanxPC LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_library(battletanx_core
    src/angle.cpp
    src/bundle.cpp
    src/gameplay.cpp
    src/geometry.cpp
    src/lzari.cpp
    src/rom.cpp
    src/sha256.cpp
)
target_include_directories(battletanx_core PUBLIC include)
target_compile_options(battletanx_core PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall;-Wextra;-Wpedantic;-Werror>
    $<$<CXX_COMPILER_ID:MSVC>:/W4;/WX;/permissive->
)

add_executable(BattleTanx src/main.cpp)
target_link_libraries(BattleTanx PRIVATE battletanx_core)

enable_testing()
add_executable(battletanx_tests tests/core_tests.cpp)
target_link_libraries(battletanx_tests PRIVATE battletanx_core)
add_test(NAME battletanx_core COMMAND battletanx_tests)

