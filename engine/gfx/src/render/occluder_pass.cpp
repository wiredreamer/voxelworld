module vw.gfx;

import std;
import vulkan;
import vw.core;
import :vk;

namespace vw::gfx {

namespace {

constexpr uint32 group_side = 8;

// см. docs/rendering.md#отсев-по-заслонам
constexpr int32 widening = 1;

struct level_push {
    vec4<int32> source;
    vec4<int32> target;
    vec4<int32> stretches;
};

}  // namespace

occluder_pass::occluder_pass(
    vulkan_context& context, vk::DescriptorPool descriptor_pool,
    vk::DescriptorSetLayout occupancy_layout
)
    : context_{&context}
    , descriptor_pool_{descriptor_pool}
    , rays_shader_{context, "shaders/occluders.comp.spv", shader_type::COMPUTE}
    , levels_shader_{context, "shaders/occluder_levels.comp.spv", shader_type::COMPUTE} {
    const vk::Device device = context_->get_device();

    const std::array bindings{
        vk::DescriptorSetLayoutBinding{
            .binding         = 0,
            .descriptorType  = vk::DescriptorType::eUniformBuffer,
            .descriptorCount = 1,
            .stageFlags      = vk::ShaderStageFlagBits::eCompute,
        },
        vk::DescriptorSetLayoutBinding{
            .binding         = 1,
            .descriptorType  = vk::DescriptorType::eStorageBuffer,
            .descriptorCount = 1,
            .stageFlags      = vk::ShaderStageFlagBits::eCompute,
        },
    };
    set_layout_ = vk_must(
        device.createDescriptorSetLayout({
            .bindingCount = static_cast<uint32>(bindings.size()),
            .pBindings    = bindings.data(),
        }),
        "create occluder descriptor set layout"
    );

    const std::array rays_sets{occupancy_layout, set_layout_};
    rays_layout_ = vk_must(
        device.createPipelineLayout({
            .setLayoutCount = static_cast<uint32>(rays_sets.size()),
            .pSetLayouts    = rays_sets.data(),
        }),
        "create occluder rays pipeline layout"
    );

    const vk::PushConstantRange level_range{
        .stageFlags = vk::ShaderStageFlagBits::eCompute,
        .offset     = 0,
        .size       = sizeof(level_push),
    };
    levels_layout_ = vk_must(
        device.createPipelineLayout({
            .setLayoutCount         = 1,
            .pSetLayouts            = &set_layout_,
            .pushConstantRangeCount = 1,
            .pPushConstantRanges    = &level_range,
        }),
        "create occluder levels pipeline layout"
    );

    rays_pipeline_ = vk_must(
        device.createComputePipeline(
            nullptr, {.stage = rays_shader_.get_stage_info(), .layout = rays_layout_}
        ),
        "create occluder rays pipeline"
    );
    levels_pipeline_ = vk_must(
        device.createComputePipeline(
            nullptr, {.stage = levels_shader_.get_stage_info(), .layout = levels_layout_}
        ),
        "create occluder levels pipeline"
    );

    depths_ = std::make_unique<device_storage_buffer>(*context_, get_depths_bytes());

    std::array<vk::DescriptorSetLayout, frames_in_flight> layouts{};
    layouts.fill(set_layout_);
    const auto sets = vk_must(
        device.allocateDescriptorSets({
            .descriptorPool     = descriptor_pool_,
            .descriptorSetCount = frames_in_flight,
            .pSetLayouts        = layouts.data(),
        }),
        "allocate occluder descriptor sets"
    );
    std::ranges::copy(sets, sets_.begin());

    for (uint32 frame = 0; frame < frames_in_flight; ++frame) {
        params_[frame] = std::make_unique<uniform_buffer>(
            *context_, static_cast<vk::DeviceSize>(sizeof(occluder_params))
        );

        const vk::DescriptorBufferInfo params_info{
            .buffer = params_[frame]->get_buffer(),
            .offset = 0,
            .range  = sizeof(occluder_params),
        };
        const vk::DescriptorBufferInfo depths_info{
            .buffer = depths_->get_buffer(),
            .offset = 0,
            .range  = vk::WholeSize,
        };

        device.updateDescriptorSets(
            {
                vk::WriteDescriptorSet{
                    .dstSet          = sets_[frame],
                    .dstBinding      = 0,
                    .descriptorCount = 1,
                    .descriptorType  = vk::DescriptorType::eUniformBuffer,
                    .pBufferInfo     = &params_info,
                },
                vk::WriteDescriptorSet{
                    .dstSet          = sets_[frame],
                    .dstBinding      = 1,
                    .descriptorCount = 1,
                    .descriptorType  = vk::DescriptorType::eStorageBuffer,
                    .pBufferInfo     = &depths_info,
                },
            },
            nullptr
        );
    }
}

occluder_pass::~occluder_pass() {
    const vk::Device device = context_->get_device();

    device.destroyPipeline(rays_pipeline_);
    device.destroyPipeline(levels_pipeline_);
    device.destroyPipelineLayout(rays_layout_);
    device.destroyPipelineLayout(levels_layout_);
    device.destroyDescriptorSetLayout(set_layout_);
}

auto occluder_pass::get_depths() const -> vk::Buffer {
    return depths_->get_buffer();
}

auto occluder_pass::dispatch(
    vk::CommandBuffer cmd, vk::DescriptorSet occupancy_set, const occluder_view& view,
    uint32 frame_index
) -> void {
    params_[frame_index]->copy_from_struct(occluder_params{
        .eye = {view.eye_voxels.x, view.eye_voxels.y, view.eye_voxels.z, view.world_units_per_voxel},
        .forward    = {view.forward.x, view.forward.y, view.forward.z, 0.0f},
        .right      = {view.right.x, view.right.y, view.right.z, 0.0f},
        .up         = {view.up.x, view.up.y, view.up.z, 0.0f},
        .base_chunk = {view.base_chunk.x, view.base_chunk.y, view.base_chunk.z, 0},
        .rays       = {rays_wide, rays_high, view.thickness_voxels, view.most_steps},
        .stretches  = {stretches, stretch_voxels, 0, 0},
    });

    const auto settle = [&] {
        cmd.pipelineBarrier(
            vk::PipelineStageFlagBits::eComputeShader,
            vk::PipelineStageFlagBits::eComputeShader,
            {},
            vk::MemoryBarrier{
                .srcAccessMask = vk::AccessFlagBits::eShaderWrite,
                .dstAccessMask = vk::AccessFlagBits::eShaderRead | vk::AccessFlagBits::eShaderWrite,
            },
            nullptr, nullptr
        );
    };

    const auto groups = [](int32 cells) -> uint32 {
        return (static_cast<uint32>(cells) + group_side - 1) / group_side;
    };

    const std::array rays_sets{occupancy_set, sets_[frame_index]};
    cmd.bindPipeline(vk::PipelineBindPoint::eCompute, rays_pipeline_);
    cmd.bindDescriptorSets(vk::PipelineBindPoint::eCompute, rays_layout_, 0, rays_sets, nullptr);
    cmd.dispatch(groups(rays_wide + 1), groups(rays_high + 1), static_cast<uint32>(stretches));
    settle();

    cmd.bindPipeline(vk::PipelineBindPoint::eCompute, levels_pipeline_);
    cmd.bindDescriptorSets(
        vk::PipelineBindPoint::eCompute, levels_layout_, 0, sets_[frame_index], nullptr
    );

    occluder_level source{.offset = 0, .width = rays_wide + 1, .height = rays_high + 1};
    for (uint32 level = 0; level < pyramid.level_count; ++level) {
        const occluder_level target = pyramid.levels[level];

        cmd.pushConstants<level_push>(
            levels_layout_, vk::ShaderStageFlagBits::eCompute, 0,
            level_push{
                .source = {source.offset, source.width, source.height, level == 0 ? 1 : 2},
                .target    = {target.offset, target.width, target.height, level == 0 ? widening : 0},
                .stretches = {level == 0 ? stretches : 1, pyramid.corner_count(), 0, 0},
            }
        );
        cmd.dispatch(groups(target.width), groups(target.height), 1);
        settle();

        source = target;
    }
}

}  // namespace vw::gfx
