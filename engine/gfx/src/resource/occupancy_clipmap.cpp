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

using layout = spatial::occupancy_clipmap_layout;

constexpr vk::Format brick_format = vk::Format::eR8Uint;

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

    throw std::runtime_error("no memory type fits an occupancy image");
}

auto whole_image() -> vk::ImageSubresourceRange {
    vk::ImageSubresourceRange range{};
    range.aspectMask = vk::ImageAspectFlagBits::eColor;
    range.levelCount = 1;
    range.layerCount = 1;
    return range;
}

constexpr vk::PipelineStageFlags reading_stages =
    vk::PipelineStageFlagBits::eFragmentShader | vk::PipelineStageFlagBits::eComputeShader;

}  // namespace

occupancy_clipmap::occupancy_clipmap(
    vulkan_context& context, vk::DescriptorPool descriptor_pool,
    const model_occupancy_buffer& model_volumes
)
    : context_{&context}
    , descriptor_pool_{descriptor_pool}
    , scratch_{std::make_unique<asset::chunk_occupancy>()} {
    for (int32 level = 0; level < layout::level_count; ++level) {
        levels_[static_cast<std::size_t>(level)].slots.resize(
            static_cast<std::size_t>(layout::slot_count(level))
        );
        stats_.slot_count += static_cast<uint32>(layout::slot_count(level));
    }

    for (auto& frame : frames_) {
        frame.staging = std::make_unique<buffer>(
            context, staging_bytes, vk::BufferUsageFlagBits::eTransferSrc,
            vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent
        );
        frame.mapped = static_cast<uint8*>(frame.staging->map());
        frame.params = std::make_unique<uniform_buffer>(context, sizeof(occupancy_params));
    }

    create_images_();
    create_sets_(model_volumes);
}

occupancy_clipmap::~occupancy_clipmap() {
    const vk::Device device = context_->get_device();

    for (auto& frame : frames_) {
        frame.staging->unmap();
    }
    for (const auto set : sets_) {
        static_cast<void>(device.freeDescriptorSets(descriptor_pool_, set));
    }
    for (auto& level : levels_) {
        device.destroyImageView(level.view);
        device.destroyImage(level.image);
        device.freeMemory(level.memory);
    }
    device.destroyDescriptorSetLayout(set_layout_);
    device.destroySampler(sampler_);
}

auto occupancy_clipmap::create_images_() -> void {
    const vk::Device device = context_->get_device();

    for (std::size_t index = 0; index < levels_.size(); ++index) {
        auto& level = levels_[index];

        const auto side = static_cast<uint32>(layout::texture_side(static_cast<int32>(index)));

        vk::ImageCreateInfo image_info{};
        image_info.imageType = vk::ImageType::e3D;
        image_info.extent    = vk::Extent3D{side, side, side};
        image_info.mipLevels     = 1;
        image_info.arrayLayers   = 1;
        image_info.format        = brick_format;
        image_info.tiling        = vk::ImageTiling::eOptimal;
        image_info.initialLayout = vk::ImageLayout::eUndefined;
        image_info.usage =
            vk::ImageUsageFlagBits::eTransferDst | vk::ImageUsageFlagBits::eSampled;
        image_info.samples     = vk::SampleCountFlagBits::e1;
        image_info.sharingMode = vk::SharingMode::eExclusive;

        level.image = vk_must(device.createImage(image_info), "create occupancy image");

        const vk::MemoryRequirements needs = device.getImageMemoryRequirements(level.image);

        vk::MemoryAllocateInfo alloc_info{};
        alloc_info.allocationSize  = needs.size;
        alloc_info.memoryTypeIndex = memory_type_for(
            context_->get_physical_device(), needs.memoryTypeBits,
            vk::MemoryPropertyFlagBits::eDeviceLocal
        );

        level.memory = vk_must(device.allocateMemory(alloc_info), "allocate occupancy image");
        vk_must(device.bindImageMemory(level.image, level.memory, 0), "bind occupancy image");

        vk::ImageViewCreateInfo view_info{};
        view_info.image            = level.image;
        view_info.viewType         = vk::ImageViewType::e3D;
        view_info.format           = brick_format;
        view_info.subresourceRange = whole_image();

        level.view = vk_must(device.createImageView(view_info), "create occupancy image view");
    }

    vk::SamplerCreateInfo sampler_info{};
    sampler_info.magFilter     = vk::Filter::eNearest;
    sampler_info.minFilter     = vk::Filter::eNearest;
    sampler_info.mipmapMode    = vk::SamplerMipmapMode::eNearest;
    sampler_info.addressModeU  = vk::SamplerAddressMode::eRepeat;
    sampler_info.addressModeV  = vk::SamplerAddressMode::eRepeat;
    sampler_info.addressModeW  = vk::SamplerAddressMode::eRepeat;
    sampler_info.maxAnisotropy = 1.0f;

    sampler_ = vk_must(device.createSampler(sampler_info), "create occupancy sampler");
}

