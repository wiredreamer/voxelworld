module vw.gfx;

import std;
import vulkan;
import vw.core;
import vw.asset;
import vw.ecs;
import vw.world;
import :vk;

namespace vw::gfx {

namespace {

constexpr int32 chunk_voxels = spatial::occupancy_clipmap_layout::chunk_voxels;
constexpr int32 brick_shift  = 3;

constexpr int32 flood_reach_voxels = 16;
constexpr int32 shaft_depth_voxels = 64;
constexpr int32 seeds_from_sky     = 8;
constexpr int32 last_pass          = 16;
constexpr int32 wipes_sources      = 32;
constexpr int32 near_sources       = 64;
constexpr int32 source_level_shift = 8;
constexpr int32 source_tint_shift  = 12;

// см. docs/lighting.md#цвет-света-ламп
auto packed_tint_of(color clr) -> int32 {
    const vec3f shown       = palette_color_of(clr);
    const float32 brightest = std::max({shown.x, shown.y, shown.z});
    if (brightest <= 0.0f) {
        return 0xFF;
    }

    const auto channel = [&](float32 value, int32 steps) -> int32 {
        return static_cast<int32>(std::lround((value / brightest) * static_cast<float32>(steps)));
    };
    return (channel(shown.x, 7) << 5) | (channel(shown.y, 7) << 2) | channel(shown.z, 3);
}

auto chunk_of(vec3i voxel) -> vec3i {
    return {voxel.x >> 6, voxel.y >> 6, voxel.z >> 6};
}

auto page_key_of(const asset::emitting_voxel& at) -> int32 {
    return ((at.z >> 3) << 6) | ((at.y >> 3) << 3) | (at.x >> 3);
}

static_assert(chunk_voxels == 64);

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
    const auto& shape = light_cache::shapes[static_cast<std::size_t>(cascade)];

