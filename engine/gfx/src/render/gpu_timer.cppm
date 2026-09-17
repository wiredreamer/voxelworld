export module vw.gfx:render.gpu_timer;

import std;

import vw.core;
import :render.vulkan_context;
import vulkan;

export namespace vw::gfx {

enum class gpu_stage : uint32 {
    frame,
    buffer_upload,
    compute_cull,
    light_cull,
    shadow_pass,
    shadow_cascade_0,
    shadow_cascade_1,
    shadow_cascade_2,
    shadow_cascade_3,
    shadow_cascade_4,
    world_pass,
    world_geometry,
    world_debug,
    world_imgui,
    count,
};

inline constexpr uint32 gpu_stage_count = static_cast<uint32>(gpu_stage::count);

inline constexpr std::array<std::string_view, gpu_stage_count> gpu_stage_names{
    "gpu_frame",
    "gpu_buffer_upload",
    "gpu_compute_cull",
    "gpu_light_cull",
    "gpu_shadow_pass",
    "gpu_cascade_0",
    "gpu_cascade_1",
    "gpu_cascade_2",
    "gpu_cascade_3",
    "gpu_cascade_4",
    "gpu_world_pass",
    "gpu_world_geometry",
    "gpu_world_debug",
    "gpu_world_imgui",
};

struct gpu_timing_stats {
    std::array<float32, gpu_stage_count> ms{};
    bool supported = false;
};

class gpu_timer final {
public:
    gpu_timer(vulkan_context& context, uint32 frames_in_flight);
    ~gpu_timer();

    gpu_timer(const gpu_timer&)                    = delete;
    auto operator=(const gpu_timer&) -> gpu_timer& = delete;
    gpu_timer(gpu_timer&&)                         = delete;
    auto operator=(gpu_timer&&) -> gpu_timer&      = delete;

    auto reset(vk::CommandBuffer cmd, uint32 frame_index) -> void;
    auto begin(vk::CommandBuffer cmd, gpu_stage stage) const -> void;
    auto end(vk::CommandBuffer cmd, gpu_stage stage) const -> void;
    auto resolve(uint32 frame_index) -> void;

    [[nodiscard]] auto get_stats() const -> const gpu_timing_stats& { return stats_; }
    [[nodiscard]] auto is_supported() const -> bool { return supported_; }

private:
    [[nodiscard]] auto first_query_(uint32 frame_index) const -> uint32;

    vulkan_context* context_;
    vk::QueryPool pool_      = nullptr;
    uint32 frames_in_flight_ = 0;
    uint32 recording_frame_  = 0;
    float64 period_ns_       = 0.0;
    uint64 valid_mask_       = 0;
    bool supported_          = false;
    std::vector<uint8> frame_recorded_;
    std::vector<uint64> scratch_;
    gpu_timing_stats stats_{};
};

}  // namespace vw::gfx
