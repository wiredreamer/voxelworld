module;

#include <cstddef>

export module vw.gfx:render.occluder_pass;

import std;

import vw.core;
import :frames_in_flight;
import :resource;
import :render.vulkan_context;
import vulkan;

export namespace vw::gfx {

struct occluder_level {
    int32 offset = 0;
    int32 width  = 0;
    int32 height = 0;

    [[nodiscard]] constexpr auto operator==(const occluder_level&) const -> bool = default;
};

// см. docs/rendering.md#отсев-по-заслонам
struct occluder_pyramid {
    static constexpr std::size_t most_levels = 12;

    int32 rays_wide = 0;
    int32 rays_high = 0;

    std::array<occluder_level, most_levels> levels{};
    uint32 level_count = 0;
    uint32 depth_count = 0;

    int32 stretches = 1;

    [[nodiscard]] constexpr auto corner_count() const -> int32 {
        return (rays_wide + 1) * (rays_high + 1);
    }
};

[[nodiscard]] constexpr auto pyramid_over(int32 rays_wide, int32 rays_high, int32 stretches)
    -> occluder_pyramid {
    occluder_pyramid out{.rays_wide = rays_wide, .rays_high = rays_high, .stretches = stretches};

    int32 offset = out.corner_count() * stretches;
    int32 width  = rays_wide;
    int32 height = rays_high;

    while (out.level_count < occluder_pyramid::most_levels) {
        out.levels[out.level_count++] = {.offset = offset, .width = width, .height = height};
        offset += width * height;

        if (width == 1 && height == 1) {
            break;
        }
        width  = (width + 1) / 2;
        height = (height + 1) / 2;
    }

    out.depth_count = static_cast<uint32>(offset);
    return out;
}

struct occluder_params {
    alignas(16) vec4f eye;
    alignas(16) vec4f forward;
    alignas(16) vec4f right;
    alignas(16) vec4f up;
    alignas(16) vec4<int32> base_chunk;
    alignas(16) vec4<int32> rays;
    alignas(16) vec4<int32> stretches;
};

static_assert(offsetof(occluder_params, eye) == 0);
static_assert(offsetof(occluder_params, forward) == 16);
static_assert(offsetof(occluder_params, right) == 32);
static_assert(offsetof(occluder_params, up) == 48);
static_assert(offsetof(occluder_params, base_chunk) == 64);
static_assert(offsetof(occluder_params, rays) == 80);
static_assert(offsetof(occluder_params, stretches) == 96);
static_assert(sizeof(occluder_params) == 112);

struct occluder_view {
    vec3f eye_voxels{0.0F, 0.0F, 0.0F};
    float32 world_units_per_voxel = 1.0F;
    vec3f forward{0.0F, 0.0F, 1.0F};
    vec3f right{1.0F, 0.0F, 0.0F};
    vec3f up{0.0F, 1.0F, 0.0F};
    vec3i base_chunk{};
    int32 thickness_voxels = 8;
    int32 most_steps       = 16;
};

class occluder_pass final {
public:
    static constexpr int32 rays_wide = 96;
    static constexpr int32 rays_high = 54;

    static constexpr int32 stretches      = 8;
    static constexpr int32 stretch_voxels = 48;

    static constexpr occluder_pyramid pyramid = pyramid_over(rays_wide, rays_high, stretches);

    occluder_pass(
        vulkan_context& context, vk::DescriptorPool descriptor_pool,
        vk::DescriptorSetLayout occupancy_layout
    );
    ~occluder_pass();

    occluder_pass(const occluder_pass&)                    = delete;
    auto operator=(const occluder_pass&) -> occluder_pass& = delete;
    occluder_pass(occluder_pass&&)                         = delete;
    auto operator=(occluder_pass&&) -> occluder_pass&      = delete;

    auto dispatch(
        vk::CommandBuffer cmd, vk::DescriptorSet occupancy_set, const occluder_view& view,
        uint32 frame_index
    ) -> void;

    [[nodiscard]] auto get_depths() const -> vk::Buffer;

    [[nodiscard]] auto get_depths_bytes() const -> vk::DeviceSize {
        return vk::DeviceSize{pyramid.depth_count} * sizeof(float32);
    }

private:
    vulkan_context* context_;
    vk::DescriptorPool descriptor_pool_ = nullptr;

    shader rays_shader_;
    shader levels_shader_;

    vk::DescriptorSetLayout set_layout_      = nullptr;
    vk::PipelineLayout rays_layout_          = nullptr;
    vk::PipelineLayout levels_layout_        = nullptr;
    vk::Pipeline rays_pipeline_              = nullptr;
    vk::Pipeline levels_pipeline_            = nullptr;

    std::unique_ptr<device_storage_buffer> depths_;
    std::array<std::unique_ptr<uniform_buffer>, frames_in_flight> params_;
    std::array<vk::DescriptorSet, frames_in_flight> sets_{};
};

}  // namespace vw::gfx
