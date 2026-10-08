module;

#include <cstddef>

export module vw.gfx:render.cull_pipeline;

import std;

import vw.core;
import :frames_in_flight;
import vw.ecs;
import vw.world;
import :camera;
import :resource;
import :render.vulkan_context;
import :render.shadow_map;
import vulkan;

namespace vw::gfx {
using namespace ::vw::ecs;
}

export namespace vw::gfx {


class vulkan_context;

inline constexpr uint32 cull_plane_count = (shadow_map::cascade_count + 1) * 6;

struct cull_frustum_ubo {
    alignas(16) vec4f planes[cull_plane_count];

    alignas(16) vec4f eye;

    alignas(4) uint32 pass_count;
    alignas(4) uint32 ring_count;
    alignas(4) float32 rings_per_doubling;
    alignas(4) uint32 pad;

    alignas(16) vec4f rings;
};

static_assert(offsetof(cull_frustum_ubo, planes) == 0);
static_assert(offsetof(cull_frustum_ubo, eye) == 576);
static_assert(offsetof(cull_frustum_ubo, pass_count) == 592);
static_assert(offsetof(cull_frustum_ubo, ring_count) == 596);
static_assert(offsetof(cull_frustum_ubo, rings_per_doubling) == 600);
static_assert(offsetof(cull_frustum_ubo, rings) == 608);
static_assert(sizeof(cull_frustum_ubo) == 624);

// см. docs/rendering.md#кольца-расстояния
struct cull_rings {
    vec3f origin{0.0F, 0.0F, 0.0F};
    float32 first_width = 1.0F;
};

class cull_pipeline {
public:

    static constexpr float32 rings_per_doubling = 2.0F;

    explicit cull_pipeline(
        vulkan_context& context,
        vk::DescriptorPool descriptor_pool
    );
    ~cull_pipeline();

    cull_pipeline(const cull_pipeline&)            = delete;
    auto operator=(const cull_pipeline&) -> cull_pipeline& = delete;
    cull_pipeline(cull_pipeline&&)                 = delete;
    auto operator=(cull_pipeline&&) -> cull_pipeline&      = delete;

    auto update_frustums(
        uint32 frame_index,
        const vw::spatial::frustum& view_frustum,
        std::span<const vw::spatial::frustum> shadow_frustums,
        const vec4f& eye,
        const cull_rings& rings
    ) -> void;

    auto dispatch(
        vk::CommandBuffer cmd,
        const std::vector<std::unique_ptr<combined_buffer>>& buffers,
        uint32 frame_index
    ) -> void;

    [[nodiscard]] auto get_buffer_descriptor_set_layout() const -> vk::DescriptorSetLayout {
        return buffer_descriptor_set_layout_;
    }

private:
    auto create_descriptor_set_layouts_() -> void;
    auto create_pipeline_() -> void;
    auto create_frustum_ubos_() -> void;

    vulkan_context* context_;
    vk::DescriptorPool descriptor_pool_ = nullptr;

    std::unique_ptr<shader> compute_shader_;
    vk::Pipeline compute_pipeline_                      = nullptr;
    vk::PipelineLayout compute_pipeline_layout_         = nullptr;
    vk::DescriptorSetLayout frustum_descriptor_set_layout_ = nullptr;
    vk::DescriptorSetLayout buffer_descriptor_set_layout_  = nullptr;

    std::array<std::unique_ptr<uniform_buffer>, frames_in_flight> frustum_ubos_;
    std::array<vk::DescriptorSet, frames_in_flight> frustum_descriptor_sets_{};
};

}  // namespace vw::gfx
