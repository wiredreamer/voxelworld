module vw.gfx;

import std;
import vulkan;
import vw.core;
import :vk;

namespace vw::gfx {

namespace {

auto memory_type_for(
    vk::PhysicalDevice device, uint32 allowed_types, vk::MemoryPropertyFlags wanted
) -> uint32 {
    const vk::PhysicalDeviceMemoryProperties memory = device.getMemoryProperties();

    for (uint32 type = 0; type < memory.memoryTypeCount; ++type) {
        if ((allowed_types & (1U << type)) != 0 &&
            (memory.memoryTypes[type].propertyFlags & wanted) == wanted) {
            return type;
        }
    }

    throw std::runtime_error("no memory type fits a bloom image");
}

auto single_color_pass(
    vk::Device device, vk::Format format, vk::AttachmentLoadOp load, vk::ImageLayout initial
) -> vk::RenderPass {
    vk::AttachmentDescription color{};
    color.format         = format;
    color.samples        = vk::SampleCountFlagBits::e1;
    color.loadOp         = load;
    color.storeOp        = vk::AttachmentStoreOp::eStore;
    color.stencilLoadOp  = vk::AttachmentLoadOp::eDontCare;
    color.stencilStoreOp = vk::AttachmentStoreOp::eDontCare;
    color.initialLayout  = initial;
    color.finalLayout    = vk::ImageLayout::eShaderReadOnlyOptimal;

    vk::AttachmentReference color_ref{};
    color_ref.attachment = 0;
    color_ref.layout     = vk::ImageLayout::eColorAttachmentOptimal;

    vk::SubpassDescription subpass{};
    subpass.pipelineBindPoint    = vk::PipelineBindPoint::eGraphics;
    subpass.colorAttachmentCount = 1;
    subpass.pColorAttachments    = &color_ref;

    vk::SubpassDependency before{};
    before.srcSubpass    = vk::SubpassExternal;
    before.dstSubpass    = 0;
    before.srcStageMask  = vk::PipelineStageFlagBits::eFragmentShader;
    before.srcAccessMask = vk::AccessFlagBits::eShaderRead;
    before.dstStageMask  = vk::PipelineStageFlagBits::eColorAttachmentOutput;
    before.dstAccessMask =
        vk::AccessFlagBits::eColorAttachmentRead | vk::AccessFlagBits::eColorAttachmentWrite;

    vk::SubpassDependency after{};
    after.srcSubpass    = 0;
    after.dstSubpass    = vk::SubpassExternal;
    after.srcStageMask  = vk::PipelineStageFlagBits::eColorAttachmentOutput;
    after.srcAccessMask = vk::AccessFlagBits::eColorAttachmentWrite;
    after.dstStageMask  = vk::PipelineStageFlagBits::eFragmentShader;
    after.dstAccessMask = vk::AccessFlagBits::eShaderRead;

    const std::array dependencies{before, after};

    vk::RenderPassCreateInfo info{};
    info.attachmentCount = 1;
    info.pAttachments    = &color;
    info.subpassCount    = 1;
    info.pSubpasses      = &subpass;
    info.dependencyCount = static_cast<uint32>(dependencies.size());
    info.pDependencies   = dependencies.data();

    return vk_must(device.createRenderPass(info), "create bloom render pass");
}

struct fullscreen_pipeline_request {
    vk::RenderPass pass;
    vk::PipelineLayout layout;
    vk::PipelineShaderStageCreateInfo vertex;
    vk::PipelineShaderStageCreateInfo fragment;
    bool additive = false;
};

auto fullscreen_pipeline(vk::Device device, const fullscreen_pipeline_request& request)
    -> vk::Pipeline {
    const std::array stages{request.vertex, request.fragment};

    vk::PipelineVertexInputStateCreateInfo vertex_input{};

    vk::PipelineInputAssemblyStateCreateInfo input_assembly{};
    input_assembly.topology = vk::PrimitiveTopology::eTriangleList;

    vk::PipelineViewportStateCreateInfo viewport_state{};
    viewport_state.viewportCount = 1;
    viewport_state.scissorCount  = 1;

    const std::array dynamic_states{vk::DynamicState::eViewport, vk::DynamicState::eScissor};

    vk::PipelineDynamicStateCreateInfo dynamic_state{};
    dynamic_state.dynamicStateCount = static_cast<uint32>(dynamic_states.size());
    dynamic_state.pDynamicStates    = dynamic_states.data();

    vk::PipelineRasterizationStateCreateInfo rasterizer{};
    rasterizer.polygonMode = vk::PolygonMode::eFill;
    rasterizer.lineWidth   = 1.0f;
    rasterizer.cullMode    = vk::CullModeFlagBits::eNone;
    rasterizer.frontFace   = vk::FrontFace::eCounterClockwise;

    vk::PipelineMultisampleStateCreateInfo multisampling{};
    multisampling.rasterizationSamples = vk::SampleCountFlagBits::e1;

    vk::PipelineColorBlendAttachmentState blend{};
    blend.colorWriteMask = vk::ColorComponentFlagBits::eR | vk::ColorComponentFlagBits::eG |
                           vk::ColorComponentFlagBits::eB | vk::ColorComponentFlagBits::eA;
    blend.blendEnable         = request.additive ? vk::True : vk::False;
    blend.srcColorBlendFactor = vk::BlendFactor::eOne;
    blend.dstColorBlendFactor = vk::BlendFactor::eOne;
    blend.colorBlendOp        = vk::BlendOp::eAdd;
    blend.srcAlphaBlendFactor = vk::BlendFactor::eOne;
    blend.dstAlphaBlendFactor = vk::BlendFactor::eOne;
    blend.alphaBlendOp        = vk::BlendOp::eAdd;

    vk::PipelineColorBlendStateCreateInfo color_blending{};
    color_blending.attachmentCount = 1;
    color_blending.pAttachments    = &blend;

    vk::PipelineDepthStencilStateCreateInfo depth_stencil{};

    vk::GraphicsPipelineCreateInfo info{};
    info.stageCount          = static_cast<uint32>(stages.size());
    info.pStages             = stages.data();
    info.pVertexInputState   = &vertex_input;
    info.pInputAssemblyState = &input_assembly;
    info.pViewportState      = &viewport_state;
    info.pRasterizationState = &rasterizer;
    info.pMultisampleState   = &multisampling;
    info.pColorBlendState    = &color_blending;
    info.pDepthStencilState  = &depth_stencil;
    info.pDynamicState       = &dynamic_state;
    info.layout              = request.layout;
    info.renderPass          = request.pass;
    info.subpass             = 0;

    return vk_must(device.createGraphicsPipeline(nullptr, info), "create fullscreen pipeline");
}

auto cover(vk::CommandBuffer cmd, vk::Extent2D extent) -> void {
    vk::Viewport viewport{};
    viewport.width    = static_cast<float32>(extent.width);
    viewport.height   = static_cast<float32>(extent.height);
    viewport.minDepth = 0.0f;
    viewport.maxDepth = 1.0f;
    cmd.setViewport(0, viewport);

    vk::Rect2D scissor{};
    scissor.extent = extent;
    cmd.setScissor(0, scissor);
}

}  // namespace

post_process::post_process(
    vulkan_context& context, vk::DescriptorPool descriptor_pool, vk::RenderPass composite_pass
)
    : context_{&context}
    , descriptor_pool_{descriptor_pool}
    , fullscreen_vertex_{context, "shaders/fullscreen.vert.spv", shader_type::VERTEX}
    , bloom_down_fragment_{context, "shaders/bloom_down.frag.spv", shader_type::FRAGMENT}
    , bloom_up_fragment_{context, "shaders/bloom_up.frag.spv", shader_type::FRAGMENT}
    , composite_fragment_{context, "shaders/composite.frag.spv", shader_type::FRAGMENT} {
    create_sampler_();
    create_layouts_();
    create_bloom_passes_();
    create_pipelines_(composite_pass);
    allocate_sets_();
}

post_process::~post_process() {
    const vk::Device device = context_->get_device();

    for (auto& level : levels_) {
        destroy_level_(level);
        static_cast<void>(device.freeDescriptorSets(descriptor_pool_, level.sampled_as));
    }
    static_cast<void>(device.freeDescriptorSets(descriptor_pool_, scene_sampled_as_));

    device.destroyPipeline(composite_pipeline_);
    device.destroyPipeline(bloom_up_pipeline_);
    device.destroyPipeline(bloom_down_pipeline_);
    device.destroyRenderPass(bloom_up_pass_);
    device.destroyRenderPass(bloom_down_pass_);
    device.destroyPipelineLayout(composite_layout_);
    device.destroyPipelineLayout(blur_layout_);
    device.destroyDescriptorSetLayout(sampled_layout_);
    device.destroySampler(sampler_);
}

auto post_process::create_sampler_() -> void {
    vk::SamplerCreateInfo info{};
    info.magFilter    = vk::Filter::eLinear;
    info.minFilter    = vk::Filter::eLinear;
    info.mipmapMode   = vk::SamplerMipmapMode::eNearest;
    info.addressModeU = vk::SamplerAddressMode::eClampToEdge;
    info.addressModeV = vk::SamplerAddressMode::eClampToEdge;
    info.addressModeW = vk::SamplerAddressMode::eClampToEdge;
    info.maxAnisotropy = 1.0f;
    info.maxLod        = 1.0f;

    sampler_ = vk_must(context_->get_device().createSampler(info), "create post-process sampler");
}

auto post_process::create_layouts_() -> void {
    const vk::Device device = context_->get_device();

    vk::DescriptorSetLayoutBinding sampled{};
    sampled.binding         = 0;
    sampled.descriptorType  = vk::DescriptorType::eCombinedImageSampler;
    sampled.descriptorCount = 1;
    sampled.stageFlags      = vk::ShaderStageFlagBits::eFragment;

    sampled_layout_ = vk_must(
        device.createDescriptorSetLayout({.bindingCount = 1, .pBindings = &sampled}),
        "create post-process set layout"
    );

    vk::PushConstantRange push{};
    push.offset     = 0;
    push.size       = sizeof(post_push_constants);
    push.stageFlags = vk::ShaderStageFlagBits::eFragment;

    blur_layout_ = vk_must(
        device.createPipelineLayout({
            .setLayoutCount         = 1,
            .pSetLayouts            = &sampled_layout_,
            .pushConstantRangeCount = 1,
            .pPushConstantRanges    = &push,
        }),
        "create bloom pipeline layout"
    );

    const std::array composite_sets{sampled_layout_, sampled_layout_};

    composite_layout_ = vk_must(
        device.createPipelineLayout({
            .setLayoutCount         = static_cast<uint32>(composite_sets.size()),
            .pSetLayouts            = composite_sets.data(),
            .pushConstantRangeCount = 1,
            .pPushConstantRanges    = &push,
        }),
        "create composite pipeline layout"
    );
}

auto post_process::create_bloom_passes_() -> void {
    const vk::Device device = context_->get_device();

    bloom_down_pass_ = single_color_pass(
        device, bloom_format, vk::AttachmentLoadOp::eDontCare, vk::ImageLayout::eUndefined
    );
    bloom_up_pass_ = single_color_pass(
        device, bloom_format, vk::AttachmentLoadOp::eLoad, vk::ImageLayout::eShaderReadOnlyOptimal
    );
}

auto post_process::create_pipelines_(vk::RenderPass composite_pass) -> void {
    const vk::Device device = context_->get_device();

    bloom_down_pipeline_ = fullscreen_pipeline(device, {
        .pass     = bloom_down_pass_,
        .layout   = blur_layout_,
        .vertex   = fullscreen_vertex_.get_stage_info(),
        .fragment = bloom_down_fragment_.get_stage_info(),
    });

    bloom_up_pipeline_ = fullscreen_pipeline(device, {
        .pass     = bloom_up_pass_,
        .layout   = blur_layout_,
        .vertex   = fullscreen_vertex_.get_stage_info(),
        .fragment = bloom_up_fragment_.get_stage_info(),
        .additive = true,
    });

    composite_pipeline_ = fullscreen_pipeline(device, {
        .pass     = composite_pass,
        .layout   = composite_layout_,
        .vertex   = fullscreen_vertex_.get_stage_info(),
        .fragment = composite_fragment_.get_stage_info(),
    });
}

auto post_process::allocate_sets_() -> void {
    std::array<vk::DescriptorSetLayout, sampled_image_count> layouts{};
    layouts.fill(sampled_layout_);

    const auto sets = vk_must(
        context_->get_device().allocateDescriptorSets({
            .descriptorPool     = descriptor_pool_,
            .descriptorSetCount = sampled_image_count,
            .pSetLayouts        = layouts.data(),
        }),
        "allocate post-process descriptor sets"
    );

    scene_sampled_as_ = sets[0];
    for (uint32 level = 0; level < bloom_level_count; ++level) {
        levels_[level].sampled_as = sets[level + 1];
    }
}

auto post_process::resize(vk::Extent2D extent, vk::ImageView scene_view) -> void {
    scene_extent_ = extent;
    point_set_at_(scene_sampled_as_, scene_view);

    vk::Extent2D level_extent = extent;
    for (auto& level : levels_) {
        level_extent.width  = std::max(level_extent.width / 2, 1U);
        level_extent.height = std::max(level_extent.height / 2, 1U);

        destroy_level_(level);
        create_level_(level, level_extent);
    }
}

auto post_process::create_level_(bloom_level& level, vk::Extent2D extent) -> void {
    const vk::Device device = context_->get_device();

    vk::ImageCreateInfo image_info{};
    image_info.imageType     = vk::ImageType::e2D;
    image_info.extent        = vk::Extent3D{extent.width, extent.height, 1};
    image_info.mipLevels     = 1;
    image_info.arrayLayers   = 1;
    image_info.format        = bloom_format;
    image_info.tiling        = vk::ImageTiling::eOptimal;
    image_info.initialLayout = vk::ImageLayout::eUndefined;
    image_info.usage =
        vk::ImageUsageFlagBits::eColorAttachment | vk::ImageUsageFlagBits::eSampled;
    image_info.samples     = vk::SampleCountFlagBits::e1;
    image_info.sharingMode = vk::SharingMode::eExclusive;

    level.image  = vk_must(device.createImage(image_info), "create bloom image");
    level.extent = extent;

    const vk::MemoryRequirements needs = device.getImageMemoryRequirements(level.image);

    vk::MemoryAllocateInfo alloc_info{};
    alloc_info.allocationSize  = needs.size;
    alloc_info.memoryTypeIndex = memory_type_for(
        context_->get_physical_device(), needs.memoryTypeBits,
        vk::MemoryPropertyFlagBits::eDeviceLocal
    );

    level.memory = vk_must(device.allocateMemory(alloc_info), "allocate bloom image memory");
    vk_must(device.bindImageMemory(level.image, level.memory, 0), "bind bloom image memory");

    vk::ImageViewCreateInfo view_info{};
    view_info.image                       = level.image;
    view_info.viewType                    = vk::ImageViewType::e2D;
    view_info.format                      = bloom_format;
    view_info.subresourceRange.aspectMask = vk::ImageAspectFlagBits::eColor;
    view_info.subresourceRange.levelCount = 1;
    view_info.subresourceRange.layerCount = 1;

    level.view = vk_must(device.createImageView(view_info), "create bloom image view");

    vk::FramebufferCreateInfo framebuffer_info{};
    framebuffer_info.renderPass      = bloom_down_pass_;
    framebuffer_info.attachmentCount = 1;
    framebuffer_info.pAttachments    = &level.view;
    framebuffer_info.width           = extent.width;
    framebuffer_info.height          = extent.height;
    framebuffer_info.layers          = 1;

    level.framebuffer =
        vk_must(device.createFramebuffer(framebuffer_info), "create bloom framebuffer");

    point_set_at_(level.sampled_as, level.view);
}

auto post_process::destroy_level_(bloom_level& level) const -> void {
    const vk::Device device = context_->get_device();

    if (level.framebuffer != nullptr) {
        device.destroyFramebuffer(level.framebuffer);
        level.framebuffer = nullptr;
    }
    if (level.view != nullptr) {
        device.destroyImageView(level.view);
        level.view = nullptr;
    }
    if (level.image != nullptr) {
        device.destroyImage(level.image);
        level.image = nullptr;
    }
    if (level.memory != nullptr) {
        device.freeMemory(level.memory);
        level.memory = nullptr;
    }
}

auto post_process::point_set_at_(vk::DescriptorSet set, vk::ImageView view) const -> void {
    vk::DescriptorImageInfo image_info{};
    image_info.sampler     = sampler_;
    image_info.imageView   = view;
    image_info.imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal;

    vk::WriteDescriptorSet write{};
    write.dstSet          = set;
    write.dstBinding      = 0;
    write.descriptorType  = vk::DescriptorType::eCombinedImageSampler;
    write.descriptorCount = 1;
    write.pImageInfo      = &image_info;

    context_->get_device().updateDescriptorSets(write, nullptr);
}

auto post_process::blur_into_(
    vk::CommandBuffer cmd, const bloom_level& target, vk::DescriptorSet source,
    vk::Extent2D source_extent, bool upward, bool takes_glow_share
) const -> void {
    vk::RenderPassBeginInfo pass_info{};
    pass_info.renderPass        = upward ? bloom_up_pass_ : bloom_down_pass_;
    pass_info.framebuffer       = target.framebuffer;
    pass_info.renderArea.extent = target.extent;

    cmd.beginRenderPass(pass_info, vk::SubpassContents::eInline);
    cover(cmd, target.extent);

    cmd.bindPipeline(
        vk::PipelineBindPoint::eGraphics, upward ? bloom_up_pipeline_ : bloom_down_pipeline_
    );
    cmd.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, blur_layout_, 0, source, nullptr);

