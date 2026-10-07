module vw.gfx;

import std;
import vulkan;
import vw.core;
import :vk;

namespace vw::gfx {

occupancy_view::occupancy_view(
    vulkan_context& context, vk::RenderPass scene_pass, vk::SampleCountFlagBits samples,
    vk::DescriptorSetLayout occupancy_layout
)
    : context_{&context}
    , vertex_{context, "shaders/fullscreen.vert.spv", shader_type::VERTEX}
    , fragment_{context, "shaders/occupancy_view.frag.spv", shader_type::FRAGMENT} {
    const vk::Device device = context_->get_device();

    vk::PushConstantRange push{};
    push.offset     = 0;
    push.size       = sizeof(occupancy_view_push);
    push.stageFlags = vk::ShaderStageFlagBits::eFragment;

    layout_ = vk_must(
        device.createPipelineLayout({
            .setLayoutCount         = 1,
            .pSetLayouts            = &occupancy_layout,
            .pushConstantRangeCount = 1,
            .pPushConstantRanges    = &push,
        }),
        "create occupancy view layout"
    );

    const std::array stages{vertex_.get_stage_info(), fragment_.get_stage_info()};

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
    multisampling.rasterizationSamples = samples;

    vk::PipelineColorBlendAttachmentState blend{};
    blend.colorWriteMask = vk::ColorComponentFlagBits::eR | vk::ColorComponentFlagBits::eG |
                           vk::ColorComponentFlagBits::eB | vk::ColorComponentFlagBits::eA;
    blend.blendEnable = vk::False;

    vk::PipelineColorBlendStateCreateInfo color_blending{};
    color_blending.attachmentCount = 1;
    color_blending.pAttachments    = &blend;

    vk::PipelineDepthStencilStateCreateInfo depth_stencil{};
    depth_stencil.depthTestEnable  = vk::False;
    depth_stencil.depthWriteEnable = vk::False;

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
    info.layout              = layout_;
    info.renderPass          = scene_pass;
    info.subpass             = 0;

    pipeline_ =
        vk_must(device.createGraphicsPipeline(nullptr, info), "create occupancy view pipeline");
}

occupancy_view::~occupancy_view() {
    const vk::Device device = context_->get_device();
    device.destroyPipeline(pipeline_);
    device.destroyPipelineLayout(layout_);
}

auto occupancy_view::draw(
    vk::CommandBuffer cmd, const occupancy_clipmap& occupancy, uint32 frame,
    const camera& camera, vec4f tonemap
) const -> void {
    constexpr int32 chunk_voxels = spatial::occupancy_clipmap_layout::chunk_voxels;

    const float32 units    = occupancy.world_units_per_voxel();
    const vec3i base_chunk = occupancy.centre_chunk();
    const vec3f eye        = camera.get_position();

    const auto corners = camera.frustum_corners(0.5f, 1.0f);

    occupancy_view_push push{};
    push.eye = vec4f{
        (eye.x / units) - static_cast<float32>(base_chunk.x * chunk_voxels),
        (eye.y / units) - static_cast<float32>(base_chunk.y * chunk_voxels),
        (eye.z / units) - static_cast<float32>(base_chunk.z * chunk_voxels),
        reach_voxels,
    };
    for (std::size_t corner = 0; corner < push.corners.size(); ++corner) {
        const vec3f& far_corner = corners[corner + 4];
        push.corners[corner]    =
            vec4f{far_corner.x - eye.x, far_corner.y - eye.y, far_corner.z - eye.z, 0.0f};
    }
    push.tonemap    = tonemap;
    push.base_chunk = vec4<int32>{base_chunk.x, base_chunk.y, base_chunk.z, 0};

    cmd.bindPipeline(vk::PipelineBindPoint::eGraphics, pipeline_);
    cmd.bindDescriptorSets(
        vk::PipelineBindPoint::eGraphics, layout_, 0, occupancy.get_descriptor_set(frame), nullptr
    );
    cmd.pushConstants<occupancy_view_push>(
        layout_, vk::ShaderStageFlagBits::eFragment, 0, push
    );
    cmd.draw(3, 1, 0, 0);
}

}  // namespace vw::gfx
