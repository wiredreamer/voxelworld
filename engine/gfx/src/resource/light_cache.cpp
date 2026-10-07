module vw.gfx;

import std;
import vulkan;
import vw.core;
import :vk;

namespace vw::gfx {

namespace {

constexpr int32 chunk_voxels = spatial::occupancy_clipmap_layout::chunk_voxels;
constexpr int32 brick_shift  = 3;

constexpr int32 near_margin_cells   = 1;
constexpr int32 shade_spread_voxels = 8;
constexpr int32 shade_depth_voxels  = 32;

static_assert((1 << brick_shift) == light_cache::brick_texels);

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

    throw std::runtime_error("no memory type fits a light cascade");
}

auto whole_image() -> vk::ImageSubresourceRange {
    vk::ImageSubresourceRange range{};
    range.aspectMask = vk::ImageAspectFlagBits::eColor;
    range.levelCount = 1;
    range.layerCount = 1;
    return range;
}

auto window_origin(vec3i centre_voxel, int32 cascade) -> vec3i {
    const int32 shift = brick_shift + cascade;
    const int32 half  = 1 << (shift - 1);
    const int32 back  = light_cache::bricks_per_side / 2;

    return {
        ((centre_voxel.x + half) >> shift) - back, ((centre_voxel.y + half) >> shift) - back,
        ((centre_voxel.z + half) >> shift) - back
    };
}

}  // namespace

light_cache::light_cache(
    vulkan_context& context, vk::DescriptorPool descriptor_pool,
    vk::DescriptorSetLayout occupancy_layout
)
    : context_{&context}
    , descriptor_pool_{descriptor_pool}
    , compute_{context, "shaders/light_cache.comp.spv", shader_type::COMPUTE} {
    for (auto& cascade : cascades_) {
        cascade.slots.resize(static_cast<std::size_t>(slot_count));
    }
    for (auto& frame : frames_) {
        frame.queue = std::make_unique<storage_buffer>(
            context, vk::DeviceSize{most_bricks_a_frame} * sizeof(vec4<int32>)
        );
    }
    chosen_.reserve(most_bricks_a_frame);

    create_images_();
    create_pipeline_(occupancy_layout);
    create_sets_();
}

light_cache::~light_cache() {
    const vk::Device device = context_->get_device();

    for (const auto& frame : frames_) {
        static_cast<void>(device.freeDescriptorSets(descriptor_pool_, frame.set));
    }
    static_cast<void>(device.freeDescriptorSets(descriptor_pool_, sampled_set_));

    device.destroyPipeline(pipeline_);
    device.destroyPipelineLayout(pipeline_layout_);
    device.destroyDescriptorSetLayout(written_layout_);
    device.destroyDescriptorSetLayout(sampled_layout_);
    device.destroySampler(sampler_);

    for (auto& cascade : cascades_) {
        device.destroyImageView(cascade.view);
        device.destroyImage(cascade.image);
        device.freeMemory(cascade.memory);
    }
}

