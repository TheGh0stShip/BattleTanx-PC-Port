cmake_minimum_required(VERSION 3.20)
project(BattleTanxPC LANGUAGES C CXX)

include(FetchContent)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_library(battletanx_core
    src/angle.cpp
    src/bundle.cpp
    src/camera.cpp
    src/controller_pak.cpp
    src/gameplay.cpp
    src/geometry.cpp
    src/host_input.cpp
    src/level.cpp
    src/lzari.cpp
    src/resource.cpp
    src/rom.cpp
    src/sha256.cpp
)
target_include_directories(battletanx_core PUBLIC include)
# GCC 15.2 currently miscompiles the recovered interleaved corner transform
# under its tree vectorizer. Keep this small translation unit scalar; the
# deterministic geometry test guards removal when the compiler is corrected.
set_property(SOURCE src/geometry.cpp APPEND PROPERTY COMPILE_OPTIONS
    $<$<CXX_COMPILER_ID:GNU>:-fno-tree-vectorize>)
target_compile_options(battletanx_core PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall;-Wextra;-Wpedantic;-Werror>
    $<$<CXX_COMPILER_ID:MSVC>:/W4;/WX;/permissive->
)

set(SDL_SHARED OFF CACHE BOOL "" FORCE)
set(SDL_STATIC ON CACHE BOOL "" FORCE)
set(SDL_TEST OFF CACHE BOOL "" FORCE)
set(SDL_TESTS OFF CACHE BOOL "" FORCE)
FetchContent_Declare(SDL2
    GIT_REPOSITORY https://github.com/libsdl-org/SDL.git
    GIT_TAG release-2.30.11
    GIT_SHALLOW TRUE)
FetchContent_MakeAvailable(SDL2)

add_library(battletanx_platform src/platform_sdl.cpp)
target_include_directories(battletanx_platform PUBLIC include)
target_link_libraries(battletanx_platform PUBLIC battletanx_core PRIVATE SDL2::SDL2-static)
target_compile_options(battletanx_platform PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall;-Wextra;-Wpedantic;-Werror>
    $<$<CXX_COMPILER_ID:MSVC>:/W4;/WX;/permissive->
)

add_executable(BattleTanx src/main.cpp)
target_link_libraries(BattleTanx PRIVATE battletanx_core battletanx_platform)

function(require_native_source_target target)
    get_target_property(target_sources ${target} SOURCES)
    foreach(source IN LISTS target_sources)
        if(source MATCHES "(recomp|emulat|mips|rsp|rdp|libultra|ultramodern|rt64|generated)" OR
           source MATCHES "\\.(s|asm)$")
            message(FATAL_ERROR "${target} contains a non-native source-port input: ${source}")
        endif()
    endforeach()
endfunction()

require_native_source_target(battletanx_core)
require_native_source_target(battletanx_platform)
require_native_source_target(BattleTanx)

get_target_property(core_dependencies battletanx_core LINK_LIBRARIES)
if(core_dependencies)
    message(FATAL_ERROR
        "battletanx_core gained a runtime dependency; review it as a native host library")
endif()
get_target_property(game_dependencies BattleTanx LINK_LIBRARIES)
if(NOT game_dependencies STREQUAL "battletanx_core;battletanx_platform")
    message(FATAL_ERROR
        "BattleTanx must link only the reviewed native source-port libraries")
endif()
get_target_property(platform_dependencies battletanx_platform LINK_LIBRARIES)
if(NOT platform_dependencies STREQUAL "battletanx_core;SDL2::SDL2-static")
    message(FATAL_ERROR
        "battletanx_platform must link only the portable core and reviewed SDL2 host library")
endif()

enable_testing()
add_executable(battletanx_tests tests/core_tests.cpp)
target_link_libraries(battletanx_tests PRIVATE battletanx_core)
add_test(NAME battletanx_core COMMAND battletanx_tests)