    const int32 shift = brick_shift + shape.cell_shift;
    const int32 half  = 1 << (shift - 1);
    const int32 back  = shape.bricks_per_side() / 2;

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
    , compute_{context, "shaders/light_cache.comp.spv", shader_type::COMPUTE}
    , scatter_{context, "shaders/light_sources.comp.spv", shader_type::COMPUTE}
    , emission_{default_material_table().emission()} {
    for (std::size_t cascade = 0; cascade < cascades_.size(); ++cascade) {
        const auto slots = static_cast<std::size_t>(shapes[cascade].slot_count());

        cascades_[cascade].slots.resize(slots);
        cascades_[cascade].owing[0] = static_cast<uint32>(slots);
    }
    for (auto& frame : frames_) {
        frame.queue = std::make_unique<storage_buffer>(
            context, vk::DeviceSize{most_bricks_a_frame} * sizeof(vec4<int32>)
        );
        frame.sources = std::make_unique<storage_buffer>(
            context, vk::DeviceSize{most_sources_a_frame} * sizeof(vec4<int32>)
        );
    }
    chosen_.reserve(most_bricks_a_frame);
    source_entries_.reserve(most_sources_a_frame);

    for (const voxel_type& type : default_voxel_registry().all()) {
        tint_of_voxel_[type.id.value] = packed_tint_of(type.material.clr);
    }

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

    device.destroyPipeline(scatter_pipeline_);
    device.destroyPipeline(pipeline_);
    device.destroyPipelineLayout(pipeline_layout_);
    device.destroyDescriptorSetLayout(written_layout_);
    device.destroyDescriptorSetLayout(sampled_layout_);
    device.destroySampler(sampler_);

    for (auto& cascade : cascades_) {
        device.destroyImageView(cascade.tint_view);
        device.destroyImage(cascade.tint_image);
        device.freeMemory(cascade.tint_memory);

        device.destroyImageView(cascade.view);
        device.destroyImage(cascade.image);
        device.freeMemory(cascade.memory);
    }
}

auto light_cache::create_images_() -> void {
    const vk::Device device = context_->get_device();

    const auto make = [&](uint32 side, vk::Image& image, vk::DeviceMemory& memory, vk::ImageView& view) {
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

        image = vk_must(device.createImage(image_info), "create light cascade");

        const vk::MemoryRequirements needs = device.getImageMemoryRequirements(image);

        vk::MemoryAllocateInfo alloc_info{};
        alloc_info.allocationSize  = needs.size;
        alloc_info.memoryTypeIndex = memory_type_for(
            context_->get_physical_device(), needs.memoryTypeBits,
            vk::MemoryPropertyFlagBits::eDeviceLocal
        );

        memory = vk_must(device.allocateMemory(alloc_info), "allocate light cascade");
        vk_must(device.bindImageMemory(image, memory, 0), "bind light cascade");

        vk::ImageViewCreateInfo view_info{};
        view_info.image            = image;
        view_info.viewType         = vk::ImageViewType::e3D;
        view_info.format           = format;
        view_info.subresourceRange = whole_image();

        view = vk_must(device.createImageView(view_info), "create light cascade view");
    };

    for (std::size_t index = 0; index < cascades_.size(); ++index) {
        auto& cascade   = cascades_[index];
        const auto side = static_cast<uint32>(shapes[index].texture_side);

        make(side, cascade.image, cascade.memory, cascade.view);
        make(side >> tint_shift, cascade.tint_image, cascade.tint_memory, cascade.tint_view);
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

    std::array<vk::DescriptorSetLayoutBinding, 2> sampled{};
    for (uint32 kind = 0; kind < sampled.size(); ++kind) {
        sampled[kind].binding         = kind;
        sampled[kind].descriptorType  = vk::DescriptorType::eCombinedImageSampler;
        sampled[kind].descriptorCount = static_cast<uint32>(cascade_count);
        sampled[kind].stageFlags      = vk::ShaderStageFlagBits::eFragment;
    }

    sampled_layout_ = vk_must(
        device.createDescriptorSetLayout({
            .bindingCount = static_cast<uint32>(sampled.size()),
            .pBindings    = sampled.data(),
        }),
        "create light cache sampled layout"
    );

    std::array<vk::DescriptorSetLayoutBinding, (cascade_count * 2) + 2> written{};
    for (uint32 cascade = 0; cascade < cascade_count; ++cascade) {
        for (const uint32 binding : {cascade, tint_binding + cascade}) {
            written[binding].binding         = binding;
            written[binding].descriptorType  = vk::DescriptorType::eStorageImage;
            written[binding].descriptorCount = 1;
            written[binding].stageFlags      = vk::ShaderStageFlagBits::eCompute;
        }
    }
    for (uint32 list = cascade_count; list < cascade_count + 2; ++list) {
        written[list].binding         = list;
        written[list].descriptorType  = vk::DescriptorType::eStorageBuffer;
        written[list].descriptorCount = 1;
        written[list].stageFlags      = vk::ShaderStageFlagBits::eCompute;
    }

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

    scatter_pipeline_ = vk_must(
        device.createComputePipeline(
            nullptr, {.stage = scatter_.get_stage_info(), .layout = pipeline_layout_}
        ),
        "create light source pipeline"
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
    std::array<vk::DescriptorImageInfo, cascade_count> sampled_tints{};
    std::array<vk::DescriptorImageInfo, cascade_count> written{};
    std::array<vk::DescriptorImageInfo, cascade_count> written_tints{};
    for (std::size_t cascade = 0; cascade < sampled.size(); ++cascade) {
        sampled[cascade].sampler     = sampler_;
        sampled[cascade].imageView   = cascades_[cascade].view;
        sampled[cascade].imageLayout = vk::ImageLayout::eGeneral;

        sampled_tints[cascade].sampler     = sampler_;
        sampled_tints[cascade].imageView   = cascades_[cascade].tint_view;
        sampled_tints[cascade].imageLayout = vk::ImageLayout::eGeneral;

        written[cascade].imageView   = cascades_[cascade].view;
        written[cascade].imageLayout = vk::ImageLayout::eGeneral;

        written_tints[cascade].imageView   = cascades_[cascade].tint_view;
        written_tints[cascade].imageLayout = vk::ImageLayout::eGeneral;
    }

    std::array<vk::WriteDescriptorSet, 2> sampled_writes{};
    sampled_writes[0].dstSet          = sampled_set_;
    sampled_writes[0].dstBinding      = 0;
    sampled_writes[0].descriptorType  = vk::DescriptorType::eCombinedImageSampler;
    sampled_writes[0].descriptorCount = static_cast<uint32>(sampled.size());
    sampled_writes[0].pImageInfo      = sampled.data();

    sampled_writes[1].dstSet          = sampled_set_;
    sampled_writes[1].dstBinding      = 1;
    sampled_writes[1].descriptorType  = vk::DescriptorType::eCombinedImageSampler;
    sampled_writes[1].descriptorCount = static_cast<uint32>(sampled_tints.size());
    sampled_writes[1].pImageInfo      = sampled_tints.data();
    device.updateDescriptorSets(sampled_writes, nullptr);

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

        vk::DescriptorBufferInfo sources_info{};
        sources_info.buffer = frames_[frame].sources->get_buffer();
        sources_info.offset = 0;
        sources_info.range  = vk::DeviceSize{most_sources_a_frame} * sizeof(vec4<int32>);

        std::array<vk::WriteDescriptorSet, (cascade_count * 2) + 2> writes{};
        for (uint32 cascade = 0; cascade < cascade_count; ++cascade) {
            writes[cascade].dstSet          = sets[frame];
            writes[cascade].dstBinding      = cascade;
            writes[cascade].descriptorType  = vk::DescriptorType::eStorageImage;
            writes[cascade].descriptorCount = 1;
            writes[cascade].pImageInfo      = &written[cascade];

            auto& tint           = writes[tint_binding + cascade];
            tint.dstSet          = sets[frame];
            tint.dstBinding      = tint_binding + cascade;
            tint.descriptorType  = vk::DescriptorType::eStorageImage;
            tint.descriptorCount = 1;
            tint.pImageInfo      = &written_tints[cascade];
        }
        writes[cascade_count].dstSet          = sets[frame];
        writes[cascade_count].dstBinding      = cascade_count;
        writes[cascade_count].descriptorType  = vk::DescriptorType::eStorageBuffer;
        writes[cascade_count].descriptorCount = 1;
        writes[cascade_count].pBufferInfo     = &queue_info;

        writes[cascade_count + 1].dstSet          = sets[frame];
        writes[cascade_count + 1].dstBinding      = cascade_count + 1;
        writes[cascade_count + 1].descriptorType  = vk::DescriptorType::eStorageBuffer;
        writes[cascade_count + 1].descriptorCount = 1;
        writes[cascade_count + 1].pBufferInfo     = &sources_info;

        device.updateDescriptorSets(writes, nullptr);
    }
}

auto light_cache::make_ready(vk::CommandBuffer cmd) -> void {
    if (ready_) {
        return;
    }
    ready_ = true;

    const auto start = [&](vk::Image image, const std::array<float32, 4>& filled) {
        vk::ImageMemoryBarrier to_general{};
        to_general.srcAccessMask       = {};
        to_general.dstAccessMask       = vk::AccessFlagBits::eTransferWrite;
        to_general.oldLayout           = vk::ImageLayout::eUndefined;
        to_general.newLayout           = vk::ImageLayout::eGeneral;
        to_general.srcQueueFamilyIndex = vk::QueueFamilyIgnored;
        to_general.dstQueueFamilyIndex = vk::QueueFamilyIgnored;
        to_general.image               = image;
        to_general.subresourceRange    = whole_image();

        cmd.pipelineBarrier(
            vk::PipelineStageFlagBits::eTopOfPipe, vk::PipelineStageFlagBits::eTransfer, {},
            nullptr, nullptr, to_general
        );

        cmd.clearColorImage(
            image, vk::ImageLayout::eGeneral, vk::ClearColorValue{filled}, whole_image()
        );
    };

    for (const auto& cascade : cascades_) {
        start(cascade.image, {0.0f, 0.0f, 0.0f, 1.0f});
        start(cascade.tint_image, {0.0f, 0.0f, 0.0f, 0.0f});
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

auto light_cache::slot_index_(int32 cascade, vec3i brick) -> std::size_t {
    const int32 side = shapes[static_cast<std::size_t>(cascade)].bricks_per_side();
    const int32 mask = side - 1;

    return static_cast<std::size_t>(
        (brick.x & mask) + (side * ((brick.y & mask) + (side * (brick.z & mask))))
    );
}

auto light_cache::arm_(int32 cascade, slot& held) -> void {
    auto& state = cascades_[static_cast<std::size_t>(cascade)];

    // см. docs/lighting.md#когда-пересчитывается
    if (cascade > 0 && held.passes_left > 0 && !held.fresh) {
        held.armed_again = true;
        return;
    }

    --state.owing[held.passes_left];
    held.passes_left = shapes[static_cast<std::size_t>(cascade)].passes_to_settle;
    ++state.owing[held.passes_left];
}

auto light_cache::move_window_(int32 cascade, vec3i origin) -> void {
    auto& state  = cascades_[static_cast<std::size_t>(cascade)];
    state.origin = origin;
    state.placed = true;

    const int32 side = shapes[static_cast<std::size_t>(cascade)].bricks_per_side();

    for (int32 z = 0; z < side; ++z) {
        for (int32 y = 0; y < side; ++y) {
            for (int32 x = 0; x < side; ++x) {
                const vec3i brick{origin.x + x, origin.y + y, origin.z + z};

                slot& held = state.slots[slot_index_(cascade, brick)];
                if (held.assigned && held.brick == brick) {
                    continue;
                }
                held.brick       = brick;
                held.assigned    = true;
                held.fresh       = true;
                held.armed_again = false;
                arm_(cascade, held);
            }
        }
    }
}

auto light_cache::mark_box_(vec3i low_voxel, vec3i high_voxel) -> void {
    for (int32 cascade = 0; cascade < cascade_count; ++cascade) {
        auto& state = cascades_[static_cast<std::size_t>(cascade)];
        if (!state.placed) {
            continue;
        }

        const auto& shape = shapes[static_cast<std::size_t>(cascade)];
        const int32 shift = brick_shift + shape.cell_shift;
        const int32 last  = shape.bricks_per_side() - 1;

        const vec3i low{low_voxel.x >> shift, low_voxel.y >> shift, low_voxel.z >> shift};
        const vec3i high{high_voxel.x >> shift, high_voxel.y >> shift, high_voxel.z >> shift};

        for (int32 z = std::max(low.z, state.origin.z); z <= std::min(high.z, state.origin.z + last);
             ++z) {
            for (int32 y = std::max(low.y, state.origin.y);
                 y <= std::min(high.y, state.origin.y + last); ++y) {
                for (int32 x = std::max(low.x, state.origin.x);
                     x <= std::min(high.x, state.origin.x + last); ++x) {
                    arm_(cascade, state.slots[slot_index_(cascade, {x, y, z})]);
                }
            }
        }
    }
}

auto light_cache::mark_changed_(const ecs::occupancy_change& change) -> void {
    vec3i low  = change.voxel;
    vec3i high = change.voxel;
    if (change.whole_chunk) {
        low = {
            change.chunk.x * chunk_voxels, change.chunk.y * chunk_voxels,
            change.chunk.z * chunk_voxels
        };
        high = {low.x + chunk_voxels - 1, low.y + chunk_voxels - 1, low.z + chunk_voxels - 1};
    }

    mark_box_(
        {
            low.x - flood_reach_voxels, low.y - shaft_depth_voxels - flood_reach_voxels,
            low.z - flood_reach_voxels
        },
        {high.x + flood_reach_voxels, high.y + flood_reach_voxels, high.z + flood_reach_voxels}
    );
}

auto light_cache::touch_sources_at_(vec3i voxel) -> void {
    for (int32 cascade = 0; cascade < cascade_count; ++cascade) {
        const int32 shift = shapes[static_cast<std::size_t>(cascade)].cell_shift;
        touched_cells_.push_back({
            .cascade = cascade,
            .cell    = {voxel.x >> shift, voxel.y >> shift, voxel.z >> shift},
        });
    }
}

auto light_cache::rescan_sources_(const ecs::world_grid& grid, vec3i chunk) -> void {
    found_.clear();
    if (const ecs::chunk* placed = grid.find_chunk(chunk);
        placed != nullptr && placed->get_model() != nullptr) {
        placed->get_model()->collect_emitters(emission_, found_);
    }
    std::ranges::stable_sort(found_, {}, page_key_of);

    const auto known = sources_.find(chunk);
    if (known == sources_.end() && found_.empty()) {
        return;
    }

    const vec3i base{chunk.x * chunk_voxels, chunk.y * chunk_voxels, chunk.z * chunk_voxels};
    const auto touch = [&](const std::vector<asset::emitting_voxel>& voxels) {
        for (const asset::emitting_voxel& at : voxels) {
            touch_sources_at_({base.x + at.x, base.y + at.y, base.z + at.z});
        }
    };

    if (known != sources_.end()) {
        touch(known->second);
    }
    touch(found_);

    if (found_.empty()) {
        sources_.erase(known);
        source_reach_stale_ = true;
    } else if (known == sources_.end()) {
        sources_.emplace(chunk, found_);
        source_reach_stale_ = true;
    } else {
        known->second = found_;
    }
}

auto light_cache::note_sources_(
    const ecs::world_grid& grid, const ecs::occupancy_change& change
) -> void {
    if (!change.whole_chunk && !sources_.contains(change.chunk)) {
        const ecs::chunk* placed = grid.find_chunk(change.chunk);
        if (placed == nullptr) {
            return;
        }

        const vec3i local{
            change.voxel.x - (change.chunk.x * chunk_voxels),
            change.voxel.y - (change.chunk.y * chunk_voxels),
            change.voxel.z - (change.chunk.z * chunk_voxels)
        };
        if (emission_[placed->get_model()->get_material(local).value] == 0) {
            return;
        }
    }

    rescan_sources_(grid, change.chunk);
}

auto light_cache::refresh_source_reach_() -> void {
    if (!source_reach_stale_) {
        return;
    }
    source_reach_stale_ = false;

    source_reach_.clear();
    for (const auto& [chunk, voxels] : sources_) {
        for (int32 z = -1; z <= 1; ++z) {
            for (int32 y = -1; y <= 1; ++y) {
                for (int32 x = -1; x <= 1; ++x) {
                    source_reach_.insert({chunk.x + x, chunk.y + y, chunk.z + z});
                }
            }
        }
    }
}

auto light_cache::seed_sources_(int32 cascade, vec3i brick) -> void {
    const int32 cell_shift  = shapes[static_cast<std::size_t>(cascade)].cell_shift;
    const int32 brick_reach = brick_shift + cell_shift;

    const vec3i low{brick.x << brick_reach, brick.y << brick_reach, brick.z << brick_reach};
    const vec3i chunk = chunk_of(low);

    const auto known = sources_.find(chunk);
    if (known == sources_.end()) {
        return;
    }

    const vec3i base{chunk.x * chunk_voxels, chunk.y * chunk_voxels, chunk.z * chunk_voxels};
    for (const asset::emitting_voxel& at : known->second) {
        const vec3i voxel{base.x + at.x, base.y + at.y, base.z + at.z};
        if ((voxel.x >> brick_reach) != brick.x || (voxel.y >> brick_reach) != brick.y ||
            (voxel.z >> brick_reach) != brick.z) {
            continue;
        }
        touched_cells_.push_back({
            .cascade = cascade,
            .cell    = {voxel.x >> cell_shift, voxel.y >> cell_shift, voxel.z >> cell_shift},
        });
    }
}

auto light_cache::strongest_source_in_(int32 cascade, vec3i cell) const -> held_source {
    const int32 shift = shapes[static_cast<std::size_t>(cascade)].cell_shift;

    const vec3i low{cell.x << shift, cell.y << shift, cell.z << shift};
    const vec3i chunk = chunk_of(low);

    const auto known = sources_.find(chunk);
    if (known == sources_.end()) {
        return {};
    }

    const asset::emitting_voxel first{
        .x = static_cast<uint8>(low.x - (chunk.x * chunk_voxels)),
        .y = static_cast<uint8>(low.y - (chunk.y * chunk_voxels)),
        .z = static_cast<uint8>(low.z - (chunk.z * chunk_voxels)),
    };

    held_source strongest{};
    for (const asset::emitting_voxel& at :
         std::ranges::equal_range(known->second, page_key_of(first), {}, page_key_of)) {
        if ((at.x >> shift) == (first.x >> shift) && (at.y >> shift) == (first.y >> shift) &&
            (at.z >> shift) == (first.z >> shift) && at.level > strongest.level) {
            strongest = {.level = at.level, .color = at.color};
        }
    }
    return strongest;
}

auto light_cache::flush_sources_(frame_state& current) -> void {
    const auto order = [](const source_cell& at) {
        return std::tuple{at.cascade, at.cell.z, at.cell.y, at.cell.x};
    };
    std::ranges::sort(touched_cells_, {}, order);
    const auto repeated = std::ranges::unique(touched_cells_);
    touched_cells_.erase(repeated.begin(), repeated.end());

    source_entries_.clear();
    late_cells_.clear();

    for (const source_cell& touched : touched_cells_) {
        if (source_entries_.size() >= most_sources_a_frame) {
            late_cells_.push_back(touched);
            continue;
        }

        const vec3i brick{
            touched.cell.x >> brick_shift, touched.cell.y >> brick_shift,
            touched.cell.z >> brick_shift
        };
        const slot& held = cascades_[static_cast<std::size_t>(touched.cascade)]
                               .slots[slot_index_(touched.cascade, brick)];
        if (!held.assigned || held.fresh || held.brick != brick) {
            continue;
        }

        const held_source source = strongest_source_in_(touched.cascade, touched.cell);

        // см. docs/lighting.md#цвет-света-ламп
        const vec3i block_low{
            (touched.cell.x >> tint_shift) << tint_shift,
            (touched.cell.y >> tint_shift) << tint_shift,
            (touched.cell.z >> tint_shift) << tint_shift
        };
        held_source in_block{};
        for (int32 corner = 0; corner < 8; ++corner) {
            const held_source beside = strongest_source_in_(
                touched.cascade,
                {block_low.x + (corner & 1), block_low.y + ((corner >> 1) & 1),
                 block_low.z + (corner >> 2)}
            );
            if (beside.level > in_block.level) {
                in_block = beside;
            }
        }
        const int32 block_tint = in_block.level > 0 ? tint_of_voxel_[in_block.color.value] : 0;

        source_entries_.push_back({
            touched.cell.x, touched.cell.y, touched.cell.z,
            touched.cascade | (int32{source.level} << source_level_shift) |
                (block_tint << source_tint_shift)
        });
    }
    touched_cells_.swap(late_cells_);

    current.source_cells = static_cast<uint32>(source_entries_.size());
    if (!source_entries_.empty()) {
        current.sources->copy_from_vector(source_entries_);
    }

    stats_.sources_frame = current.source_cells;
    stats_.source_chunks = static_cast<uint32>(sources_.size());
}

auto light_cache::choose_bricks(
    const ecs::world_grid* grid, vec3i centre_voxel,
    std::span<const ecs::occupancy_change> changes, const light_cache_settings& settings,
    uint32 frame
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
        if (grid != nullptr) {
            note_sources_(*grid, change);
        }
    }
    refresh_source_reach_();

    const uint32 budget = std::min(settings.bricks_per_frame, most_bricks_a_frame);

    chosen_.clear();
    ++serial_;
    uint32 waiting = 0;

    for (int32 cascade = cascade_count - 1; cascade >= 0; --cascade) {
        auto& state        = cascades_[static_cast<std::size_t>(cascade)];
        const uint8 passes = shapes[static_cast<std::size_t>(cascade)].passes_to_settle;

        const int32 brick_reach =
            brick_shift + shapes[static_cast<std::size_t>(cascade)].cell_shift;

        uint32 taken_above = 0;
        for (uint8 wave = passes; wave > 0 && chosen_.size() < budget; --wave) {
            uint32 left = state.owing[wave] - taken_above;
            taken_above = 0;

            for (std::size_t index = 0; left > 0 && chosen_.size() < budget; ++index) {
                slot& held = state.slots[index];
                if (held.passes_left != wave || held.chosen_at == serial_) {
                    continue;
                }
                --left;
                ++taken_above;

                int32 flags = (wave == passes ? seeds_from_sky : 0) | (wave == 1 ? last_pass : 0);
                if (wave == passes && held.fresh) {
                    flags      |= wipes_sources;
                    held.fresh = false;
                    seed_sources_(cascade, held.brick);
                }
                if (!source_reach_.empty() &&
                    source_reach_.contains(chunk_of({
                        held.brick.x << brick_reach, held.brick.y << brick_reach,
                        held.brick.z << brick_reach
                    }))) {
                    flags |= near_sources;
                }
                chosen_.push_back({held.brick.x, held.brick.y, held.brick.z, cascade | flags});

                --state.owing[wave];
                --held.passes_left;
                if (held.passes_left == 0 && held.armed_again) {
                    held.armed_again = false;
                    held.passes_left = passes;
                }
                ++state.owing[held.passes_left];
                held.chosen_at = serial_;
            }
        }

        waiting += static_cast<uint32>(state.slots.size()) - state.owing[0];
    }

    auto& current  = frames_[frame];
    current.bricks = static_cast<uint32>(chosen_.size());
    if (!chosen_.empty()) {
        current.queue->copy_from_vector(chosen_);
    }
    flush_sources_(current);

    stats_.bricks_frame = current.bricks;
    stats_.bricks_total += current.bricks;
    stats_.waiting      = waiting;
    stats_.peak_waiting = std::max(stats_.peak_waiting, waiting);
}

auto light_cache::dispatch(
    vk::CommandBuffer cmd, vk::DescriptorSet occupancy_set, vec3i base_chunk, uint32 frame
) const -> void {
    const auto& current = frames_[frame];
    if (current.bricks == 0 && current.source_cells == 0) {
        return;
    }

    const std::array sets{occupancy_set, current.set};
    cmd.bindDescriptorSets(vk::PipelineBindPoint::eCompute, pipeline_layout_, 0, sets, nullptr);

    light_cache_push push{};
    push.base_chunk = {base_chunk.x, base_chunk.y, base_chunk.z, 0};
    for (std::size_t cascade = 0; cascade < cascades_.size(); ++cascade) {
        const vec3i origin   = cascades_[cascade].origin;
        push.window[cascade] = {
            origin.x * brick_texels, origin.y * brick_texels, origin.z * brick_texels, 0
        };
    }

    const auto settle = [&] {
        cmd.pipelineBarrier(
            vk::PipelineStageFlagBits::eComputeShader,
            vk::PipelineStageFlagBits::eComputeShader | vk::PipelineStageFlagBits::eFragmentShader,
            {},
            vk::MemoryBarrier{
                .srcAccessMask = vk::AccessFlagBits::eShaderWrite,
                .dstAccessMask = vk::AccessFlagBits::eShaderRead | vk::AccessFlagBits::eShaderWrite,
            },
            nullptr, nullptr
        );
    };

    if (current.bricks > 0) {
        cmd.bindPipeline(vk::PipelineBindPoint::eCompute, pipeline_);
        cmd.pushConstants<light_cache_push>(
            pipeline_layout_, vk::ShaderStageFlagBits::eCompute, 0, push
        );
        cmd.dispatch(current.bricks, 1, 1);
        settle();
    }

    if (current.source_cells > 0) {
        constexpr uint32 cells_a_group = 64;

        push.base_chunk.w = static_cast<int32>(current.source_cells);

        cmd.bindPipeline(vk::PipelineBindPoint::eCompute, scatter_pipeline_);
        cmd.pushConstants<light_cache_push>(
            pipeline_layout_, vk::ShaderStageFlagBits::eCompute, 0, push
        );
        cmd.dispatch((current.source_cells + cells_a_group - 1) / cells_a_group, 1, 1);
        settle();
    }
}

auto light_cache::wrap_of(int32 cascade, vec3i base_chunk) -> vec4f {
    const int32 span = shapes[static_cast<std::size_t>(cascade)].span_voxels();

    const auto along = [&](int32 chunk) -> float32 {
        const int32 voxel = chunk * chunk_voxels;
        return static_cast<float32>(((voxel % span) + span) % span) / static_cast<float32>(span);
    };

    return {along(base_chunk.x), along(base_chunk.y), along(base_chunk.z), 0.0f};
}

}  // namespace vw::gfx