auto light_cache::create_images_() -> void {
    const vk::Device device = context_->get_device();

    for (auto& cascade : cascades_) {
        const auto side = static_cast<uint32>(texture_side);

        vk::ImageCreateInfo image_info{};
        image_info.imageType     = vk::ImageType::e3D;
        image_info.extent        = vk::Extent3D{side, side, side};
        image_info.mipLevels     = 1;
        image_info.arrayLayers   = 1;
        image_info.format        = format;
        image_info.tiling        = vk::ImageTiling::eOptimal;
        image_info.initialLayout = vk::ImageLayout::eUndefined;
        image_info.usage = vk::ImageUsageFlagBits::eStorage | vk::ImageUsageFlagBits::eSampled |
                           vk::ImageUsageFlagBits::eTransferDst;
        image_info.samples     = vk::SampleCountFlagBits::e1;
        image_info.sharingMode = vk::SharingMode::eExclusive;

        cascade.image = vk_must(device.createImage(image_info), "create light cascade");

        const vk::MemoryRequirements needs = device.getImageMemoryRequirements(cascade.image);

        vk::MemoryAllocateInfo alloc_info{};
        alloc_info.allocationSize  = needs.size;
        alloc_info.memoryTypeIndex = memory_type_for(
            context_->get_physical_device(), needs.memoryTypeBits,
            vk::MemoryPropertyFlagBits::eDeviceLocal
        );

        cascade.memory = vk_must(device.allocateMemory(alloc_info), "allocate light cascade");
        vk_must(device.bindImageMemory(cascade.image, cascade.memory, 0), "bind light cascade");

        vk::ImageViewCreateInfo view_info{};
        view_info.image            = cascade.image;
        view_info.viewType         = vk::ImageViewType::e3D;
        view_info.format           = format;
        view_info.subresourceRange = whole_image();

        cascade.view = vk_must(device.createImageView(view_info), "create light cascade view");
    }

    vk::SamplerCreateInfo sampler_info{};
    sampler_info.magFilter     = vk::Filter::eLinear;
    sampler_info.minFilter     = vk::Filter::eLinear;
    sampler_info.mipmapMode    = vk::SamplerMipmapMode::eNearest;
    sampler_info.addressModeU  = vk::SamplerAddressMode::eRepeat;
    sampler_info.addressModeV  = vk::SamplerAddressMode::eRepeat;
    sampler_info.addressModeW  = vk::SamplerAddressMode::eRepeat;
    sampler_info.maxAnisotropy = 1.0f;

    sampler_ = vk_must(device.createSampler(sampler_info), "create light cache sampler");
}

auto light_cache::create_pipeline_(vk::DescriptorSetLayout occupancy_layout) -> void {
    const vk::Device device = context_->get_device();

    vk::DescriptorSetLayoutBinding sampled{};
    sampled.binding         = 0;
    sampled.descriptorType  = vk::DescriptorType::eCombinedImageSampler;
    sampled.descriptorCount = static_cast<uint32>(cascade_count);
    sampled.stageFlags      = vk::ShaderStageFlagBits::eFragment;

    sampled_layout_ = vk_must(
        device.createDescriptorSetLayout({.bindingCount = 1, .pBindings = &sampled}),
        "create light cache sampled layout"
    );

    std::array<vk::DescriptorSetLayoutBinding, cascade_count + 1> written{};
    for (uint32 cascade = 0; cascade < cascade_count; ++cascade) {
        written[cascade].binding         = cascade;
        written[cascade].descriptorType  = vk::DescriptorType::eStorageImage;
        written[cascade].descriptorCount = 1;
        written[cascade].stageFlags      = vk::ShaderStageFlagBits::eCompute;
    }
    written[cascade_count].binding         = cascade_count;
    written[cascade_count].descriptorType  = vk::DescriptorType::eStorageBuffer;
    written[cascade_count].descriptorCount = 1;
    written[cascade_count].stageFlags      = vk::ShaderStageFlagBits::eCompute;

    written_layout_ = vk_must(
        device.createDescriptorSetLayout({
            .bindingCount = static_cast<uint32>(written.size()),
            .pBindings    = written.data(),
        }),
        "create light cache written layout"
    );

    const std::array set_layouts{occupancy_layout, written_layout_};

    const vk::PushConstantRange push{
        .stageFlags = vk::ShaderStageFlagBits::eCompute,
        .offset     = 0,
        .size       = sizeof(light_cache_push),
    };

    pipeline_layout_ = vk_must(
        device.createPipelineLayout({
            .setLayoutCount         = static_cast<uint32>(set_layouts.size()),
            .pSetLayouts            = set_layouts.data(),
            .pushConstantRangeCount = 1,
            .pPushConstantRanges    = &push,
        }),
        "create light cache pipeline layout"
    );

    pipeline_ = vk_must(
        device.createComputePipeline(
            nullptr, {.stage = compute_.get_stage_info(), .layout = pipeline_layout_}
        ),
        "create light cache pipeline"
    );
}