auto occupancy_clipmap::create_sets_(const model_occupancy_buffer& model_volumes) -> void {
    const vk::Device device = context_->get_device();

    constexpr vk::ShaderStageFlags readers =
        vk::ShaderStageFlagBits::eFragment | vk::ShaderStageFlagBits::eCompute;

    std::array<vk::DescriptorSetLayoutBinding, 3> bindings{};
    bindings[2].binding         = 2;
    bindings[2].descriptorType  = vk::DescriptorType::eStorageBuffer;
    bindings[2].descriptorCount = 1;
    bindings[2].stageFlags      = readers;
    bindings[0].binding         = 0;
    bindings[0].descriptorType  = vk::DescriptorType::eUniformBuffer;
    bindings[0].descriptorCount = 1;
    bindings[0].stageFlags      = readers;
    bindings[1].binding         = 1;
    bindings[1].descriptorType  = vk::DescriptorType::eCombinedImageSampler;
    bindings[1].descriptorCount = static_cast<uint32>(layout::level_count);
    bindings[1].stageFlags      = readers;

    set_layout_ = vk_must(
        device.createDescriptorSetLayout({
            .bindingCount = static_cast<uint32>(bindings.size()),
            .pBindings    = bindings.data(),
        }),
        "create occupancy set layout"
    );

    std::array<vk::DescriptorSetLayout, frames_in_flight> layouts{};
    layouts.fill(set_layout_);

    const auto sets = vk_must(
        device.allocateDescriptorSets({
            .descriptorPool     = descriptor_pool_,
            .descriptorSetCount = frames_in_flight,
            .pSetLayouts        = layouts.data(),
        }),
        "allocate occupancy descriptor sets"
    );

    std::array<vk::DescriptorImageInfo, layout::level_count> images{};
    for (std::size_t level = 0; level < images.size(); ++level) {
        images[level].sampler     = sampler_;
        images[level].imageView   = levels_[level].view;
        images[level].imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal;
    }

    for (uint32 frame = 0; frame < frames_in_flight; ++frame) {
        sets_[frame] = sets[frame];

        vk::DescriptorBufferInfo params_info{};
        params_info.buffer = frames_[frame].params->get_buffer();
        params_info.offset = 0;
        params_info.range  = sizeof(occupancy_params);

        vk::DescriptorBufferInfo volumes_info{};
        volumes_info.buffer = model_volumes.get_buffer();
        volumes_info.offset = 0;
        volumes_info.range  = model_occupancy_buffer::byte_size();

        std::array<vk::WriteDescriptorSet, 3> writes{};
        writes[2].dstSet          = sets_[frame];
        writes[2].dstBinding      = 2;
        writes[2].descriptorType  = vk::DescriptorType::eStorageBuffer;
        writes[2].descriptorCount = 1;
        writes[2].pBufferInfo     = &volumes_info;
        writes[0].dstSet          = sets_[frame];
        writes[0].dstBinding      = 0;
        writes[0].descriptorType  = vk::DescriptorType::eUniformBuffer;
        writes[0].descriptorCount = 1;
        writes[0].pBufferInfo     = &params_info;
        writes[1].dstSet          = sets_[frame];
        writes[1].dstBinding      = 1;
        writes[1].descriptorType  = vk::DescriptorType::eCombinedImageSampler;
        writes[1].descriptorCount = static_cast<uint32>(images.size());
        writes[1].pImageInfo      = images.data();

        device.updateDescriptorSets(writes, nullptr);
    }
}

