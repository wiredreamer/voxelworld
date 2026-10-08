export module vw.gfx:resource.palette_buffer;

import std;

import vw.core;
import vw.ecs;
import vw.world;
import :gpu_buffers;
import vulkan;

namespace vw::gfx {
using namespace ::vw::ecs;
}

export namespace vw::gfx {

class vulkan_context;

// см. docs/lighting.md#палитра
[[nodiscard]] auto palette_color_of(color clr) -> vec3f;

class palette_buffer {
public:
    palette_buffer(
        vulkan_context& context,
        vk::DescriptorPool descriptor_pool,
        vk::DescriptorSetLayout descriptor_set_layout,
        const voxel_registry& registry
    );
    ~palette_buffer();

    [[nodiscard]] auto get_descriptor_set() const -> vk::DescriptorSet;

private:
    vulkan_context* context_;
    std::unique_ptr<storage_buffer> buffer_;
    std::unique_ptr<storage_buffer> materials_;
    vk::DescriptorSet descriptor_set_              = nullptr;
    vk::DescriptorPool descriptor_pool_            = nullptr;
    vk::DescriptorSetLayout descriptor_set_layout_ = nullptr;
};

}  // namespace vw::gfx