auto light_cache::create_sets_() -> void {
    const vk::Device device = context_->get_device();

    sampled_set_ = vk_must(
        device.allocateDescriptorSets({
            .descriptorPool     = descriptor_pool_,
            .descriptorSetCount = 1,
            .pSetLayouts        = &sampled_layout_,
        }),
        "allocate light cache sampled set"
    )[0];

    std::array<vk::DescriptorImageInfo, cascade_count> sampled{};
    std::array<vk::DescriptorImageInfo, cascade_count> written{};
    for (std::size_t cascade = 0; cascade < sampled.size(); ++cascade) {
        sampled[cascade].sampler     = sampler_;
        sampled[cascade].imageView   = cascades_[cascade].view;
        sampled[cascade].imageLayout = vk::ImageLayout::eGeneral;

        written[cascade].imageView   = cascades_[cascade].view;
        written[cascade].imageLayout = vk::ImageLayout::eGeneral;
    }

    vk::WriteDescriptorSet sampled_write{};
    sampled_write.dstSet          = sampled_set_;
    sampled_write.dstBinding      = 0;
    sampled_write.descriptorType  = vk::DescriptorType::eCombinedImageSampler;
    sampled_write.descriptorCount = static_cast<uint32>(sampled.size());
    sampled_write.pImageInfo      = sampled.data();
    device.updateDescriptorSets(sampled_write, nullptr);

    std::array<vk::DescriptorSetLayout, frames_in_flight> layouts{};
    layouts.fill(written_layout_);

    const auto sets = vk_must(
        device.allocateDescriptorSets({
            .descriptorPool     = descriptor_pool_,
            .descriptorSetCount = frames_in_flight,
            .pSetLayouts        = layouts.data(),
        }),
        "allocate light cache compute sets"
    );

    for (uint32 frame = 0; frame < frames_in_flight; ++frame) {
        frames_[frame].set = sets[frame];

        vk::DescriptorBufferInfo queue_info{};
        queue_info.buffer = frames_[frame].queue->get_buffer();
        queue_info.offset = 0;
        queue_info.range  = vk::DeviceSize{most_bricks_a_frame} * sizeof(vec4<int32>);

        std::array<vk::WriteDescriptorSet, cascade_count + 1> writes{};
        for (uint32 cascade = 0; cascade < cascade_count; ++cascade) {
            writes[cascade].dstSet          = sets[frame];
            writes[cascade].dstBinding      = cascade;
            writes[cascade].descriptorType  = vk::DescriptorType::eStorageImage;
            writes[cascade].descriptorCount = 1;
            writes[cascade].pImageInfo      = &written[cascade];
        }
        writes[cascade_count].dstSet          = sets[frame];
        writes[cascade_count].dstBinding      = cascade_count;
        writes[cascade_count].descriptorType  = vk::DescriptorType::eStorageBuffer;
        writes[cascade_count].descriptorCount = 1;
        writes[cascade_count].pBufferInfo     = &queue_info;

        device.updateDescriptorSets(writes, nullptr);
    }
}

