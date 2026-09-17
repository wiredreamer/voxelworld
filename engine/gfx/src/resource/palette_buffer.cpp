module vw.gfx;

import std;
import vulkan;
import vw.core;
import vw.ecs;
import vw.world;
import vw.platform;
import :vk;

namespace vw::gfx {

namespace {

constexpr float32 palette_gamma = 1.5f;

struct palette_entry {
    alignas(16) vec3f color;
    alignas(4) float32 glow;
};

static_assert(sizeof(palette_entry) == 16);

[[nodiscard]] auto decode(uint8 channel) -> float32 {
    return std::pow(static_cast<float32>(channel) / 255.0f, palette_gamma);
}

}  // namespace

palette_buffer::palette_buffer(
    vulkan_context& context,
    vk::DescriptorPool descriptor_pool,
    vk::DescriptorSetLayout descriptor_set_layout,
    const voxel_registry& registry
)
    : context_(&context)
    , descriptor_pool_(descriptor_pool)
    , descriptor_set_layout_(descriptor_set_layout) {
    const std::span<const voxel_type> types = registry.all();

    std::vector<palette_entry> palette_data;
    palette_data.reserve(types.size());
    for (const voxel_type& type : types) {
        const color clr = type.material.clr;

        palette_data.push_back(palette_entry{
            .color = vec3f{decode(clr.r()), decode(clr.g()), decode(clr.b())},
            .glow  = static_cast<float32>(type.material.glow) / 255.0f,
        });
    }

    const std::size_t palette_bytes = palette_data.size() * sizeof(palette_entry);

    buffer_ = std::make_unique<storage_buffer>(*context_, palette_bytes);
    buffer_->copy_from(palette_data.data(), palette_bytes);

    descriptor_set_ = vk_must(
        context_->get_device().allocateDescriptorSets({
            .descriptorPool     = descriptor_pool_,
            .descriptorSetCount = 1,
            .pSetLayouts        = &descriptor_set_layout_,
        }),
        "allocate palette descriptor set"
    ).front();

    const vk::DescriptorBufferInfo buffer_info{
        .buffer = buffer_->get_buffer(),
        .offset = 0,
        .range  = vk::WholeSize,
    };

    context_->get_device().updateDescriptorSets(
        vk::WriteDescriptorSet{
            .dstSet          = descriptor_set_,
            .dstBinding      = 0,
            .dstArrayElement = 0,
            .descriptorCount = 1,
            .descriptorType  = vk::DescriptorType::eStorageBuffer,
            .pBufferInfo     = &buffer_info,
        },
        nullptr
    );
}

palette_buffer::~palette_buffer() {
    if (descriptor_set_ && descriptor_pool_) {
        static_cast<void>(
            context_->get_device().freeDescriptorSets(descriptor_pool_, descriptor_set_)
        );
    }
}

auto palette_buffer::get_descriptor_set() const -> vk::DescriptorSet {
    return descriptor_set_;
}

}  // namespace vw::gfx
