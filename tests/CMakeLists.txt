cmake_minimum_required(VERSION 4.0)
project(voxelworld_tests)

find_package(Catch2 3 CONFIG QUIET)

if(NOT Catch2_FOUND)
    include(FetchContent)
    FetchContent_Declare(Catch2
        GIT_REPOSITORY https://github.com/catchorg/Catch2.git
        GIT_TAG v3.15.3
        GIT_SHALLOW TRUE
    )
    FetchContent_MakeAvailable(Catch2)
    list(APPEND CMAKE_MODULE_PATH "${catch2_SOURCE_DIR}/extras")
endif()

include(CTest)
include(Catch)

add_executable(core_tests
    core/voxels.test.cpp
    core/color.test.cpp
    core/vec2.test.cpp
    core/vec3.test.cpp
    core/vec4.test.cpp
    core/quat.test.cpp
    core/mat4.test.cpp
    core/math.test.cpp
    core/transform.test.cpp
    core/cluster_grid.test.cpp
    core/log.test.cpp
)

target_link_libraries(core_tests PRIVATE
    vw_core
    Catch2::Catch2WithMain
)

target_compile_features(core_tests PRIVATE cxx_std_23)
vw_use_std_module(core_tests)

catch_discover_tests(core_tests)

add_executable(asset_tests
    asset/model_fill.test.cpp
    asset/model_edit.test.cpp
    asset/model_trim.test.cpp
    asset/animation_fsm.test.cpp
    asset/voxf_format.test.cpp
    asset/asset_ref.test.cpp
    asset/model_library.test.cpp
    asset/serial.test.cpp
)

target_link_libraries(asset_tests PRIVATE
    vw_core
    vw_asset
    Catch2::Catch2WithMain
)

target_compile_features(asset_tests PRIVATE cxx_std_23)
vw_use_std_module(asset_tests)

catch_discover_tests(asset_tests)

add_executable(ecs_tests
    ecs/entity_pool.test.cpp
    ecs/component_pool.test.cpp
    ecs/registry.test.cpp
    ecs/socket.test.cpp
    ecs/animation_layer.test.cpp
    ecs/dynamic_aabb_tree.test.cpp
)

target_link_libraries(ecs_tests PRIVATE
    vw_core
    vw_asset
    vw_ecs
    vw_world
    Catch2::Catch2WithMain
)

target_compile_features(ecs_tests PRIVATE cxx_std_23)
vw_use_std_module(ecs_tests)

catch_discover_tests(ecs_tests)

add_executable(world_tests
    world/headless.test.cpp
    world/hierarchy.test.cpp
    world/column_staging.test.cpp
    world/surface.test.cpp
    world/sky_light.test.cpp
    world/emission.test.cpp
    world/block_light.test.cpp
    world/caves.test.cpp
    world/chunk_links.test.cpp
    world/chunk_walk.test.cpp
    world/cave_culling.test.cpp
    world/scene_serial.test.cpp
    world/component_codec.test.cpp
    world/structure.test.cpp
    world/variant.test.cpp
    world/variant_subtree.test.cpp
    world/rig.test.cpp
)

target_link_libraries(world_tests PRIVATE
    vw_core
    vw_asset
    vw_ecs
    vw_world
    Catch2::Catch2WithMain
)

target_compile_features(world_tests PRIVATE cxx_std_23)
vw_use_std_module(world_tests)

catch_discover_tests(world_tests)

if(VW_BUILD_GFX)
    add_executable(gfx_tests
        gfx/mesher.test.cpp
        gfx/camera.test.cpp
    )

    target_link_libraries(gfx_tests PRIVATE
        vw_core
        vw_asset
        vw_ecs
        vw_world
        vw_gfx
        Catch2::Catch2WithMain
    )

    target_compile_features(gfx_tests PRIVATE cxx_std_23)
    vw_use_std_module(gfx_tests)

    catch_discover_tests(gfx_tests)
endif()

add_executable(view_bench ecs/view_bench.cpp)
target_link_libraries(view_bench PRIVATE vw_core vw_ecs)
target_compile_features(view_bench PRIVATE cxx_std_23)
vw_use_std_module(view_bench)