auto occupancy_clipmap::mark_(
    level_state& state, int32 level, std::size_t index, bool known, bool hollow
) -> void {
    slot& held = state.slots[index];

    // см. docs/rendering.md#занятость-на-gpu
    const bool was_told = held.valid && !held.hollow;
    const bool told     = known && !hollow;

    if (held.valid != known) {
        stats_.valid_slots += known ? 1U : ~0U;
    }
    held.valid  = known;
    held.hollow = hollow;

    if (was_told == told) {
        return;
    }

    const auto bit  = static_cast<std::size_t>(layout::first_valid_bit(level)) + index;
    const uint32 at = 1U << (bit & 31);
    if (told) {
        params_.valid[bit >> 7][(bit >> 5) & 3] |= at;
    } else {
        params_.valid[bit >> 7][(bit >> 5) & 3] &= ~at;
    }
    frames_behind_ = frames_in_flight;
}

auto occupancy_clipmap::forget_everything_() -> void {
    params_            = occupancy_params{};
    stats_.valid_slots = 0;
    frames_behind_     = frames_in_flight;

    for (auto& level : levels_) {
        for (auto& held : level.slots) {
            held = slot{};
        }
        level.queue.clear();
        level.queue_head = 0;
    }
    touched_.clear();
    touched_head_ = 0;
}

auto occupancy_clipmap::enqueue_(level_state& state, int32 index) -> void {
    auto& held = state.slots[static_cast<std::size_t>(index)];
    if (held.queued) {
        return;
    }
    held.queued = true;
    state.queue.push_back(index);
}

auto occupancy_clipmap::move_window_(int32 level, vec3i origin) -> void {
    auto& state  = levels_[static_cast<std::size_t>(level)];
    state.origin = origin;

    params_.origin[static_cast<std::size_t>(level)] = vec4<int32>{origin.x, origin.y, origin.z, 0};
    frames_behind_ = frames_in_flight;

    const int32 side = layout::window_chunks(level);
    for (int32 z = 0; z < side; ++z) {
        for (int32 y = 0; y < side; ++y) {
            for (int32 x = 0; x < side; ++x) {
                const vec3i chunk{origin.x + x, origin.y + y, origin.z + z};
                const int32 index =
                    spatial::occupancy_slot_index(spatial::occupancy_slot_of(chunk, level), level);

                auto& held = state.slots[static_cast<std::size_t>(index)];
                if (held.assigned && held.chunk == chunk) {
                    continue;
                }

                mark_(state, level, static_cast<std::size_t>(index), false, false);

                held.chunk    = chunk;
                held.assigned = true;
                held.valid    = false;
                held.touched  = false;
                enqueue_(state, index);
            }
        }
    }
}

auto occupancy_clipmap::coarsest_slot_(vec3i chunk) -> slot& {
    constexpr int32 coarsest = layout::level_count - 1;
    return levels_[static_cast<std::size_t>(coarsest)].slots[static_cast<std::size_t>(
        spatial::occupancy_slot_index(spatial::occupancy_slot_of(chunk, coarsest), coarsest)
    )];
}

auto occupancy_clipmap::touch_(const ecs::occupancy_change& change) -> void {
    constexpr int32 coarsest = layout::level_count - 1;

    const auto& widest = levels_[static_cast<std::size_t>(coarsest)];
    if (!spatial::occupancy_window_holds(change.chunk, widest.origin, coarsest)) {
        return;
    }

    auto& held = coarsest_slot_(change.chunk);
    if (held.touched) {
        for (std::size_t at = touched_head_; at < touched_.size(); ++at) {
            if (touched_[at].chunk == change.chunk && touched_[at] != change) {
                touched_[at].whole_chunk = true;
            }
        }
        return;
    }
    held.touched = true;
    touched_.push_back(change);
}

auto occupancy_clipmap::report_packed_(const ecs::occupancy_change& change) -> void {
    if (!packed_changes_.empty() && packed_changes_.back() == change) {
        return;
    }
    packed_changes_.push_back(change);
}

