project(testbed)

find_package(imgui CONFIG REQUIRED)

add_executable(${PROJECT_NAME})

target_sources(${PROJECT_NAME}
    PUBLIC
        FILE_SET CXX_MODULES BASE_DIRS src FILES
            src/testbed.cppm
            src/args.cppm
            src/scene.cppm
            src/camera.cppm
            src/cameras.cppm
            src/probes/clusters.cppm
            src/app.cppm
            src/scenes.cppm
            src/scenes/terrain.cppm
            src/scenes/voxel_edits.cppm
            src/scenes/lamp_edits.cppm
            src/scenes/standing_lights.cppm
            src/scenes/blob_shadows.cppm
            src/scenes/animated_crowd.cppm
    PRIVATE
        src/main.cpp
        src/cameras.cpp
        src/scenes.cpp
        src/probes/clusters.cpp
        src/app/app.cpp
        src/app/world_setup.cpp
        src/app/daynight.cpp
        src/app/editing.cpp
        src/app/ui.cpp
        src/scenes/voxel_edits.cpp
        src/scenes/lamp_edits.cpp
        src/scenes/standing_lights.cpp
        src/scenes/blob_shadows.cpp
        src/scenes/animated_crowd.cpp
)

target_link_libraries(${PROJECT_NAME} PRIVATE vwengine imgui::imgui)
target_compile_features(${PROJECT_NAME} PRIVATE cxx_std_23)
vw_use_std_module(${PROJECT_NAME})
vw_set_warnings(${PROJECT_NAME})

if (MSVC)
    target_compile_options(${PROJECT_NAME} PRIVATE /bigobj)
endif()

include(shaders)
vw_setup_shaders(${PROJECT_NAME})

option(VW_ALLOC_PROBE "Count and time every heap allocation in testbed" OFF)
if(VW_ALLOC_PROBE)
    target_sources(${PROJECT_NAME} PRIVATE src/alloc_probe.cpp)
endif()
