export module vw.gfx:render.frame_probe;

import std;

import vw.core;
import :render.vulkan_context;
import :gpu_buffers;
import :resource;
import vulkan;

export namespace vw::gfx {

// см. docs/rendering.md#приборы-кадра
struct frame_probe_stats {
    bool pipeline_counted = false;

    uint64 vertex_invocations   = 0;
    uint64 fragment_invocations = 0;
    uint64 clipped_primitives   = 0;

    bool cull_counted = false;

    uint32 commands_offered = 0;
    uint32 commands_drawn   = 0;
};

class frame_probe final {
public:
    frame_probe(vulkan_context& context, uint32 frames_in_flight);
    ~frame_probe();

    frame_probe(const frame_probe&)                    = delete;
    auto operator=(const frame_probe&) -> frame_probe& = delete;
    frame_probe(frame_probe&&)                         = delete;
    auto operator=(frame_probe&&) -> frame_probe&      = delete;

    auto set_enabled(bool enabled) -> void {
        enabled_ = enabled;
    }

    [[nodiscard]] auto is_enabled() const -> bool {
        return enabled_;
    }

    auto reset(vk::CommandBuffer cmd, uint32 frame_index) -> void;

    auto copy_cull_counts(
        vk::CommandBuffer cmd, std::span<const std::unique_ptr<combined_buffer>> buffers,
        uint32 frame_index
    ) -> void;

    auto begin(vk::CommandBuffer cmd) const -> void;
    auto end(vk::CommandBuffer cmd) const -> void;

    auto resolve(uint32 frame_index) -> void;

    [[nodiscard]] auto get_stats() const -> const frame_probe_stats& {
        return stats_;
    }

private:
    static constexpr uint32 most_buffers = 64;

    struct frame_state {
        std::unique_ptr<storage_buffer> counts;
        uint32 buffers_copied   = 0;
        uint32 commands_offered = 0;
        bool queried            = false;
    };

    vulkan_context* context_;
    vk::QueryPool pool_ = nullptr;
    std::vector<frame_state> frames_;
    uint32 recording_frame_ = 0;
    bool recording_         = false;
    bool enabled_           = false;
    frame_probe_stats stats_{};
};

}  // namespace vw::gfx
