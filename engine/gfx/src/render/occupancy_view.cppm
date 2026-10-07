module;

#include <cstddef>

export module vw.gfx:render.occupancy_view;

import std;

import vw.core;
import :camera;
import :resource;
import :resource.occupancy_clipmap;
import :render.vulkan_context;
import vulkan;

export namespace vw::gfx {

struct occupancy_view_push {
    alignas(16) vec4f eye;
    alignas(16) std::array<vec4f, 4> corners;
    alignas(16) vec4f tonemap;
    alignas(16) vec4<int32> base_chunk;
};

static_assert(offsetof(occupancy_view_push, corners) == 16);
static_assert(offsetof(occupancy_view_push, tonemap) == 80);
static_assert(offsetof(occupancy_view_push, base_chunk) == 96);
static_assert(sizeof(occupancy_view_push) == 112);

// см. docs/rendering.md#вид-занятости
class occupancy_view {
public:
    static constexpr float32 reach_voxels = 768.0F;

    occupancy_view(
        vulkan_context& context, vk::RenderPass scene_pass, vk::SampleCountFlagBits samples,
        vk::DescriptorSetLayout occupancy_layout
    );
    ~occupancy_view();

    occupancy_view(const occupancy_view&)                    = delete;
    auto operator=(const occupancy_view&) -> occupancy_view& = delete;
    occupancy_view(occupancy_view&&)                         = delete;
    auto operator=(occupancy_view&&) -> occupancy_view&      = delete;

    auto draw(
        vk::CommandBuffer cmd, const occupancy_clipmap& occupancy, uint32 frame,
        const camera& camera, vec4f tonemap
    ) const -> void;

private:
    vulkan_context* context_;

    shader vertex_;
    shader fragment_;

    vk::PipelineLayout layout_ = nullptr;
    vk::Pipeline pipeline_     = nullptr;
};

}  // namespace vw::gfx
