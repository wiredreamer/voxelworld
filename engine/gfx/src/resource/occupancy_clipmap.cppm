module;

#include <cstddef>

export module vw.gfx:resource.occupancy_clipmap;

import std;

import vw.core;
import :frames_in_flight;
import vw.asset;
import vw.ecs;
import vw.world;
import :gpu_buffers;
import :resource.model_occupancy;
import vulkan;

export namespace vw::gfx {

struct occupancy_params {
    alignas(16) std::array<vec4<int32>, spatial::occupancy_clipmap_layout::level_count> origin;
    alignas(16) std::array<vec4<uint32>, 68> valid;
};

static_assert(offsetof(occupancy_params, valid) == 48);
static_assert(sizeof(occupancy_params) == 1136);
static_assert(spatial::occupancy_clipmap_layout::valid_word_count <= 68 * 4);

struct occupancy_stats {
    uint32 valid_slots    = 0;
    uint32 slot_count     = 0;
    uint32 queued         = 0;
    uint32 packed_frame   = 0;
    uint64 packed_total   = 0;
    uint64 uploaded_bytes = 0;
    float32 pack_ms       = 0.0F;
    float32 pack_peak_ms  = 0.0F;
};

// см. docs/rendering.md#занятость-на-gpu
class occupancy_clipmap {
public:
    using layout = spatial::occupancy_clipmap_layout;

    occupancy_clipmap(
        vulkan_context& context, vk::DescriptorPool descriptor_pool,
        const model_occupancy_buffer& model_volumes
    );
    ~occupancy_clipmap();

    occupancy_clipmap(const occupancy_clipmap&)                    = delete;
    auto operator=(const occupancy_clipmap&) -> occupancy_clipmap& = delete;
    occupancy_clipmap(occupancy_clipmap&&)                         = delete;
    auto operator=(occupancy_clipmap&&) -> occupancy_clipmap&      = delete;

    auto update(ecs::world& world, const vec3f& eye, uint32 frame) -> void;
    auto record_uploads(vk::CommandBuffer cmd, uint32 frame) -> void;

    [[nodiscard]] auto get_descriptor_set_layout() const -> vk::DescriptorSetLayout {
        return set_layout_;
    }

    [[nodiscard]] auto get_descriptor_set(uint32 frame) const -> vk::DescriptorSet {
        return sets_[frame];
    }

    [[nodiscard]] auto get_stats() const -> const occupancy_stats& {
        return stats_;
    }

    [[nodiscard]] auto world_units_per_voxel() const -> float32 {
        return units_per_voxel_;
    }

    [[nodiscard]] auto centre_chunk() const -> vec3i {
        return centre_chunk_;
    }

    [[nodiscard]] auto centre_voxel() const -> vec3i {
        return centre_voxel_;
    }

    [[nodiscard]] auto packed_changes() const -> std::span<const ecs::occupancy_change> {
        return packed_changes_;
    }

private:
    static constexpr vk::DeviceSize staging_bytes = vk::DeviceSize{2} << 20;
    static constexpr float32 pack_budget_ms       = 0.4F;

    struct slot {
        vec3i chunk{};
        bool assigned = false;
        bool valid    = false;
        bool hollow   = false;
        bool queued   = false;
        bool touched  = false;
    };

    struct level_state {
        vk::Image image         = nullptr;
        vk::DeviceMemory memory = nullptr;
        vk::ImageView view      = nullptr;
        bool readable           = false;

        vec3i origin{};
        std::vector<slot> slots;
        std::vector<int32> queue;
        std::size_t queue_head = 0;
    };

    struct frame_state {
        std::unique_ptr<buffer> staging;
        uint8* mapped = nullptr;
        std::unique_ptr<uniform_buffer> params;
        bool params_written = false;
        std::array<std::vector<vk::BufferImageCopy>, layout::level_count> copies;
    };

    auto create_images_() -> void;
    auto create_sets_(const model_occupancy_buffer& model_volumes) -> void;

    auto forget_everything_() -> void;
    auto move_window_(int32 level, vec3i origin) -> void;
    auto touch_(const ecs::occupancy_change& change) -> void;
    auto enqueue_(level_state& state, int32 index) -> void;
    auto pack_queued_(const ecs::world_grid& grid, frame_state& frame) -> void;
    [[nodiscard]] auto read_chunk_(const ecs::world_grid& grid, vec3i chunk) -> asset::model_fill;
    auto stage_(frame_state& frame, int32 level, slot& held, asset::model_fill fill) -> void;
    auto report_packed_(const ecs::occupancy_change& change) -> void;
    [[nodiscard]] auto has_room_for_(int32 level) const -> bool;
    [[nodiscard]] auto coarsest_slot_(vec3i chunk) -> slot&;
    auto write_params_(frame_state& frame) -> void;

    vulkan_context* context_;
    vk::DescriptorPool descriptor_pool_;

    vk::Sampler sampler_                = nullptr;
    vk::DescriptorSetLayout set_layout_ = nullptr;
    std::array<vk::DescriptorSet, frames_in_flight> sets_{};

    std::array<level_state, layout::level_count> levels_;
    std::array<frame_state, frames_in_flight> frames_;

    const ecs::world_grid* grid_ = nullptr;
    uint64 seen_serial_          = 0;
    float32 units_per_voxel_     = 1.0F;
    vec3i centre_chunk_{};
    vec3i centre_voxel_{};
    std::vector<ecs::occupancy_change> packed_changes_;

    std::vector<ecs::occupancy_change> touched_;
    std::size_t touched_head_ = 0;
    vk::DeviceSize staged_    = 0;

    occupancy_params params_{};
    uint32 frames_behind_ = frames_in_flight;
    auto mark_(level_state& state, int32 level, std::size_t index, bool known, bool hollow)
        -> void;

    std::unique_ptr<asset::chunk_occupancy> scratch_;
    occupancy_stats stats_;
};

}  // namespace vw::gfx
