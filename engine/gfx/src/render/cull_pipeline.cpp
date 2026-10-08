module vw.gfx;

import std;
import vulkan;
import vw.core;
import vw.ecs;
import vw.world;
import vw.platform;
import :vk;

namespace vw::gfx {

cull_pipeline::cull_pipeline(
    vulkan_context& context,
    vk::DescriptorPool descriptor_pool,
    vk::Buffer occluder_depths,
    vk::DeviceSize occluder_depths_bytes
)
    : context_(&context)
    , descriptor_pool_(descriptor_pool) {
    compute_shader_ = std::make_unique<shader>(
        *context_, "shaders/cull.comp.spv", shader_type::COMPUTE
    );

    create_descriptor_set_layouts_();
    create_frustum_ubos_(occluder_depths, occluder_depths_bytes);
    create_pipeline_();
}

cull_pipeline::~cull_pipeline() {
    const vk::Device device = context_->get_device();

    device.destroyPipeline(compute_pipeline_);
    device.destroyPipelineLayout(compute_pipeline_layout_);
    device.destroyDescriptorSetLayout(frustum_descriptor_set_layout_);
    device.destroyDescriptorSetLayout(buffer_descriptor_set_layout_);
}

auto cull_pipeline::create_descriptor_set_layouts_() -> void {
    const std::array frustum_bindings{
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

    frustum_descriptor_set_layout_ = vk_must(
        context_->get_device().createDescriptorSetLayout({
            .bindingCount = static_cast<uint32>(frustum_bindings.size()),
            .pBindings    = frustum_bindings.data(),
        }),
        "create frustum descriptor set layout"
    );

    std::array<vk::DescriptorSetLayoutBinding, 5> buffer_bindings{};
    for (uint32 i = 0; i < buffer_bindings.size(); i++) {
        buffer_bindings[i] = {
            .binding         = i,
            .descriptorType  = vk::DescriptorType::eStorageBuffer,
            .descriptorCount = 1,
            .stageFlags      = vk::ShaderStageFlagBits::eCompute,
        };
    }

    buffer_descriptor_set_layout_ = vk_must(
        context_->get_device().createDescriptorSetLayout({
            .bindingCount = static_cast<uint32>(buffer_bindings.size()),
            .pBindings    = buffer_bindings.data(),
        }),
        "create buffer descriptor set layout"
    );
}

auto cull_pipeline::create_pipeline_() -> void {
    std::array<vk::DescriptorSetLayout, 2> set_layouts{
        frustum_descriptor_set_layout_,
        buffer_descriptor_set_layout_,
    };

    const vk::PushConstantRange push_constant{
        .stageFlags = vk::ShaderStageFlagBits::eCompute,
        .offset     = 0,
        .size       = sizeof(uint32),
    };

    compute_pipeline_layout_ = vk_must(
        context_->get_device().createPipelineLayout({
            .setLayoutCount         = static_cast<uint32>(set_layouts.size()),
            .pSetLayouts            = set_layouts.data(),
            .pushConstantRangeCount = 1,
            .pPushConstantRanges    = &push_constant,
        }),
        "create compute pipeline layout"
    );

    compute_pipeline_ = vk_must(
        context_->get_device().createComputePipeline(
            nullptr,
            {
                .stage  = compute_shader_->get_stage_info(),
                .layout = compute_pipeline_layout_,
            }
        ),
        "create compute pipeline"
    );
}

auto cull_pipeline::create_frustum_ubos_(
    vk::Buffer occluder_depths, vk::DeviceSize occluder_depths_bytes
) -> void {
    for (uint32 i = 0; i < frames_in_flight; i++) {
        frustum_ubos_[i] = std::make_unique<uniform_buffer>(
            *context_, static_cast<vk::DeviceSize>(sizeof(cull_frustum_ubo))
        );
    }

    std::array<vk::DescriptorSetLayout, frames_in_flight> layouts{};
    layouts.fill(frustum_descriptor_set_layout_);

    const auto sets = vk_must(
        context_->get_device().allocateDescriptorSets({
            .descriptorPool     = descriptor_pool_,
            .descriptorSetCount = frames_in_flight,
            .pSetLayouts        = layouts.data(),
        }),
        "allocate frustum descriptor sets"
    );
    std::ranges::copy(sets, frustum_descriptor_sets_.begin());

    for (uint32 i = 0; i < frames_in_flight; i++) {
        const vk::DescriptorBufferInfo buffer_info{
            .buffer = frustum_ubos_[i]->get_buffer(),
            .offset = 0,
            .range  = sizeof(cull_frustum_ubo),
        };

        const vk::DescriptorBufferInfo depths_info{
            .buffer = occluder_depths,
            .offset = 0,
            .range  = occluder_depths_bytes,
        };

        context_->get_device().updateDescriptorSets(
            {
                vk::WriteDescriptorSet{
                    .dstSet          = frustum_descriptor_sets_[i],
                    .dstBinding      = 0,
                    .dstArrayElement = 0,
                    .descriptorCount = 1,
                    .descriptorType  = vk::DescriptorType::eUniformBuffer,
                    .pBufferInfo     = &buffer_info,
                },
                vk::WriteDescriptorSet{
                    .dstSet          = frustum_descriptor_sets_[i],
                    .dstBinding      = 1,
                    .dstArrayElement = 0,
                    .descriptorCount = 1,
                    .descriptorType  = vk::DescriptorType::eStorageBuffer,
                    .pBufferInfo     = &depths_info,
                },
            },
            nullptr
        );
    }
}

static_assert(
    combined_buffer::cull_pass_count == shadow_map::cascade_count + 1,
    "one cull pass for the camera and one per shadow cascade"
);

auto cull_pipeline::update_frustums(
    uint32 frame_index,
    const vw::spatial::frustum& view_frustum,
    std::span<const vw::spatial::frustum> shadow_frustums,
    const vec4f& eye,
    const cull_rings& rings,
    const cull_occlusion& occlusion
) -> void {
    cull_frustum_ubo ubo{};
    ubo.pass_count =
        std::min(combined_buffer::cull_pass_count,
                 1 + static_cast<uint32>(shadow_frustums.size()));
    ubo.eye        = eye;

    ubo.ring_count         = combined_buffer::cull_ring_count;
    ubo.rings_per_doubling = rings_per_doubling;
    ubo.rings = vec4f{rings.origin.x, rings.origin.y, rings.origin.z, rings.first_width};

    ubo.hidden_region = combined_buffer::cull_hidden_region;
    ubo.view_eye      = vec4f{
        occlusion.eye.x, occlusion.eye.y, occlusion.eye.z,
        static_cast<float32>(std::to_underlying(occlusion.mode))
    };
    ubo.view_forward = vec4f{
        occlusion.forward.x, occlusion.forward.y, occlusion.forward.z, occlusion.nearest_depth
    };
    ubo.view_right = vec4f{
        occlusion.right_over_span.x, occlusion.right_over_span.y, occlusion.right_over_span.z, 0.0f
    };
    ubo.view_up = vec4f{
        occlusion.up_over_span.x, occlusion.up_over_span.y, occlusion.up_over_span.z, 0.0f
    };

    const occluder_pyramid& pyramid = occluder_pass::pyramid;
    ubo.occluder_level_count        = pyramid.level_count;
    for (uint32 level = 0; level < pyramid.level_count; ++level) {
        const occluder_level& held = pyramid.levels[level];
        ubo.occluder_levels[level] = {held.offset, held.width, held.height, 0};
    }

    for (uint32 i = 0; i < 6; i++) {
        const auto& p = view_frustum.planes[i];
        ubo.planes[i] = vec4f{p.normal.x, p.normal.y, p.normal.z, p.distance};
    }

    for (uint32 c = 0; c + 1 < ubo.pass_count; c++) {
        for (uint32 i = 0; i < 6; i++) {
            const auto& p = shadow_frustums[c].planes[i];
            ubo.planes[(c + 1) * 6 + i] =
                vec4f{p.normal.x, p.normal.y, p.normal.z, p.distance};
        }
    }

    frustum_ubos_[frame_index]->copy_from_struct(ubo);
}

auto cull_pipeline::dispatch(
    vk::CommandBuffer cmd,
    const std::vector<std::unique_ptr<combined_buffer>>& buffers,
    uint32 frame_index
) -> void {
    bool any = false;

    for (const auto& buffer : buffers) {
        if (buffer->get_instance_count() == 0) {
            continue;
        }
        any = true;
        cmd.fillBuffer(
            buffer->get_count_buffer(), 0, combined_buffer::cull_region_count * sizeof(uint32), 0
        );
    }

    if (!any) {
        return;
    }

    cmd.pipelineBarrier(
        vk::PipelineStageFlagBits::eTransfer,
        vk::PipelineStageFlagBits::eComputeShader,
        {},
        vk::MemoryBarrier{
            .srcAccessMask = vk::AccessFlagBits::eTransferWrite,
            .dstAccessMask = vk::AccessFlagBits::eShaderRead | vk::AccessFlagBits::eShaderWrite,
        },
        nullptr,
        nullptr
    );

    cmd.bindPipeline(vk::PipelineBindPoint::eCompute, compute_pipeline_);

    cmd.bindDescriptorSets(
        vk::PipelineBindPoint::eCompute, compute_pipeline_layout_, 0,
        frustum_descriptor_sets_[frame_index], nullptr
    );

    for (const auto& buffer : buffers) {
        const uint32 instance_count = buffer->get_instance_count();
        if (instance_count == 0) {
            continue;
        }

        cmd.bindDescriptorSets(
            vk::PipelineBindPoint::eCompute, compute_pipeline_layout_, 1,
            buffer->get_compute_descriptor_set(frame_index), nullptr
        );

        cmd.pushConstants<uint32>(
            compute_pipeline_layout_, vk::ShaderStageFlagBits::eCompute, 0, instance_count
        );

        cmd.dispatch((instance_count + 63) / 64, 1, 1);
    }
}

}  // namespace vw::gfx