auto occupancy_clipmap::update(ecs::world& world, const vec3f& eye, uint32 frame) -> void {
    auto& current = frames_[frame];
    for (auto& copies : current.copies) {
        copies.clear();
    }
    stats_.packed_frame = 0;
    stats_.pack_ms      = 0.0F;
    packed_changes_.clear();

    const auto* grid = world.system<ecs::world_grid_system>().grid();
    if (grid == nullptr) {
        write_params_(current);
        return;
    }

    units_per_voxel_ = static_cast<float32>(grid->world_units_per_voxel());

    const vec3i centre_voxel{
        static_cast<int32>(std::floor(eye.x / units_per_voxel_)),
        static_cast<int32>(std::floor(eye.y / units_per_voxel_)),
        static_cast<int32>(std::floor(eye.z / units_per_voxel_)),
    };
    centre_chunk_ = spatial::occupancy_chunk_of(centre_voxel);
    centre_voxel_ = centre_voxel;

    std::optional<std::span<const ecs::occupancy_change>> changes;
    if (grid == grid_) {
        changes = grid->occupancy_changes_since(seen_serial_);
    }
    const bool starts_over = !changes.has_value();
    if (starts_over) {
        forget_everything_();
        grid_ = grid;
    }

    for (int32 level = 0; level < layout::level_count; ++level) {
        const vec3i origin = spatial::occupancy_window_origin(centre_voxel, level);
        if (starts_over || origin != levels_[static_cast<std::size_t>(level)].origin) {
            move_window_(level, origin);
        }
    }

    if (changes) {
        for (const auto& change : *changes) {
            touch_(change);
        }
    }
    seen_serial_ = grid->occupancy_serial();

    pack_queued_(*grid, current);
    write_params_(current);
}

auto occupancy_clipmap::read_chunk_(const ecs::world_grid& grid, vec3i chunk)
    -> asset::model_fill {
    const ecs::chunk* placed = grid.find_chunk(chunk);
    if (placed == nullptr) {
        return asset::model_fill::air;
    }

    const auto fill = placed->get_fill();
    if (fill != asset::model_fill::mixed) {
        return fill;
    }
    return placed->get_model()->build_rows_page_by_page(*scratch_) ? asset::model_fill::mixed
                                                        : asset::model_fill::air;
}

auto occupancy_clipmap::has_room_for_(int32 level) const -> bool {
    return staged_ + asset::occupancy_brick_count(level) <= staging_bytes;
}

auto occupancy_clipmap::stage_(
    frame_state& frame, int32 level, slot& held, asset::model_fill fill
) -> void {
    const int32 across = layout::bricks_per_chunk(level);
    const auto bytes   = asset::occupancy_brick_count(level);

    const std::span<uint8> bricks{frame.mapped + staged_, bytes};
    if (fill == asset::model_fill::mixed) {
        asset::pack_occupancy_bricks(*scratch_, level, bricks);
    } else {
        std::ranges::fill(bricks, fill == asset::model_fill::solid ? uint8{0xFF} : uint8{0});
    }

    const vec3i slot_at = spatial::occupancy_slot_of(held.chunk, level);

    vk::BufferImageCopy region{};
    region.bufferOffset                = staged_;
    region.imageSubresource.aspectMask = vk::ImageAspectFlagBits::eColor;
    region.imageSubresource.layerCount = 1;
    region.imageOffset =
        vk::Offset3D{slot_at.x * across, slot_at.y * across, slot_at.z * across};
    region.imageExtent = vk::Extent3D{
        static_cast<uint32>(across), static_cast<uint32>(across), static_cast<uint32>(across)
    };
    frame.copies[static_cast<std::size_t>(level)].push_back(region);

    staged_ += bytes;
    mark_(
        levels_[static_cast<std::size_t>(level)], level,
        static_cast<std::size_t>(
            spatial::occupancy_slot_index(spatial::occupancy_slot_of(held.chunk, level), level)
        ),
        true, fill == asset::model_fill::air
    );
    ++stats_.packed_frame;

}

