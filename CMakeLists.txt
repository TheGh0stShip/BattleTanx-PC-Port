cmake_minimum_required(VERSION 3.20)
project(BattleTanxPC VERSION 0.1.0 LANGUAGES C CXX)

option(BATTLETANX_PORTABLE "Keep settings and Controller Pak beside the executable" OFF)

include(FetchContent)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_library(battletanx_core
    src/angle.cpp
    src/audio.cpp
    src/bundle.cpp
    src/camera.cpp
    src/campaign_briefing.cpp
    src/controller_pak.cpp
    src/gameplay.cpp
    src/geometry.cpp
    src/host_input.cpp
    src/hud.cpp
    src/level.cpp
    src/lzari.cpp
    src/material.cpp
    src/model.cpp
    src/music.cpp
    src/pc_settings.cpp
    src/resource.cpp
    src/render_math.cpp
    src/rom.cpp
    src/sha256.cpp
    src/software_renderer.cpp
    src/texture.cpp
    src/terrain.cpp
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


# Native gameplay runtime (renderer- and platform-neutral).
target_sources(battletanx_core PRIVATE
    src/battle_session.cpp
    src/campaign_record.cpp
    src/combat.cpp
    src/enemy_control.cpp
    src/level_spawns.cpp
    src/mission_state.cpp
    src/pickup_runtime.cpp
    src/projectile_runtime.cpp
    src/runtime_math.cpp
    src/state_hash.cpp
    src/unit_runtime.cpp
)
# Campaign runtime v3 modules (placed before the native-source check below).
target_sources(battletanx_core PRIVATE
    src/campaign_ai_parameters.cpp
    src/campaign_battle_bridge.cpp
    src/campaign_flow.cpp
    src/campaign_harness.cpp
    src/campaign_harness_bindings.cpp
    src/campaign_owner_plays.cpp
    src/campaign_save_transaction.cpp
    src/deployables_runtime.cpp
    src/effect_runtime.cpp
    src/frontend_model.cpp
    src/frontend_setup_menus.cpp
    src/music_selection.cpp
    src/owner_links.cpp
    src/owner_profiles.cpp
    src/placed_objects.cpp
    src/powerup_state.cpp
    src/queen_world.cpp
    src/route_service.cpp
    src/spawner_runtime.cpp
    src/squad_units.cpp
)
require_native_source_target(battletanx_core)
function(add_runtime_test name)
    add_executable(${name} tests/${name}.cpp)
    target_link_libraries(${name} PRIVATE battletanx_core)
    target_compile_options(${name} PRIVATE
        $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall;-Wextra;-Wpedantic;-Werror>
        $<$<CXX_COMPILER_ID:MSVC>:/W4;/WX;/permissive->
    )
    add_test(NAME ${name} COMMAND ${name})
endfunction()
add_runtime_test(runtime_pool_tests)
add_runtime_test(campaign_record_tests)
add_runtime_test(combat_tests)
add_runtime_test(mission_tests)
add_runtime_test(enemy_tests)
add_runtime_test(battle_session_tests)
add_runtime_test(spawner_tests)
add_runtime_test(pickup_tests)
add_runtime_test(hud_tests)

target_sources(battletanx_core PRIVATE
    src/campaign_ai.cpp
    src/campaign_catalog.cpp
    src/campaign_spawn.cpp
    src/navigation_route.cpp
    src/objective_runtime.cpp
    src/pickup_effects.cpp
    src/unit_kinds.cpp
    src/weapon_catalog.cpp
    src/weapon_runtime.cpp
)
add_runtime_test(campaign_catalog_tests)
add_runtime_test(campaign_spawn_tests)
add_runtime_test(objective_tests)
add_runtime_test(navigation_route_tests)
add_runtime_test(campaign_ai_tests)
add_runtime_test(weapon_tests)
add_runtime_test(pickup_effects_tests)
add_runtime_test(unit_kinds_tests)

add_executable(battletanx_campaign_audit tools/campaign_audit.cpp)
target_link_libraries(battletanx_campaign_audit PRIVATE battletanx_core)
target_compile_options(battletanx_campaign_audit PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall;-Wextra;-Wpedantic;-Werror>
    $<$<CXX_COMPILER_ID:MSVC>:/W4;/WX;/permissive->
)

# Campaign runtime v3 tests.
add_runtime_test(campaign_ai_parameters_tests)
add_runtime_test(campaign_ai_scenario_tests)
add_runtime_test(campaign_battle_bridge_tests)
add_runtime_test(campaign_flow_tests)
add_runtime_test(campaign_harness_bindings_tests)
add_runtime_test(campaign_harness_tests)
add_runtime_test(campaign_owner_plays_tests)
add_runtime_test(campaign_save_transaction_tests)
add_runtime_test(campaign_scenario_tests)
add_runtime_test(campaign_wave_fixtures_tests)
add_runtime_test(deployables_tests)
add_runtime_test(effect_runtime_tests)
add_runtime_test(frontend_model_tests)
add_runtime_test(frontend_setup_menus_tests)
add_runtime_test(music_selection_tests)
add_runtime_test(objective_world_scenario_tests)
add_runtime_test(owner_links_tests)
add_runtime_test(owner_profiles_tests)
add_runtime_test(placed_objects_tests)
add_runtime_test(powerup_state_tests)
add_runtime_test(queen_world_tests)
add_runtime_test(route_service_tests)
add_runtime_test(spawner_runtime_tests)
add_runtime_test(squad_units_tests)
add_runtime_test(world_runtime_scenario_tests)

# Campaign runtime v3 audit tools (same flags and link as campaign_audit).
add_executable(battletanx_campaign_play_audit tools/campaign_play_audit.cpp)
target_link_libraries(battletanx_campaign_play_audit PRIVATE battletanx_core)
target_compile_options(battletanx_campaign_play_audit PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall;-Wextra;-Wpedantic;-Werror>
    $<$<CXX_COMPILER_ID:MSVC>:/W4;/WX;/permissive->
)
add_executable(battletanx_model_effect_slot_audit tools/model_effect_slot_audit.cpp)
target_link_libraries(battletanx_model_effect_slot_audit PRIVATE battletanx_core)
target_compile_options(battletanx_model_effect_slot_audit PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall;-Wextra;-Wpedantic;-Werror>
    $<$<CXX_COMPILER_ID:MSVC>:/W4;/WX;/permissive->
)
add_executable(battletanx_music_mapping_audit tools/music_mapping_audit.cpp)
target_link_libraries(battletanx_music_mapping_audit PRIVATE battletanx_core)
target_compile_options(battletanx_music_mapping_audit PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall;-Wextra;-Wpedantic;-Werror>
    $<$<CXX_COMPILER_ID:MSVC>:/W4;/WX;/permissive->
)
add_executable(battletanx_spawner_audit tools/spawner_audit.cpp)
target_link_libraries(battletanx_spawner_audit PRIVATE battletanx_core)
target_compile_options(battletanx_spawner_audit PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall;-Wextra;-Wpedantic;-Werror>
    $<$<CXX_COMPILER_ID:MSVC>:/W4;/WX;/permissive->
)
add_executable(battletanx_weapon_pickup_audit tools/weapon_pickup_audit.cpp)
target_link_libraries(battletanx_weapon_pickup_audit PRIVATE battletanx_core)
target_compile_options(battletanx_weapon_pickup_audit PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall;-Wextra;-Wpedantic;-Werror>
    $<$<CXX_COMPILER_ID:MSVC>:/W4;/WX;/permissive->
)
add_executable(battletanx_world_objective_audit tools/world_objective_audit.cpp)
target_link_libraries(battletanx_world_objective_audit PRIVATE battletanx_core)
target_compile_options(battletanx_world_objective_audit PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall;-Wextra;-Wpedantic;-Werror>
    $<$<CXX_COMPILER_ID:MSVC>:/W4;/WX;/permissive->
)

# Keep binary distributions self-contained and free of retail game data. SDL is
# linked statically above, so the installed runtime consists of the native host
# executable and its build/run documentation.
install(TARGETS BattleTanx
    RUNTIME DESTINATION .
    COMPONENT Runtime)
install(FILES README.md COPYING
    DESTINATION .
    COMPONENT Runtime)
if(WIN32)
    install(FILES "Launch BattleTanx.cmd" DESTINATION . COMPONENT Runtime)
endif()
if(BATTLETANX_PORTABLE)
    file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/portable.txt" "BattleTanx portable mode")
    install(FILES "${CMAKE_CURRENT_BINARY_DIR}/portable.txt" DESTINATION . COMPONENT Runtime)
endif()

if(WIN32)
    set(CPACK_GENERATOR ZIP)
    set(BATTLETANX_PACKAGE_PLATFORM "Windows-x64")
else()
    set(CPACK_GENERATOR TGZ)
    set(BATTLETANX_PACKAGE_PLATFORM "Linux-${CMAKE_SYSTEM_PROCESSOR}")
endif()
set(CPACK_ARCHIVE_COMPONENT_INSTALL ON)
set(CPACK_COMPONENTS_ALL Runtime)
set(CPACK_INCLUDE_TOPLEVEL_DIRECTORY ON)
set(BATTLETANX_PACKAGE_SUFFIX "")
if(BATTLETANX_PORTABLE)
    set(BATTLETANX_PACKAGE_SUFFIX "-portable")
endif()
set(CPACK_PACKAGE_FILE_NAME
    "BattleTanx-PC-${PROJECT_VERSION}-${BATTLETANX_PACKAGE_PLATFORM}${BATTLETANX_PACKAGE_SUFFIX}")
set(CPACK_PACKAGE_CHECKSUM SHA256)
include(CPack)