    const post_push_constants push{
        .params = vec4f{
            1.0f / static_cast<float32>(source_extent.width),
            1.0f / static_cast<float32>(source_extent.height),
            takes_glow_share ? 1.0f : 0.0f,
            0.0f,
        },
    };
    cmd.pushConstants<post_push_constants>(
        blur_layout_, vk::ShaderStageFlagBits::eFragment, 0, push
    );

    cmd.draw(3, 1, 0, 0);
    cmd.endRenderPass();
}

auto post_process::record_bloom(vk::CommandBuffer cmd) const -> void {
    blur_into_(cmd, levels_[0], scene_sampled_as_, scene_extent_, false, true);

    for (uint32 level = 1; level < bloom_level_count; ++level) {
        const auto& source = levels_[level - 1];
        blur_into_(cmd, levels_[level], source.sampled_as, source.extent, false, false);
    }

    for (uint32 level = bloom_level_count - 1; level > 0; --level) {
        const auto& source = levels_[level];
        blur_into_(cmd, levels_[level - 1], source.sampled_as, source.extent, true, false);
    }
}

auto post_process::draw_composite(
    vk::CommandBuffer cmd, const tonemap_settings& tonemap, const bloom_settings& bloom,
    float32 heat_full_scale
) const -> void {
    const std::array sets{
        scene_sampled_as_,
        bloom.enabled ? levels_[0].sampled_as : scene_sampled_as_,
    };

    cmd.bindPipeline(vk::PipelineBindPoint::eGraphics, composite_pipeline_);
    cmd.bindDescriptorSets(
        vk::PipelineBindPoint::eGraphics, composite_layout_, 0, sets, nullptr
    );

    const post_push_constants push{
        .params = vec4f{
            tonemap.exposure,
            std::max(tonemap.white_point, 0.01f),
            bloom.enabled && heat_full_scale <= 0.0f ? bloom.intensity : 0.0f,
            heat_full_scale,
        },
    };
    cmd.pushConstants<post_push_constants>(
        composite_layout_, vk::ShaderStageFlagBits::eFragment, 0, push
    );

    cmd.draw(3, 1, 0, 0);
}

}  // namespace vw::gfx