auto occupancy_clipmap::pack_queued_(const ecs::world_grid& grid, frame_state& frame) -> void {
    const auto started = std::chrono::steady_clock::now();

    const auto over_time = [&] {
        const std::chrono::duration<float32, std::milli> spent =
            std::chrono::steady_clock::now() - started;
        return spent.count() > pack_budget_ms;
    };

    staged_            = 0;
    bool out_of_budget = false;

    while (!out_of_budget && touched_head_ < touched_.size()) {
        if (staged_ + (asset::occupancy_brick_count(0) * 2) > staging_bytes) {
            out_of_budget = true;
            break;
        }

        const ecs::occupancy_change change = touched_[touched_head_++];
        const vec3i chunk                  = change.chunk;
        coarsest_slot_(chunk).touched      = false;
        report_packed_(change);

        const auto fill = read_chunk_(grid, chunk);
        for (int32 level = 0; level < layout::level_count; ++level) {
            auto& state = levels_[static_cast<std::size_t>(level)];
            if (!spatial::occupancy_window_holds(chunk, state.origin, level)) {
                continue;
            }
            stage_(
                frame, level,
                state.slots[static_cast<std::size_t>(spatial::occupancy_slot_index(
                    spatial::occupancy_slot_of(chunk, level), level
                ))],
                fill
            );
        }

        out_of_budget = fill == asset::model_fill::mixed && over_time();
    }

    if (touched_head_ == touched_.size()) {
        touched_.clear();
        touched_head_ = 0;
    }

    uint32 queued = static_cast<uint32>(touched_.size() - touched_head_);

    for (int32 level = 0; level < layout::level_count; ++level) {
        auto& state = levels_[static_cast<std::size_t>(level)];

        while (!out_of_budget && state.queue_head < state.queue.size()) {
            if (!has_room_for_(level)) {
                out_of_budget = true;
                break;
            }

            const int32 index = state.queue[state.queue_head++];
            auto& held        = state.slots[static_cast<std::size_t>(index)];
            held.queued       = false;

            const auto fill = read_chunk_(grid, held.chunk);
            stage_(frame, level, held, fill);
            report_packed_({.chunk = held.chunk});

            out_of_budget = fill == asset::model_fill::mixed && over_time();
        }

        if (state.queue_head == state.queue.size()) {
            state.queue.clear();
            state.queue_head = 0;
        }
        queued += static_cast<uint32>(state.queue.size() - state.queue_head);
    }

    const std::chrono::duration<float32, std::milli> spent =
        std::chrono::steady_clock::now() - started;

    stats_.pack_ms      = spent.count();
    stats_.pack_peak_ms = std::max(stats_.pack_peak_ms, stats_.pack_ms);
    stats_.queued       = queued;
    stats_.packed_total += stats_.packed_frame;
    stats_.uploaded_bytes += staged_;
}

auto occupancy_clipmap::write_params_(frame_state& frame) -> void {
    if (frames_behind_ == 0 && frame.params_written) {
        return;
    }
    if (frames_behind_ > 0) {
        --frames_behind_;
    }

    frame.params->copy_from_struct(params_);
    frame.params_written = true;
}

auto occupancy_clipmap::record_uploads(vk::CommandBuffer cmd, uint32 frame) -> void {
    auto& current = frames_[frame];

    for (std::size_t level = 0; level < levels_.size(); ++level) {
        auto& state        = levels_[level];
        const auto& copies = current.copies[level];

        if (copies.empty() && state.readable) {
            continue;
        }

        vk::ImageMemoryBarrier to_write{};
        to_write.srcAccessMask = state.readable ? vk::AccessFlagBits::eShaderRead
                                                : vk::AccessFlags{};
        to_write.dstAccessMask = vk::AccessFlagBits::eTransferWrite;
        to_write.oldLayout     = state.readable ? vk::ImageLayout::eShaderReadOnlyOptimal
                                                : vk::ImageLayout::eUndefined;
        to_write.newLayout           = vk::ImageLayout::eTransferDstOptimal;
        to_write.srcQueueFamilyIndex = vk::QueueFamilyIgnored;
        to_write.dstQueueFamilyIndex = vk::QueueFamilyIgnored;
        to_write.image               = state.image;
        to_write.subresourceRange    = whole_image();

        cmd.pipelineBarrier(
            state.readable ? reading_stages
                           : vk::PipelineStageFlags{vk::PipelineStageFlagBits::eTopOfPipe},
            vk::PipelineStageFlagBits::eTransfer, {}, nullptr, nullptr, to_write
        );

        if (!copies.empty()) {
            cmd.copyBufferToImage(
                current.staging->get_buffer(), state.image,
                vk::ImageLayout::eTransferDstOptimal, copies
            );
        }

        vk::ImageMemoryBarrier to_read{};
        to_read.srcAccessMask       = vk::AccessFlagBits::eTransferWrite;
        to_read.dstAccessMask       = vk::AccessFlagBits::eShaderRead;
        to_read.oldLayout           = vk::ImageLayout::eTransferDstOptimal;
        to_read.newLayout           = vk::ImageLayout::eShaderReadOnlyOptimal;
        to_read.srcQueueFamilyIndex = vk::QueueFamilyIgnored;
        to_read.dstQueueFamilyIndex = vk::QueueFamilyIgnored;
        to_read.image               = state.image;
        to_read.subresourceRange    = whole_image();

        cmd.pipelineBarrier(
            vk::PipelineStageFlagBits::eTransfer, reading_stages, {}, nullptr, nullptr, to_read
        );

        state.readable = true;
    }
}

}  // namespace vw::gfx
