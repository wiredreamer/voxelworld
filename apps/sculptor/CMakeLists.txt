project(sculptor VERSION 0.2.0 LANGUAGES CXX)

find_package(imgui CONFIG REQUIRED)

add_executable(${PROJECT_NAME})

target_sources(${PROJECT_NAME}
    PUBLIC
        FILE_SET CXX_MODULES BASE_DIRS src FILES
            src/sculptor.cppm
            src/state.cppm
            src/operations.cppm
            src/services.cppm
            src/tools.cppm
            src/ui.cppm
            src/app.cppm
    PRIVATE
        src/main.cpp
        src/app/app.cpp
        src/app/app_state.cpp
        src/operations/add_animation_target_operation.cpp
        src/operations/add_keyframe_operation.cpp
        src/operations/add_model_component_operation.cpp
        src/operations/add_socket_component_operation.cpp
        src/operations/add_socket_operation.cpp
        src/operations/add_track_operation.cpp
        src/operations/add_voxel_operation.cpp
        src/operations/close_clip_operation.cpp
        src/operations/create_clip_operation.cpp
        src/operations/create_entity_operation.cpp
        src/operations/delete_entity_operation.cpp
        src/operations/expand_model_operation.cpp
        src/operations/modify_keyframe_operation.cpp
        src/operations/operation_manager.cpp
        src/operations/paint_voxel_operation.cpp
        src/operations/remove_animation_target_operation.cpp
        src/operations/remove_keyframe_operation.cpp
        src/operations/remove_model_component_operation.cpp
        src/operations/remove_socket_component_operation.cpp
        src/operations/remove_socket_operation.cpp
        src/operations/remove_track_operation.cpp
        src/operations/remove_voxel_operation.cpp
        src/operations/set_socket_transform_operation.cpp
        src/operations/set_transform_operation.cpp
        src/services/clip_service.cpp
        src/services/file_service.cpp
        src/services/keyframe_service.cpp
        src/services/playback_service.cpp
        src/tools/add_voxel_tool.cpp
        src/tools/color_picker_tool.cpp
        src/tools/paint_tool.cpp
        src/tools/remove_voxel_tool.cpp
        src/tools/select_entity_tool.cpp
        src/ui/add_model_component_modal.cpp
        src/ui/clip_manager_panel.cpp
        src/ui/block_palette_panel.cpp
        src/ui/create_clip_modal.cpp
        src/ui/create_entity_modal.cpp
        src/ui/create_keyframe_modal.cpp
        src/ui/delete_entity_modal.cpp
        src/ui/delete_track_modal.cpp
        src/ui/entity_properties_panel.cpp
        src/ui/entity_tree_panel.cpp
        src/ui/keyframe_properties_panel.cpp
        src/ui/layer_blend_modal.cpp
        src/ui/menu_bar.cpp
        src/ui/new_file_modal.cpp
        src/ui/open_file_modal.cpp
        src/ui/save_as_modal.cpp
        src/ui/save_clip_as_modal.cpp
        src/ui/socket_panel.cpp
        src/ui/startup_modal.cpp
        src/ui/timeline_panel.cpp
        src/ui/tool_panel.cpp
        src/ui/ui_utils.cpp
)

target_link_libraries(${PROJECT_NAME} PRIVATE vwengine imgui::imgui)
vw_use_std_module(${PROJECT_NAME})
vw_set_warnings(${PROJECT_NAME})
target_compile_features(${PROJECT_NAME} PRIVATE cxx_std_23)

if (MSVC)
    target_compile_options(${PROJECT_NAME} PRIVATE /bigobj)
endif()

configure_file(
        ${CMAKE_CURRENT_SOURCE_DIR}/src/app/sculptor_version.h.in
        ${CMAKE_CURRENT_BINARY_DIR}/include/sculptor_version.h
        @ONLY
)

target_include_directories(${PROJECT_NAME} PRIVATE
        ${CMAKE_CURRENT_BINARY_DIR}/include
)

target_include_directories(${PROJECT_NAME} PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
)

include(shaders)
vw_setup_shaders(${PROJECT_NAME})

include(static-analyzers)
vw_setup_static_analyzers(${PROJECT_NAME})
