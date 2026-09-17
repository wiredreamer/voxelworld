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

light_buffer::light_buffer(
    vulkan_context& context,
    deletion_queue& deletion,
    vk::DescriptorPool descriptor_pool,
    vk::DescriptorSetLayout descriptor_set_layout
)
    : context_(&context)
    , deletion_(&deletion)
    , descriptor_pool_(descriptor_pool)
    , descriptor_set_layout_(descriptor_set_layout) {
    const std::vector<vk::DescriptorSetLayout> layouts(
        max_frames_in_flight, descriptor_set_layout_
    );

    const auto sets = vk_must(
        context_->get_device().allocateDescriptorSets({
            .descriptorPool     = descriptor_pool_,
            .descriptorSetCount = max_frames_in_flight,
            .pSetLayouts        = layouts.data(),
        }),
        "allocate light descriptor sets"
    );

    for (uint32 frame = 0; frame < max_frames_in_flight; ++frame) {
        capacities_[frame]     = default_capacity_;
        lights_buffers_[frame] = std::make_unique<storage_buffer>(
            *context_, capacities_[frame] * sizeof(point_light_data)
        );
        descriptor_sets_[frame] = sets[frame];

        update_descriptor_set_(frame);
    }
}

light_buffer::~light_buffer() {
    if (descriptor_pool_) {
        static_cast<void>(
            context_->get_device().freeDescriptorSets(descriptor_pool_, descriptor_sets_)
        );
    }
}

auto light_buffer::update(
    world_type& world, const spatial::frustum& frustum, const vec3f& eye, uint32 frame_index
) -> void {
    std::vector<point_light_data>& point_lights_data = lights_;
    point_lights_data.clear();

    auto view = world.view<light_component, transform_component>();
    for (const auto& [ent, light_comp, transform_comp] : view) {
        const vec3f& pos      = transform_comp.get_position();
        const float32 range   = light_comp.get_range();

        const spatial::aabb reach{
            .min = vec3f{pos.x - range, pos.y - range, pos.z - range},
            .max = vec3f{pos.x + range, pos.y + range, pos.z + range},
        };

        if (!frustum.intersects(reach)) {
            continue;
        }

        const vec3f& col = light_comp.get_color();

        point_lights_data.push_back(point_light_data{
            .position  = vec4f(pos.x, pos.y, pos.z, 0.0f),
            .color     = vec4f(col.x, col.y, col.z, 0.0f),
            .intensity = light_comp.get_intensity(),
            .range     = range,
        });
    }

    const auto max_visible = static_cast<std::size_t>(max_visible_);

    if (point_lights_data.size() > max_visible) {
        const auto nth = point_lights_data.begin() + static_cast<std::ptrdiff_t>(max_visible);

        std::nth_element(
            point_lights_data.begin(), nth, point_lights_data.end(),
            [&eye](const point_light_data& a, const point_light_data& b) -> bool {
                const auto d2 = [&eye](const point_light_data& l) -> float32 {
                    const float32 dx = l.position.x - eye.x;
                    const float32 dy = l.position.y - eye.y;
                    const float32 dz = l.position.z - eye.z;
                    return (dx * dx) + (dy * dy) + (dz * dz);
                };
                return d2(a) < d2(b);
            }
        );

        point_lights_data.resize(max_visible);
    }

    lights_count_ = static_cast<uint32>(point_lights_data.size());

    expand_buffer_if_needed_(frame_index, lights_count_);

    if (lights_count_ > 0) {
        lights_buffers_[frame_index]->copy_from_vector(point_lights_data);
    }
}

auto light_buffer::get_descriptor_set(uint32 frame_index) const -> vk::DescriptorSet {
    return descriptor_sets_[frame_index];
}

auto light_buffer::is_empty() const -> bool {
    return lights_count_ == 0;
}

auto light_buffer::get_lights_count() const -> uint32 {
    return lights_count_;
}

auto light_buffer::expand_buffer_if_needed_(uint32 frame_index, uint32 required_count) -> void {
    uint32& capacity = capacities_[frame_index];

    if (required_count <= capacity) {
        return;
    }

    while (capacity < required_count) {
        capacity *= 2;
    }

    auto new_lights_buffer = std::make_unique<storage_buffer>(
        *context_, capacity * sizeof(point_light_data)
    );

    deletion_->retire(std::exchange(lights_buffers_[frame_index], std::move(new_lights_buffer)));
    update_descriptor_set_(frame_index);
}

auto light_buffer::update_descriptor_set_(uint32 frame_index) -> void {
    const vk::DescriptorBufferInfo storage_buffer_info{
        .buffer = lights_buffers_[frame_index]->get_buffer(),
        .offset = 0,
        .range  = vk::WholeSize,
    };

    context_->get_device().updateDescriptorSets(
        vk::WriteDescriptorSet{
            .dstSet          = descriptor_sets_[frame_index],
            .dstBinding      = 0,
            .dstArrayElement = 0,
            .descriptorCount = 1,
            .descriptorType  = vk::DescriptorType::eStorageBuffer,
            .pBufferInfo     = &storage_buffer_info,
        },
        nullptr
    );
}

}  // namespace vw::gfx
