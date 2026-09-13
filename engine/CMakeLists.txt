cmake_minimum_required(VERSION 4.0)

project(vwengine)

add_subdirectory(core)
add_subdirectory(asset)
add_subdirectory(ecs)
add_subdirectory(world)

# The headless half of the engine stops here: core, asset, ecs and world know
# nothing about a GPU, and a build with VW_BUILD_GFX=OFF never looks for one.
if(NOT VW_BUILD_GFX)
    return()
endif()

add_subdirectory(platform)
add_subdirectory(gfx)

# vwengine is now nothing but a convenience bundle of the module libraries:
# with the include tree gone there are no headers to hand out, and an
# application reaches the engine through `import` alone.
add_library(${PROJECT_NAME} INTERFACE)

target_compile_features(${PROJECT_NAME} INTERFACE cxx_std_23)

target_link_libraries(${PROJECT_NAME} INTERFACE
        vw_core
        vw_asset
        vw_ecs
        vw_world
        vw_platform
        vw_gfx
)
