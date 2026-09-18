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
    vk::DescriptorPool descriptor_pool
)
    : context_(&context)
    , descriptor_pool_(descriptor_pool) {
    compute_shader_ = std::make_unique<shader>(
        *context_, "shaders/cull.comp.spv", shader_type::COMPUTE
    );

    create_descriptor_set_layouts_();
    create_frustum_ubos_();
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
    const vk::DescriptorSetLayoutBinding frustum_binding{
        .binding         = 0,
        .descriptorType  = vk::DescriptorType::eUniformBuffer,
        .descriptorCount = 1,
        .stageFlags      = vk::ShaderStageFlagBits::eCompute,
    };

    frustum_descriptor_set_layout_ = vk_must(
        context_->get_device().createDescriptorSetLayout({
            .bindingCount = 1,
            .pBindings    = &frustum_binding,
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

auto cull_pipeline::create_frustum_ubos_() -> void {
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

        context_->get_device().updateDescriptorSets(
            vk::WriteDescriptorSet{
                .dstSet          = frustum_descriptor_sets_[i],
                .dstBinding      = 0,
                .dstArrayElement = 0,
                .descriptorCount = 1,
                .descriptorType  = vk::DescriptorType::eUniformBuffer,
                .pBufferInfo     = &buffer_info,
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
    const vec3f& eye
) -> void {
    cull_frustum_ubo ubo{};
    ubo.pass_count =
        std::min(combined_buffer::cull_pass_count,
                 1 + static_cast<uint32>(shadow_frustums.size()));
    ubo.eye        = vec4f{eye.x, eye.y, eye.z, 1.0f};

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
            buffer->get_count_buffer(), 0, combined_buffer::cull_pass_count * sizeof(uint32), 0
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
