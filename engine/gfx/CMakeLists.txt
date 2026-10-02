find_package(Vulkan REQUIRED)
find_package(glfw3 CONFIG REQUIRED)
find_package(imgui CONFIG REQUIRED)
find_package(Stb REQUIRED)

vw_add_vulkan_module()

add_library(vw_gfx STATIC)

target_sources(vw_gfx
    PUBLIC
        FILE_SET CXX_MODULES BASE_DIRS src FILES
            src/gfx.cppm
            src/vk.cppm
            src/frames_in_flight.cppm
            src/camera/camera.cppm
            src/camera/controllers.cppm
            src/camera/fps_camera_controller.cppm
            src/camera/free_camera_controller.cppm
            src/camera/third_person_camera_controller.cppm
            src/camera/player_input_controller.cppm
            src/resource/resource.cppm
            src/resource/meshing.cppm
            src/resource/mesh_pool.cppm
            src/resource/gpu_buffers.cppm
            src/resource/shader.cppm
            src/resource/buffer.cppm
            src/resource/deletion_queue.cppm
            src/resource/staging_buffer.cppm
            src/resource/palette_buffer.cppm
            src/resource/light_buffer.cppm
            src/resource/light_grid.cppm
            src/resource/combined_buffer.cppm
            src/resource/combined_buffer_pool.cppm
            src/resource/image.cppm
            src/render/render.cppm
            src/render/vulkan_context.cppm
            src/render/gpu_timer.cppm
            src/render/shadow_map.cppm
            src/render/cull_pipeline.cppm
            src/render/render_settings.cppm
            src/render/render_uniforms.cppm
            src/render/renderer_stats.cppm
            src/render/renderer.cppm
            src/debug/debug.cppm
            src/debug/debug_window.cppm
            src/debug/debug_primitive.cppm
            src/engine/stats.cppm
            src/engine/report.cppm
            src/engine/app.cppm
            src/engine/frame_recorder.cppm
            src/engine/engine.cppm
    PRIVATE
        src/camera/camera.cpp
        src/camera/fps_camera_controller.cpp
        src/camera/free_camera_controller.cpp
        src/camera/third_person_camera_controller.cpp
        src/camera/player_input_controller.cpp
        src/resource/shader.cpp
        src/resource/buffer.cpp
        src/resource/staging_buffer.cpp
        src/resource/deletion_queue.cpp
        src/resource/mesh.cpp
        src/resource/mesh_pool.cpp
        src/resource/palette_buffer.cpp
        src/resource/light_buffer.cpp
        src/resource/blob_buffer.cpp
        src/resource/light_grid.cpp
        src/resource/combined_buffer.cpp
        src/resource/combined_buffer_pool.cpp
        src/resource/image.cpp
        src/render/vulkan_context.cpp
        src/render/gpu_timer.cpp
        src/render/shadow_map.cpp
        src/render/cull_pipeline.cpp
        src/render/renderer.cpp
        src/debug/debug_primitive.cpp
        src/debug/debug_window.cpp
        src/debug/debug_panels_stats.cpp
        src/debug/debug_panels_animation.cpp
        src/debug/debug_panels_settings.cpp
        src/engine/report.cpp
        src/engine/frame_recorder.cpp
        src/engine/engine.cpp
)

target_link_libraries(vw_gfx
    PUBLIC vw_core vw_asset vw_ecs vw_world vw_platform VulkanHppModule
    PRIVATE glfw imgui::imgui)
target_compile_features(vw_gfx PUBLIC cxx_std_23)
target_include_directories(vw_gfx SYSTEM PRIVATE ${Stb_INCLUDE_DIR})
vw_use_std_module(vw_gfx)
vw_set_warnings(vw_gfx)
