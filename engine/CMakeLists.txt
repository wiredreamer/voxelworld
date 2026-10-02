cmake_minimum_required(VERSION 4.0)

project(vwengine)

add_subdirectory(core)
add_subdirectory(asset)
add_subdirectory(net)
add_subdirectory(ecs)
add_subdirectory(world)
add_subdirectory(game)

if(NOT VW_BUILD_GFX)
    return()
endif()

add_subdirectory(platform)
add_subdirectory(gfx)

add_library(${PROJECT_NAME} INTERFACE)

target_compile_features(${PROJECT_NAME} INTERFACE cxx_std_23)

target_link_libraries(${PROJECT_NAME} INTERFACE
        vw_core
        vw_asset
        vw_net
        vw_ecs
        vw_world
        vw_platform
        vw_gfx
)