auto light_cache::make_ready(vk::CommandBuffer cmd) -> void {
    if (ready_) {
        return;
    }
    ready_ = true;

    for (const auto& cascade : cascades_) {
        vk::ImageMemoryBarrier to_general{};
        to_general.srcAccessMask       = {};
        to_general.dstAccessMask       = vk::AccessFlagBits::eTransferWrite;
        to_general.oldLayout           = vk::ImageLayout::eUndefined;
        to_general.newLayout           = vk::ImageLayout::eGeneral;
        to_general.srcQueueFamilyIndex = vk::QueueFamilyIgnored;
        to_general.dstQueueFamilyIndex = vk::QueueFamilyIgnored;
        to_general.image               = cascade.image;
        to_general.subresourceRange    = whole_image();

        cmd.pipelineBarrier(
            vk::PipelineStageFlagBits::eTopOfPipe, vk::PipelineStageFlagBits::eTransfer, {},
            nullptr, nullptr, to_general
        );

        const vk::ClearColorValue open_sky{std::array<float32, 4>{0.0f, 0.0f, 0.0f, 1.0f}};
        cmd.clearColorImage(cascade.image, vk::ImageLayout::eGeneral, open_sky, whole_image());
    }

    cmd.pipelineBarrier(
        vk::PipelineStageFlagBits::eTransfer,
        vk::PipelineStageFlagBits::eComputeShader | vk::PipelineStageFlagBits::eFragmentShader,
        {},
        vk::MemoryBarrier{
            .srcAccessMask = vk::AccessFlagBits::eTransferWrite,
            .dstAccessMask = vk::AccessFlagBits::eShaderRead | vk::AccessFlagBits::eShaderWrite,
        },
        nullptr, nullptr
    );
}

auto light_cache::slot_index_(vec3i brick) -> std::size_t {
    constexpr int32 mask = bricks_per_side - 1;
    return static_cast<std::size_t>(
        (brick.x & mask) + (bricks_per_side * ((brick.y & mask) + (bricks_per_side * (brick.z & mask))))
    );
}

auto light_cache::move_window_(int32 cascade, vec3i origin) -> void {
    auto& state  = cascades_[static_cast<std::size_t>(cascade)];
    state.origin = origin;
    state.placed = true;

    for (int32 z = 0; z < bricks_per_side; ++z) {
        for (int32 y = 0; y < bricks_per_side; ++y) {
            for (int32 x = 0; x < bricks_per_side; ++x) {
                const vec3i brick{origin.x + x, origin.y + y, origin.z + z};

                slot& held = state.slots[slot_index_(brick)];
                if (held.assigned && held.brick == brick) {
                    continue;
                }
                held.brick    = brick;
                held.assigned = true;
                held.waiting  = true;
            }
        }
    }
}

auto light_cache::mark_box_(vec3i low_voxel, vec3i high_voxel, int32 margin_cells) -> void {
    for (int32 cascade = 0; cascade < cascade_count; ++cascade) {
        auto& state = cascades_[static_cast<std::size_t>(cascade)];

        const int32 shift  = brick_shift + cascade;
        const int32 margin = margin_cells << cascade;
        const vec3i low{
            (low_voxel.x - margin) >> shift, (low_voxel.y - margin) >> shift,
            (low_voxel.z - margin) >> shift
        };
        const vec3i high{
            (high_voxel.x + margin) >> shift, (high_voxel.y + margin) >> shift,
            (high_voxel.z + margin) >> shift
        };

        for (int32 z = std::max(low.z, state.origin.z);
             z <= std::min(high.z, state.origin.z + bricks_per_side - 1); ++z) {
            for (int32 y = std::max(low.y, state.origin.y);
                 y <= std::min(high.y, state.origin.y + bricks_per_side - 1); ++y) {
                for (int32 x = std::max(low.x, state.origin.x);
                     x <= std::min(high.x, state.origin.x + bricks_per_side - 1); ++x) {
                    state.slots[slot_index_({x, y, z})].waiting = true;
                }
            }
        }
    }
}

auto light_cache::mark_changed_(const ecs::occupancy_change& change) -> void {
    if (!change.whole_chunk) {
        mark_box_(change.voxel, change.voxel, near_margin_cells);
        return;
    }

    const vec3i low{
        change.chunk.x * chunk_voxels, change.chunk.y * chunk_voxels,
        change.chunk.z * chunk_voxels
    };
    const vec3i high{low.x + chunk_voxels - 1, low.y + chunk_voxels - 1, low.z + chunk_voxels - 1};

    mark_box_(
        {low.x - shade_spread_voxels, low.y - shade_depth_voxels, low.z - shade_spread_voxels},
        {high.x + shade_spread_voxels, high.y, high.z + shade_spread_voxels}, near_margin_cells
    );
}

