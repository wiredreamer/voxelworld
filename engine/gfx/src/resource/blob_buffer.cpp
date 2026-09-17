module vw.gfx;

import std;
import vulkan;
import vw.core;
import vw.ecs;
import vw.world;
import vw.platform;
import :vk;

namespace vw::gfx {

using namespace ::vw::ecs;

blob_buffer::blob_buffer(
    vulkan_context& context, deletion_queue& deletion, std::span<const vk::DescriptorSet> sets
)
    : context_(&context)
    , deletion_(&deletion) {
    std::ranges::copy_n(sets.begin(), max_frames_in_flight, sets_.begin());

    for (uint32 frame = 0; frame < max_frames_in_flight; ++frame) {
        capacities_[frame] = default_capacity_;
        buffers_[frame]    = std::make_unique<storage_buffer>(
            *context_, capacities_[frame] * sizeof(blob_data)
        );

        write_binding_(frame);
    }
}

auto blob_buffer::update(
    world_type& world, uint32 frame_index
) -> void {
    blobs_.clear();

    auto view = world.view<blob_shadow_component, transform_component>();
    for (const auto& [ent, blob, transform] : view) {
        vec3f pos = transform.get_position();

        float32 height = blob.get_fall();

        if (world.has<spatial_component>(ent)) {
            const auto& bounds = world.get<spatial_component>(ent).get_bounds();
            const auto size    = bounds.size();
            if (size.x > 0.0F && size.z > 0.0F) {
                pos    = vec3f{bounds.center().x, bounds.min.y, bounds.center().z};
                height = size.y;
            }
        }

        const float32 fall   = std::max(blob.get_fall(), 0.001F);
        const float32 radius = blob.get_radius();
        const float32 reach  = fall * blob_reach_falls;

        const float32 top    = pos.y + (height * 0.5F);
        const float32 bottom = pos.y - reach;

        blobs_.push_back(blob_data{
            .position_radius = vec4f{pos.x, pos.y, pos.z, radius},
            .params          = vec4f{fall, blob.get_strength(), height, reach},
            .cull_a          = vec4f{pos.x, top, pos.z, radius},
            .cull_b          = vec4f{pos.x, bottom, pos.z, 0.0F},
        });
    }

    expand_buffer_if_needed_(frame_index, static_cast<uint32>(blobs_.size()));

    if (!blobs_.empty()) {
        buffers_[frame_index]->copy_from_vector(blobs_);
    }
}

auto blob_buffer::expand_buffer_if_needed_(
    uint32 frame_index, uint32 required_count
) -> void {
    uint32& capacity = capacities_[frame_index];

    if (required_count <= capacity) {
        return;
    }

    while (capacity < required_count) {
        capacity *= 2;
    }

    deletion_->retire(std::exchange(
        buffers_[frame_index],
        std::make_unique<storage_buffer>(*context_, capacity * sizeof(blob_data))
    ));

    write_binding_(frame_index);
}

auto blob_buffer::write_binding_(
    uint32 frame_index
) -> void {
    const vk::DescriptorBufferInfo info{
        .buffer = buffers_[frame_index]->get_buffer(),
        .offset = 0,
        .range  = vk::WholeSize,
    };

    context_->get_device().updateDescriptorSets(
        vk::WriteDescriptorSet{
            .dstSet          = sets_[frame_index],
            .dstBinding      = 3,
            .dstArrayElement = 0,
            .descriptorCount = 1,
            .descriptorType  = vk::DescriptorType::eStorageBuffer,
            .pBufferInfo     = &info,
        },
        nullptr
    );
}

}  // namespace vw::gfx
