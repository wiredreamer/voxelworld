export module vw.gfx:renderer.stats;

import std;

import vw.core;
import :render.gpu_timer;
import :resource;

export namespace vw::gfx {

struct render_timing_stats {
    gpu_timing_stats gpu{};

    float32 mesh_sync_ms            = 0.0f;
    float32 occupancy_update_ms     = 0.0f;
    float32 light_cache_ms          = 0.0f;
    float32 shadow_map_update_ms    = 0.0f;
    float32 buffer_pool_update_ms   = 0.0f;
    float32 compute_cull_ms         = 0.0f;

    float32 light_gather_ms         = 0.0f;
    float32 grass_prepare_ms        = 0.0f;
    float32 light_cull_ms           = 0.0f;

    float32 shadow_pass_ms          = 0.0f;
    float32 world_pass_ms           = 0.0f;
    float32 world_pass_uniform_ms   = 0.0f;
    float32 world_pass_geometry_ms  = 0.0f;
    float32 world_pass_debug_ms     = 0.0f;
    float32 world_pass_imgui_ms     = 0.0f;
    float32 shadow_cascades_drawn   = 0.0f;
};

struct renderer_stats {
    combined_buffer_pool_stats combined_buffers;
    uint32 draw_call_count = 0;
    occupancy_stats occupancy;
    model_occupancy_stats model_volumes;
    light_cache_stats light_cache;
    render_timing_stats timing;
};
}  // namespace vw::gfx
