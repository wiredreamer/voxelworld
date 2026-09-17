module vw.gfx;

import std;
import vulkan;
import vw.core;
import vw.ecs;
import vw.world;
import vw.platform;
import :vk;

namespace vw::gfx {

namespace {

auto counts_binding_of(cull_list kind) -> uint32 {
    return kind == cull_list::sources ? 1U : 4U;
}

auto indices_binding_of(cull_list kind) -> uint32 {
    return kind == cull_list::sources ? 2U : 5U;
}

constexpr std::array<cull_list, cull_list_count> every_list{
    cull_list::sources,
    cull_list::blobs,
};

}  // namespace

light_grid::light_grid(
    vulkan_context& context,
    deletion_queue& deletion,
    vk::DescriptorPool descriptor_pool,
    vk::DescriptorSetLayout light_set_layout,
    std::span<const vk::DescriptorSet> light_sets
)
    : context_(&context)
    , deletion_(&deletion)
    , descriptor_pool_(descriptor_pool)
    , light_set_layout_(light_set_layout) {
    std::ranges::copy_n(light_sets.begin(), max_frames_in_flight, light_sets_.begin());

    compute_shader_ = std::make_unique<shader>(
        *context_, "shaders/light_cull.comp.spv", shader_type::COMPUTE
    );

    create_params_ubos_();
    create_pipeline_();
}

light_grid::~light_grid() {
    const vk::Device device = context_->get_device();

    device.destroyPipeline(compute_pipeline_);
    device.destroyPipelineLayout(compute_pipeline_layout_);
    device.destroyDescriptorSetLayout(params_descriptor_set_layout_);

    if (descriptor_pool_) {
        static_cast<void>(device.freeDescriptorSets(descriptor_pool_, params_descriptor_sets_));
    }
}

auto light_grid::create_params_ubos_() -> void {
    const vk::DescriptorSetLayoutBinding params_binding{
        .binding         = 0,
        .descriptorType  = vk::DescriptorType::eUniformBuffer,
        .descriptorCount = 1,
        .stageFlags      = vk::ShaderStageFlagBits::eCompute,
    };

    params_descriptor_set_layout_ = vk_must(
        context_->get_device().createDescriptorSetLayout({
            .bindingCount = 1,
            .pBindings    = &params_binding,
        }),
        "create light cull params descriptor set layout"
    );

    std::array<vk::DescriptorSetLayout, params_slots_> layouts{};
    layouts.fill(params_descriptor_set_layout_);

    const auto sets = vk_must(
        context_->get_device().allocateDescriptorSets({
            .descriptorPool     = descriptor_pool_,
            .descriptorSetCount = params_slots_,
            .pSetLayouts        = layouts.data(),
        }),
        "allocate light cull params descriptor sets"
    );
    std::ranges::copy(sets, params_descriptor_sets_.begin());

    for (uint32 slot = 0; slot < params_slots_; ++slot) {
        params_ubos_[slot] = std::make_unique<uniform_buffer>(
            *context_, static_cast<vk::DeviceSize>(sizeof(light_cull_ubo))
        );

        const vk::DescriptorBufferInfo buffer_info{
            .buffer = params_ubos_[slot]->get_buffer(),
            .offset = 0,
            .range  = sizeof(light_cull_ubo),
        };

        context_->get_device().updateDescriptorSets(
            vk::WriteDescriptorSet{
                .dstSet          = params_descriptor_sets_[slot],
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

auto light_grid::create_pipeline_() -> void {
    std::array<vk::DescriptorSetLayout, 2> set_layouts{
        params_descriptor_set_layout_,
        light_set_layout_,
    };

    compute_pipeline_layout_ = vk_must(
        context_->get_device().createPipelineLayout({
            .setLayoutCount = static_cast<uint32>(set_layouts.size()),
            .pSetLayouts    = set_layouts.data(),
        }),
        "create light cull pipeline layout"
    );

    compute_pipeline_ = vk_must(
        context_->get_device().createComputePipeline(
            nullptr,
            {
                .stage  = compute_shader_->get_stage_info(),
                .layout = compute_pipeline_layout_,
            }
        ),
        "create light cull pipeline"
    );
}

auto light_grid::cap_of(cull_list kind) const -> uint32 {
    return kind == cull_list::sources ? cap_ : blob_cap_;
}

auto light_grid::counts_size_() const -> vk::DeviceSize {
    return (static_cast<vk::DeviceSize>(cluster_count_) * sizeof(uint32)) +
           sizeof(cluster_readback::overflow_count);
}

auto light_grid::indices_size_(cull_list kind) const -> vk::DeviceSize {
    return static_cast<vk::DeviceSize>(cluster_count_) * cap_of(kind) * sizeof(uint32);
}

auto light_grid::set_grid(
    const spatial::cluster_grid& grid, uint32 cap, uint32 blob_cap
) -> void {
    const uint32 clusters = grid.cluster_count();

    if (clusters != cluster_count_ || cap != cap_ || blob_cap != blob_cap_) {
        ++generation_;
    }

    grid_          = grid;
    cap_           = cap;
    blob_cap_      = blob_cap;
    cluster_count_ = clusters;
}

auto light_grid::rebuild_frame_(
    uint32 frame_index
) -> void {
    for (const cull_list kind : every_list) {
        rebuild_list_(kind, frame_index);
    }

    built_[frame_index] = generation_;
}

auto light_grid::rebuild_list_(
    cull_list kind, uint32 frame_index
) -> void {
    list_frame& lf = list_(kind, frame_index);

    auto new_counts  = std::make_unique<device_storage_buffer>(*context_, counts_size_());
    auto new_indices = std::make_unique<device_storage_buffer>(*context_, indices_size_(kind));

    if (lf.counts) {
        deletion_->retire(std::exchange(lf.counts, std::move(new_counts)));
        deletion_->retire(std::exchange(lf.indices, std::move(new_indices)));
    } else {
        lf.counts  = std::move(new_counts);
        lf.indices = std::move(new_indices);
    }

    const vk::DescriptorBufferInfo counts_info{
        .buffer = lf.counts->get_buffer(),
        .offset = 0,
        .range  = vk::WholeSize,
    };

    const vk::DescriptorBufferInfo indices_info{
        .buffer = lf.indices->get_buffer(),
        .offset = 0,
        .range  = vk::WholeSize,
    };

    const std::array<vk::WriteDescriptorSet, 2> writes{
        vk::WriteDescriptorSet{
            .dstSet          = light_sets_[frame_index],
            .dstBinding      = counts_binding_of(kind),
            .dstArrayElement = 0,
            .descriptorCount = 1,
            .descriptorType  = vk::DescriptorType::eStorageBuffer,
            .pBufferInfo     = &counts_info,
        },
        vk::WriteDescriptorSet{
            .dstSet          = light_sets_[frame_index],
            .dstBinding      = indices_binding_of(kind),
            .dstArrayElement = 0,
            .descriptorCount = 1,
            .descriptorType  = vk::DescriptorType::eStorageBuffer,
            .pBufferInfo     = &indices_info,
        },
    };

    context_->get_device().updateDescriptorSets(writes, nullptr);

    rebuild_mirrors_(kind, frame_index);
    lf.pending_valid = false;
}

auto light_grid::rebuild_mirrors_(
    cull_list kind, uint32 frame_index
) -> void {
    list_frame& lf = list_(kind, frame_index);

    auto drop = [this](std::unique_ptr<storage_buffer>& slot) -> void {
        if (slot) {
            deletion_->retire(std::move(slot));
        }
    };

    drop(lf.counts_host);
    drop(lf.indices_host);

    if (readback_ == cluster_readback_level::off) {
        return;
    }

    lf.counts_host = std::make_unique<storage_buffer>(
        *context_, counts_size_(), vk::BufferUsageFlagBits::eTransferDst
    );

    if (readback_ == cluster_readback_level::full) {
        lf.indices_host = std::make_unique<storage_buffer>(
            *context_, indices_size_(kind), vk::BufferUsageFlagBits::eTransferDst
        );
    }
}

auto light_grid::set_readback(
    cluster_readback_level level
) -> void {
    if (level == readback_) {
        return;
    }

    readback_ = level;

    ++generation_;

    for (auto& slot : ready_) {
        slot.reset();
    }

    for (auto& per_kind : lists_) {
        for (auto& lf : per_kind) {
            lf.pending_valid = false;
        }
    }
}

auto light_grid::take_readback(
    cull_list kind
) -> std::optional<cluster_readback> {
    auto& slot = ready_[static_cast<uint32>(kind)];

    if (!slot) {
        return std::nullopt;
    }

    auto out = std::move(*slot);
    slot.reset();

    return out;
}

auto light_grid::harvest_(
    cull_list kind, uint32 frame_index
) -> void {
    list_frame& lf = list_(kind, frame_index);

    if (!lf.pending_valid) {
        return;
    }

    lf.pending_valid = false;

    const auto clusters = static_cast<std::size_t>(lf.pending.grid.cluster_count());

    if (lf.counts_host) {
        const std::size_t cluster_counts_bytes = clusters * sizeof(uint32);

        lf.pending.cluster_counts.resize(clusters);
        lf.counts_host->copy_to(lf.pending.cluster_counts.data(), cluster_counts_bytes);
        lf.counts_host->copy_to_struct(lf.pending.overflow_count, cluster_counts_bytes);
    }

    if (lf.indices_host) {
        lf.pending.indices.resize(clusters * lf.pending.cap);
        lf.indices_host->copy_to(
            lf.pending.indices.data(), clusters * lf.pending.cap * sizeof(uint32)
        );
    } else {
        lf.pending.indices.clear();
    }

    ready_[static_cast<uint32>(kind)] = std::move(lf.pending);
}

auto light_grid::write_params_(
    cull_list kind, const mat4f& view, uint32 sphere_count, uint32 frame_index
) -> void {
    light_cull_ubo ubo{
        .z_scale       = grid_.z_scale(),
        .z_bias        = grid_.z_bias(),
        .tile_size     = static_cast<float32>(grid_.tile_size),
        .slices        = static_cast<float32>(grid_.slices),
        .near_depth    = grid_.near_depth,
        .far_depth     = grid_.far_depth,
        .proj_x        = grid_.proj_x,
        .proj_y        = grid_.proj_y,
        .screen_width  = static_cast<float32>(grid_.screen_width),
        .screen_height = static_cast<float32>(grid_.screen_height),
        .tiles_x       = static_cast<float32>(grid_.tiles_x()),
        .tiles_y       = static_cast<float32>(grid_.tiles_y()),
        .cap           = cap_of(kind),
        .shape_count   = sphere_count,
        .cluster_count = cluster_count_,
        .list          = kind,
    };

    std::memcpy(ubo.view, view.cptr(), sizeof(mat4f));

    params_ubos_[params_slot_(kind, frame_index)]->copy_from_struct(ubo);
}

auto light_grid::record_(
    vk::CommandBuffer cmd, vk::DescriptorSet light_set, cull_list kind, uint32 sphere_count,
    uint32 frame_index
) -> void {
    if (sphere_count == 0) {
        return;
    }

    cmd.bindDescriptorSets(
        vk::PipelineBindPoint::eCompute, compute_pipeline_layout_, 0,
        params_descriptor_sets_[params_slot_(kind, frame_index)], nullptr
    );

    cmd.bindDescriptorSets(
        vk::PipelineBindPoint::eCompute, compute_pipeline_layout_, 1, light_set, nullptr
    );

    cmd.dispatch(sphere_count, grid_.slices, 1);
}

auto light_grid::snapshot_(
    cull_list kind, uint32 frame_index
) -> cluster_readback& {
    list_frame& lf = list_(kind, frame_index);

    lf.pending.kind = kind;
    lf.pending.grid = grid_;
    lf.pending.cap  = cap_of(kind);
    lf.pending.columns.clear();
    lf.pending_valid = true;

    return lf.pending;
}

auto light_grid::dispatch(
    vk::CommandBuffer cmd,
    vk::DescriptorSet light_set,
    const mat4f& view,
    std::span<const point_light_data> lights,
    std::span<const blob_data> blobs,
    uint32 frame_index
) -> void {
    if (cluster_count_ == 0) {
        return;
    }

    for (const cull_list kind : every_list) {
        harvest_(kind, frame_index);
    }

    if (built_[frame_index] != generation_) {
        rebuild_frame_(frame_index);
    }

    const auto light_count = static_cast<uint32>(lights.size());
    const auto blob_count  = static_cast<uint32>(blobs.size());

    write_params_(cull_list::sources, view, light_count, frame_index);
    write_params_(cull_list::blobs, view, blob_count, frame_index);

    for (const cull_list kind : every_list) {
        cmd.fillBuffer(list_(kind, frame_index).counts->get_buffer(), 0, counts_size_(), 0);
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

    if (light_count > 0 || blob_count > 0) {
        cmd.bindPipeline(vk::PipelineBindPoint::eCompute, compute_pipeline_);

        record_(cmd, light_set, cull_list::sources, light_count, frame_index);
        record_(cmd, light_set, cull_list::blobs, blob_count, frame_index);
    }

    const bool reading_back = readback_ != cluster_readback_level::off;

    cmd.pipelineBarrier(
        vk::PipelineStageFlagBits::eComputeShader,
        reading_back
            ? vk::PipelineStageFlagBits::eFragmentShader | vk::PipelineStageFlagBits::eTransfer
            : vk::PipelineStageFlagBits::eFragmentShader,
        {},
        vk::MemoryBarrier{
            .srcAccessMask = vk::AccessFlagBits::eShaderWrite,
            .dstAccessMask = reading_back
                ? vk::AccessFlagBits::eShaderRead | vk::AccessFlagBits::eTransferRead
                : vk::AccessFlagBits::eShaderRead,
        },
        nullptr,
        nullptr
    );

    if (!reading_back) {
        return;
    }

    for (const cull_list kind : every_list) {
        list_frame& lf = list_(kind, frame_index);

        if (lf.counts_host) {
            cmd.copyBuffer(
                lf.counts->get_buffer(), lf.counts_host->get_buffer(),
                vk::BufferCopy{.size = counts_size_()}
            );
        }

        if (lf.indices_host) {
            cmd.copyBuffer(
                lf.indices->get_buffer(), lf.indices_host->get_buffer(),
                vk::BufferCopy{.size = indices_size_(kind)}
            );
        }
    }

    auto to_view_depth = [&view](const vec4f& point) -> spatial::view_depth_point {
        const vec4f in_view = view * vec4f{point.x, point.y, point.z, 1.0F};

        return spatial::view_depth_point{.x = in_view.x, .y = in_view.y, .depth = -in_view.z};
    };

    cluster_readback& sources = snapshot_(cull_list::sources, frame_index);

    sources.columns.reserve(lights.size());
    for (const point_light_data& light : lights) {
        const spatial::view_depth_point at = to_view_depth(light.position);

        sources.columns.push_back(spatial::view_capsule{
            .end_a = at, .end_b = at, .radius = light.range,
        });
    }

    cluster_readback& bodies = snapshot_(cull_list::blobs, frame_index);

    bodies.columns.reserve(blobs.size());
    for (const blob_data& blob : blobs) {
        bodies.columns.push_back(spatial::view_capsule{
            .end_a  = to_view_depth(blob.cull_a),
            .end_b  = to_view_depth(blob.cull_b),
            .radius = blob.cull_a.w,
        });
    }
}

auto light_grid::get_cluster_count() const -> uint32 {
    return cluster_count_;
}

auto light_grid::get_cap() const -> uint32 {
    return cap_;
}

}  // namespace vw::gfx
