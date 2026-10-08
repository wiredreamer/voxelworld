export module vw.gfx:engine.stats;

import std;

import vw.core;

export namespace vw::gfx {

struct engine_stats {
    float32 fps             = 0.0f;
    float32 frame_ms        = 0.0f;
    float32 world_update_ms = 0.0f;
    float32 world_render_ms = 0.0f;
    float32 begin_frame_ms  = 0.0f;
    float32 app_render_ms   = 0.0f;
    float32 renderer_ms     = 0.0f;
    float32 end_frame_ms    = 0.0f;
    uint64 ram_usage_bytes  = 0;
    uint64 vram_usage_bytes = 0;

    uint64 ram_peak_bytes  = 0;
    uint64 vram_peak_bytes = 0;

    uint64 commit_bytes      = 0;
    uint64 commit_peak_bytes = 0;
};

struct bench_config {
    uint32 warmup_frames  = 0;
    uint32 measure_frames = 0;
    std::string report_path;

    std::string json_path;

    std::string series_path;

    uint32 workers = 0;

    float32 fixed_delta_seconds = 0.0f;

    [[nodiscard]] auto enabled() const -> bool {
        return measure_frames > 0;
    }
};

}  // namespace vw::gfx
