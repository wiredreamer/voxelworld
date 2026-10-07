module;

#include <cstddef>

export module vw.gfx:resource.light_cache;

import std;

import vw.core;
import :frames_in_flight;
import :gpu_buffers;
import :renderer.settings;
import vw.ecs;
import vw.world;
import vulkan;

export namespace vw::gfx {

struct light_cache_push {
    alignas(16) vec4<int32> base_chunk;
    alignas(16) std::array<vec4<int32>, 3> window;
};

static_assert(offsetof(light_cache_push, window) == 16);
static_assert(sizeof(light_cache_push) == 64);

struct light_cascade_shape {
    int32 texture_side;
    int32 cell_shift;
    uint8 passes_to_settle;

    [[nodiscard]] constexpr auto bricks_per_side() const -> int32 {
        return texture_side / 8;
    }

    [[nodiscard]] constexpr auto slot_count() const -> int32 {
        return bricks_per_side() * bricks_per_side() * bricks_per_side();
    }

    [[nodiscard]] constexpr auto span_voxels() const -> int32 {
        return texture_side << cell_shift;
    }
};

struct light_cache_stats {
    uint32 bricks_frame  = 0;
    uint32 waiting       = 0;
    uint64 bricks_total  = 0;
    uint32 peak_waiting  = 0;
};

// см. docs/lighting.md#кеш-освещённости
class light_cache {
public:
    static constexpr int32 cascade_count = 3;
    static constexpr int32 brick_texels  = 8;

    static constexpr std::array<light_cascade_shape, cascade_count> shapes{{
        {.texture_side = 256, .cell_shift = 0, .passes_to_settle = 16},
        {.texture_side = 128, .cell_shift = 2, .passes_to_settle = 5},
        {.texture_side = 128, .cell_shift = 3, .passes_to_settle = 3},
    }};

    static constexpr uint32 most_bricks_a_frame = 4096;

    static constexpr vk::Format format = vk::Format::eR8G8B8A8Unorm;

    light_cache(
        vulkan_context& context, vk::DescriptorPool descriptor_pool,
        vk::DescriptorSetLayout occupancy_layout
    );
    ~light_cache();

    light_cache(const light_cache&)                    = delete;
    auto operator=(const light_cache&) -> light_cache& = delete;
    light_cache(light_cache&&)                         = delete;
    auto operator=(light_cache&&) -> light_cache&      = delete;

    auto make_ready(vk::CommandBuffer cmd) -> void;

    auto choose_bricks(
        vec3i centre_voxel, std::span<const ecs::occupancy_change> changes,
        const light_cache_settings& settings, uint32 frame
    ) -> void;

    auto dispatch(
        vk::CommandBuffer cmd, vk::DescriptorSet occupancy_set, vec3i base_chunk, uint32 frame
    ) const -> void;

    [[nodiscard]] auto get_sampled_layout() const -> vk::DescriptorSetLayout {
        return sampled_layout_;
    }

    [[nodiscard]] auto get_sampled_set() const -> vk::DescriptorSet {
        return sampled_set_;
    }

    [[nodiscard]] static auto wrap_of(int32 cascade, vec3i base_chunk) -> vec4f;

    [[nodiscard]] auto get_stats() const -> const light_cache_stats& {
        return stats_;
    }

private:
    static constexpr std::size_t most_passes = 16;

    struct slot {
        vec3i brick{};
        uint32 chosen_at  = 0;
        bool assigned     = false;
        uint8 passes_left = 0;
    };

    struct cascade_state {
        vk::Image image         = nullptr;
        vk::DeviceMemory memory = nullptr;
        vk::ImageView view      = nullptr;

        vec3i origin{};
        bool placed = false;
        std::vector<slot> slots;
        std::array<uint32, most_passes + 1> owing{};
    };

    struct frame_state {
        std::unique_ptr<storage_buffer> queue;
        vk::DescriptorSet set = nullptr;
        uint32 bricks         = 0;
    };

    auto create_images_() -> void;
    auto create_pipeline_(vk::DescriptorSetLayout occupancy_layout) -> void;
    auto create_sets_() -> void;

    auto move_window_(int32 cascade, vec3i origin) -> void;
    auto mark_box_(vec3i low_voxel, vec3i high_voxel) -> void;
    auto mark_changed_(const ecs::occupancy_change& change) -> void;
    auto arm_(int32 cascade, slot& held) -> void;
    [[nodiscard]] static auto slot_index_(int32 cascade, vec3i brick) -> std::size_t;

    vulkan_context* context_;
    vk::DescriptorPool descriptor_pool_;

    shader compute_;

    vk::Sampler sampler_                     = nullptr;
    vk::DescriptorSetLayout sampled_layout_  = nullptr;
    vk::DescriptorSetLayout written_layout_  = nullptr;
    vk::PipelineLayout pipeline_layout_      = nullptr;
    vk::Pipeline pipeline_                   = nullptr;
    vk::DescriptorSet sampled_set_           = nullptr;

    std::array<cascade_state, cascade_count> cascades_;
    std::array<frame_state, frames_in_flight> frames_;
    std::vector<vec4<int32>> chosen_;
    uint32 serial_ = 0;
    bool ready_    = false;

    light_cache_stats stats_;
};

}  // namespace vw::gfx
