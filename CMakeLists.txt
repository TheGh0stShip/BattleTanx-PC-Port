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

add_executable(BattleTanx src/main.cpp)
target_link_libraries(BattleTanx PRIVATE battletanx_core)

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
require_native_source_target(BattleTanx)

get_target_property(core_dependencies battletanx_core LINK_LIBRARIES)
if(core_dependencies)
    message(FATAL_ERROR
        "battletanx_core gained a runtime dependency; review it as a native host library")
endif()
get_target_property(game_dependencies BattleTanx LINK_LIBRARIES)
if(NOT game_dependencies STREQUAL "battletanx_core")
    message(FATAL_ERROR
        "BattleTanx must link only the reviewed native source-port core")
endif()

enable_testing()
add_executable(battletanx_tests tests/core_tests.cpp)
target_link_libraries(battletanx_tests PRIVATE battletanx_core)
add_test(NAME battletanx_core COMMAND battletanx_tests)