auto light_cache::choose_bricks(
    vec3i centre_voxel, std::span<const ecs::occupancy_change> changes,
    const light_cache_settings& settings, uint32 frame
) -> void {
    for (int32 cascade = 0; cascade < cascade_count; ++cascade) {
        const vec3i origin = window_origin(centre_voxel, cascade);

        const auto& state = cascades_[static_cast<std::size_t>(cascade)];
        if (!state.placed || origin != state.origin) {
            move_window_(cascade, origin);
        }
    }

    for (const auto& change : changes) {
        mark_changed_(change);
    }

    const uint32 budget = std::min(settings.bricks_per_frame, most_bricks_a_frame);

    chosen_.clear();
    uint32 waiting = 0;

    for (int32 cascade = cascade_count - 1; cascade >= 0; --cascade) {
        auto& state = cascades_[static_cast<std::size_t>(cascade)];
        for (slot& held : state.slots) {
            if (!held.waiting) {
                continue;
            }
            if (chosen_.size() >= budget) {
                ++waiting;
                continue;
            }
            held.waiting = false;
            chosen_.push_back({held.brick.x, held.brick.y, held.brick.z, cascade});
        }
    }

    auto& current  = frames_[frame];
    current.bricks = static_cast<uint32>(chosen_.size());
    if (!chosen_.empty()) {
        current.queue->copy_from_vector(chosen_);
    }

    stats_.bricks_frame = current.bricks;
    stats_.bricks_total += current.bricks;
    stats_.waiting      = waiting;
    stats_.peak_waiting = std::max(stats_.peak_waiting, waiting);
}

auto light_cache::dispatch(
    vk::CommandBuffer cmd, vk::DescriptorSet occupancy_set, vec3i base_chunk,
    const light_cache_settings& settings, uint32 frame
) const -> void {
    const auto& current = frames_[frame];
    if (current.bricks == 0) {
        return;
    }

    const std::array sets{occupancy_set, current.set};

    cmd.bindPipeline(vk::PipelineBindPoint::eCompute, pipeline_);
    cmd.bindDescriptorSets(vk::PipelineBindPoint::eCompute, pipeline_layout_, 0, sets, nullptr);

    const vk::MemoryBarrier between_phases{
        .srcAccessMask = vk::AccessFlagBits::eShaderWrite,
        .dstAccessMask = vk::AccessFlagBits::eShaderRead | vk::AccessFlagBits::eShaderWrite,
    };

    for (int32 phase = 0; phase < 2; ++phase) {
        const light_cache_push push{
            .sky        = {settings.sky_reach_cells, 0.0f, 0.0f, 0.0f},
            .base_chunk = {base_chunk.x, base_chunk.y, base_chunk.z, phase},
        };
        cmd.pushConstants<light_cache_push>(
            pipeline_layout_, vk::ShaderStageFlagBits::eCompute, 0, push
        );
        cmd.dispatch(current.bricks, 1, 1);

        cmd.pipelineBarrier(
            vk::PipelineStageFlagBits::eComputeShader,
            phase == 0 ? vk::PipelineStageFlags{vk::PipelineStageFlagBits::eComputeShader}
                       : vk::PipelineStageFlags{vk::PipelineStageFlagBits::eFragmentShader},
            {}, between_phases, nullptr, nullptr
        );
    }
}

auto light_cache::wrap_of(int32 cascade, vec3i base_chunk) -> vec4f {
    const int32 span = texture_side << cascade;

    const auto along = [&](int32 chunk) -> float32 {
        const int32 voxel = chunk * chunk_voxels;
        return static_cast<float32>(((voxel % span) + span) % span) / static_cast<float32>(span);
    };

    return {along(base_chunk.x), along(base_chunk.y), along(base_chunk.z), 0.0f};
}

}  // namespace vw::gfx
