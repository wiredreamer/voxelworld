project(sculptor VERSION 0.2.0 LANGUAGES CXX)

# Редактор открывает ассеты прямо из репозитория: он их не только читает, но и
# пишет, а копия в каталоге сборки уносила бы правки в сторону от git и гибла
# при следующей сборке. Сборка «на раздачу» ставит сюда ".", и корнем становится
# каталог рядом с исполняемым файлом.
set(VW_SCULPTOR_ASSET_ROOT "${CMAKE_SOURCE_DIR}/assets"
    CACHE PATH "Asset directory Sculptor reads and writes")

find_package(imgui CONFIG REQUIRED)

add_executable(${PROJECT_NAME})

target_sources(${PROJECT_NAME}
    PUBLIC
        FILE_SET CXX_MODULES BASE_DIRS src FILES
            src/sculptor.cppm
            src/state.cppm
            src/shortcuts.cppm
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
        src/operations/attach_model_operation.cpp
        src/operations/add_socket_component_operation.cpp
        src/operations/add_socket_operation.cpp
        src/operations/add_track_operation.cpp
        src/operations/add_voxel_operation.cpp
        src/operations/close_clip_operation.cpp
        src/operations/composite_operation.cpp
        src/operations/create_clip_operation.cpp
        src/operations/create_entity_operation.cpp
        src/operations/delete_entity_operation.cpp
        src/operations/expand_model_operation.cpp
        src/operations/modify_keyframe_operation.cpp
        src/operations/move_entity_operation.cpp
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
        src/operations/set_pivot_operation.cpp
        src/operations/set_rig_operation.cpp
        src/operations/add_variant_slot_operation.cpp
        src/operations/remove_variant_slot_operation.cpp
        src/operations/select_variant_operation.cpp
        src/operations/set_variant_candidates_operation.cpp
        src/operations/fsm_operations.cpp
        src/operations/structure_operations.cpp
        src/operations/set_transform_operation.cpp
        src/operations/trim_model_operation.cpp
        src/services/clip_service.cpp
        src/services/file_service.cpp
        src/services/fsm_service.cpp
        src/services/keyframe_service.cpp
        src/services/playback_service.cpp
        src/shortcuts/shortcuts.cpp
        src/tools/add_voxel_tool.cpp
        src/tools/color_picker_tool.cpp
        src/tools/move_pivot_tool.cpp
        src/tools/paint_tool.cpp
        src/tools/pose_tool.cpp
        src/tools/remove_voxel_tool.cpp
        src/tools/gizmo.cpp
        src/tools/select_entity_tool.cpp
        src/ui/add_candidate_modal.cpp
        src/ui/add_model_component_modal.cpp
        src/ui/open_clip_modal.cpp
        src/ui/voxel_palette_panel.cpp
        src/ui/breadcrumb_bar.cpp
        src/ui/create_clip_modal.cpp
        src/ui/create_entity_modal.cpp
        src/ui/create_keyframe_modal.cpp
        src/ui/delete_entity_modal.cpp
        src/ui/delete_track_modal.cpp
        src/ui/component_drawers.cpp
        src/ui/edit_components_modal.cpp
        src/ui/entity_properties_panel.cpp
        src/ui/entity_tree_panel.cpp
        src/ui/keyframe_properties_panel.cpp
        src/ui/layer_blend_modal.cpp
        src/ui/menu_bar.cpp
        src/ui/new_file_modal.cpp
        src/ui/open_file_modal.cpp
        src/ui/save_as_modal.cpp
        src/ui/add_machine_modal.cpp
        src/ui/fsm_panel.cpp
        src/ui/gizmo_panel.cpp
        src/ui/shortcuts_modal.cpp
        src/ui/save_clip_as_modal.cpp
        src/ui/socket_panel.cpp
        src/ui/startup_modal.cpp
        src/ui/timeline_panel.cpp
        src/ui/tool_panel.cpp
        src/ui/ui_utils.cpp
)

target_link_libraries(${PROJECT_NAME} PRIVATE vwengine imgui::imgui)
target_compile_definitions(${PROJECT_NAME}
    PRIVATE VW_SCULPTOR_ASSET_ROOT="${VW_SCULPTOR_ASSET_ROOT}")
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
