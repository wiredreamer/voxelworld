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

constexpr uint32 max_tuft_quads        = 2048;
constexpr uint32 initial_instances     = 1024;
constexpr uint32 initial_quads         = 4096;
constexpr int32 refreshes_per_frame    = 3;
constexpr int32 column_side            = ecs::chunk::size;
constexpr float32 quarter_turn         = 1.5707963F;

auto cell_hash(vec3i cell) -> uint64 {
    uint64 h = (static_cast<uint64>(static_cast<uint32>(cell.x)) * 0x9E3779B97F4A7C15ULL) ^
               (static_cast<uint64>(static_cast<uint32>(cell.y)) * 0xC2B2AE3D27D4EB4FULL) ^
               (static_cast<uint64>(static_cast<uint32>(cell.z)) * 0x165667B19E3779F9ULL);
    h ^= h >> 30U;
    h *= 0xBF58476D1CE4E5B9ULL;
    h ^= h >> 27U;
    h *= 0x94D049BB133111EBULL;
    h ^= h >> 31U;
    return h;
}

auto unit_of(uint64 h, uint32 lane) -> float32 {
    return static_cast<float32>((h >> (lane * 16U)) & 0xFFFFU) / 65535.0F;
}

auto floor_div(float32 value, float32 step) -> int32 {
    return static_cast<int32>(std::floor(value / step));
}

}  // namespace

grass_renderer::grass_renderer(
    vulkan_context& context, vk::DescriptorPool descriptor_pool, vk::RenderPass render_pass,
    vk::SampleCountFlagBits samples, const grass_pipeline_layouts& layouts,
    const vk::PipelineShaderStageCreateInfo& fragment_stage
)
    : context_(&context)
    , descriptor_pool_(descriptor_pool) {
    vertex_shader_ = std::make_unique<shader>(*context_, "shaders/grass.vert.spv", shader_type::VERTEX);

    std::array<vk::DescriptorSetLayoutBinding, 2> bindings{};
    for (uint32 i = 0; i < bindings.size(); ++i) {
        bindings[i] = {
            .binding         = i,
            .descriptorType  = vk::DescriptorType::eStorageBuffer,
            .descriptorCount = 1,
            .stageFlags      = vk::ShaderStageFlagBits::eVertex,
        };
    }
    set_layout_ = vk_must(
        context_->get_device().createDescriptorSetLayout({
            .bindingCount = static_cast<uint32>(bindings.size()),
            .pBindings    = bindings.data(),
        }),
        "create grass descriptor set layout"
    );

    create_pipeline_(render_pass, samples, layouts, fragment_stage);

    std::vector<uint32> pattern;
    pattern.reserve(static_cast<std::size_t>(max_tuft_quads) * 6);
    for (uint32 q = 0; q < max_tuft_quads; ++q) {
        const uint32 base = q * 4;
        for (const uint32 corner : {0U, 1U, 2U, 2U, 3U, 0U}) {
            pattern.push_back(base + corner);
        }
    }
    indices_ = std::make_unique<index_buffer>(*context_, pattern);

    std::array<vk::DescriptorSetLayout, frames_in_flight> set_layouts{};
    set_layouts.fill(set_layout_);
    const auto sets = vk_must(
        context_->get_device().allocateDescriptorSets({
            .descriptorPool     = descriptor_pool_,
            .descriptorSetCount = frames_in_flight,
            .pSetLayouts        = set_layouts.data(),
        }),
        "allocate grass descriptor sets"
    );
    for (uint32 frame = 0; frame < frames_in_flight; ++frame) {
        frames_[frame].set = sets[frame];
        ensure_frame_buffers_(frames_[frame], initial_instances);
    }
}

grass_renderer::~grass_renderer() {
    const vk::Device device = context_->get_device();
    for (auto& frame : frames_) {
        if (frame.set != nullptr) {
            static_cast<void>(device.freeDescriptorSets(descriptor_pool_, frame.set));
        }
    }
    device.destroyPipeline(pipeline_);
    device.destroyPipelineLayout(pipeline_layout_);
    device.destroyDescriptorSetLayout(set_layout_);
}

auto grass_renderer::create_pipeline_(
    vk::RenderPass render_pass, vk::SampleCountFlagBits samples,
    const grass_pipeline_layouts& layouts, const vk::PipelineShaderStageCreateInfo& fragment_stage
) -> void {
    const std::array<vk::DescriptorSetLayout, 7> set_layouts{
        layouts.uniform, set_layout_, layouts.shadow, layouts.lights, layouts.palette,
        layouts.occupancy, layouts.light_cache
    };
    const vk::PushConstantRange push_range{
        .stageFlags = vk::ShaderStageFlagBits::eVertex,
        .offset     = 0,
        .size       = sizeof(grass_push_constants),
    };
    pipeline_layout_ = vk_must(
        context_->get_device().createPipelineLayout({
            .setLayoutCount         = static_cast<uint32>(set_layouts.size()),
            .pSetLayouts            = set_layouts.data(),
            .pushConstantRangeCount = 1,
            .pPushConstantRanges    = &push_range,
        }),
        "create grass pipeline layout"
    );

    const std::array stages{vertex_shader_->get_stage_info(), fragment_stage};

    const vk::PipelineVertexInputStateCreateInfo vertex_input{};
    const vk::PipelineInputAssemblyStateCreateInfo input_assembly{
        .topology = vk::PrimitiveTopology::eTriangleList,
    };
    const vk::PipelineViewportStateCreateInfo viewport_state{
        .viewportCount = 1,
        .scissorCount  = 1,
    };
    const std::array dynamic_states{vk::DynamicState::eViewport, vk::DynamicState::eScissor};
    const vk::PipelineDynamicStateCreateInfo dynamic_state{
        .dynamicStateCount = static_cast<uint32>(dynamic_states.size()),
        .pDynamicStates    = dynamic_states.data(),
    };
    const vk::PipelineRasterizationStateCreateInfo rasterizer{
        .polygonMode = vk::PolygonMode::eFill,
        .cullMode    = vk::CullModeFlagBits::eBack,
        .frontFace   = vk::FrontFace::eCounterClockwise,
        .lineWidth   = 1.0F,
    };
    const vk::PipelineMultisampleStateCreateInfo multisampling{
        .rasterizationSamples = samples,
    };
    const vk::PipelineColorBlendAttachmentState blend_attachment{
        .colorWriteMask = vk::ColorComponentFlagBits::eR | vk::ColorComponentFlagBits::eG |
                          vk::ColorComponentFlagBits::eB | vk::ColorComponentFlagBits::eA,
    };
    const vk::PipelineColorBlendStateCreateInfo color_blending{
        .attachmentCount = 1,
        .pAttachments    = &blend_attachment,
    };
    const vk::PipelineDepthStencilStateCreateInfo depth_stencil{
        .depthTestEnable  = vk::True,
        .depthWriteEnable = vk::True,
        .depthCompareOp   = vk::CompareOp::eGreater,
    };

    pipeline_ = vk_must(
        context_->get_device().createGraphicsPipeline(
            nullptr,
            {
                .stageCount          = static_cast<uint32>(stages.size()),
                .pStages             = stages.data(),
                .pVertexInputState   = &vertex_input,
                .pInputAssemblyState = &input_assembly,
                .pViewportState      = &viewport_state,
                .pRasterizationState = &rasterizer,
                .pMultisampleState   = &multisampling,
                .pDepthStencilState  = &depth_stencil,
                .pColorBlendState    = &color_blending,
                .pDynamicState       = &dynamic_state,
                .layout              = pipeline_layout_,
                .renderPass          = render_pass,
                .subpass             = 0,
            }
        ),
        "create grass pipeline"
    );
}

auto grass_renderer::ensure_frame_buffers_(
    frame_buffers& buffers, uint32 instances
) -> void {
    bool rebound = false;

    if (instances > buffers.instance_capacity) {
        uint32 capacity = std::max(buffers.instance_capacity, initial_instances);
        while (capacity < instances) {
            capacity *= 2;
        }
        buffers.instances = std::make_unique<storage_buffer>(
            *context_, static_cast<vk::DeviceSize>(capacity) * sizeof(grass_instance)
        );
        buffers.instance_capacity = capacity;
        rebound                   = true;
    }

    const auto wanted_quads = static_cast<uint32>(quads_.size());
    if (buffers.quads == nullptr || wanted_quads > buffers.quad_capacity) {
        uint32 capacity = std::max(buffers.quad_capacity, initial_quads);
        while (capacity < wanted_quads) {
            capacity *= 2;
        }
        buffers.quads = std::make_unique<storage_buffer>(
            *context_, static_cast<vk::DeviceSize>(capacity) * sizeof(quad)
        );
        buffers.quad_capacity = capacity;
        buffers.quads_written = std::numeric_limits<uint64>::max();
        rebound               = true;
    }

    if (buffers.quads_written != quads_revision_) {
        buffers.quads->copy_from(quads_.data(), quads_.size() * sizeof(quad));
        buffers.quads_written = quads_revision_;
    }

    if (!rebound) {
        return;
    }

    const std::array infos{
        vk::DescriptorBufferInfo{
            .buffer = buffers.instances->get_buffer(), .offset = 0, .range = vk::WholeSize
        },
        vk::DescriptorBufferInfo{
            .buffer = buffers.quads->get_buffer(), .offset = 0, .range = vk::WholeSize
        },
    };
    std::array<vk::WriteDescriptorSet, 2> writes{};
    for (uint32 i = 0; i < writes.size(); ++i) {
        writes[i] = {
            .dstSet          = buffers.set,
            .dstBinding      = i,
            .descriptorCount = 1,
            .descriptorType  = vk::DescriptorType::eStorageBuffer,
            .pBufferInfo     = &infos[i],
        };
    }
    context_->get_device().updateDescriptorSets(writes, nullptr);
}

auto grass_renderer::mesh_for_(
    ecs::world& world, uint8 form, voxel look
) -> uint16 {
    const auto kind = static_cast<uint16>((static_cast<uint32>(form) << 8U) | look.value);
    if (const auto it = mesh_by_kind_.find(kind); it != mesh_by_kind_.end()) {
        return it->second;
    }

    const auto tuft = ecs::grow_grass_tuft(world.resource<asset::model_registry>(), form, look);
    const mesh built = greedy_mesh_generator::generate_mesh_data(mesh_scratch_, mesh_source{.voxels = *tuft});

    const auto count = static_cast<uint32>(std::min<std::size_t>(built.quads.size(), max_tuft_quads));
    const tuft_mesh entry{.quad_offset = static_cast<uint32>(quads_.size()), .quad_count = count};
    quads_.insert(quads_.end(), built.quads.begin(), built.quads.begin() + count);
    ++quads_revision_;

    const auto index = static_cast<uint16>(meshes_.size());
    meshes_.push_back(entry);
    mesh_by_kind_.emplace(kind, index);
    return index;
}

auto grass_renderer::refresh_chunk_(
    ecs::world& world, const ecs::world_grid& grid, vec3i coord, const ecs::chunk& c,
    chunk_grass& entry
) -> void {
    const auto& volume = *c.get_volume();
    const auto vs      = static_cast<float32>(grid.world_units_per_voxel());
    const vec3i base   = coord * column_side;

    const ecs::chunk* above = grid.find_chunk(coord + vec3i{0, 1, 0});
    const asset::chunk_volume* above_volume = above != nullptr ? above->get_volume().get() : nullptr;
    constexpr auto full = static_cast<float32>(ecs::light_column::max_level);

    const auto level = [](const asset::light_field* field, vec3i at, uint8 fallback) -> float32 {
        return static_cast<float32>(field != nullptr ? field->level_at(at) : fallback);
    };

    entry.instances.clear();
    volume.cover().for_each([&](const asset::cover_layer::entry& e) {
        const voxel look = c.get_voxel(e.support);
        if (look.is_empty()) {
            return;
        }
        const uint16 mesh = mesh_for_(world, e.form, look);

        const vec3i cell = base + e.support + vec3i{0, 1, 0};
        const uint64 h   = cell_hash(cell);
        const auto turns    = static_cast<float32>(h & 3U);
        const float32 angle = turns * quarter_turn;
        const float32 fit   = vs / static_cast<float32>(ecs::grass_tuft_footprint);

        const vec3f centre{
            (static_cast<float32>(cell.x) + 0.5F) * vs, 0.0F, (static_cast<float32>(cell.z) + 0.5F) * vs
        };

        const bool inside = e.support.y + 1 < column_side;
        const asset::chunk_volume* holder = inside ? &volume : above_volume;
        const vec3i at{e.support.x, inside ? e.support.y + 1 : 0, e.support.z};
        const ecs::world_light light{
            .sky   = level(holder != nullptr ? holder->get_sky_light() : nullptr, at, ecs::light_column::max_level) / full,
            .block = level(holder != nullptr ? holder->get_block_light() : nullptr, at, 0) / full,
        };

        entry.instances.emplace_back(
            mesh,
            grass_instance{
                .place = vec4f{centre.x, static_cast<float32>(cell.y) * vs, centre.z, angle},
                .light = vec4f{1.0F - light.sky, light.block, unit_of(h, 1) * 6.2831853F, fit},
            }
        );
    });

    std::ranges::sort(entry.instances, {}, &std::pair<uint16, grass_instance>::first);

    entry.revision  = volume.cover().revision();
    entry.sky       = volume.get_sky_light();
    entry.block     = volume.get_block_light();
    entry.sky_above = above_volume != nullptr ? above_volume->get_sky_light() : nullptr;
}

auto grass_renderer::prepare(
    ecs::world& world, const camera& camera, const grass_settings& settings, const wind_settings& wind,
    float32 wind_time, uint32 frame
) -> void {
    ++frame_number_;
    draws_.clear();
    stats_ = {};

    const ecs::world_grid* grid = world.system<ecs::world_grid_system>().grid();
    if (!settings.enabled || grid == nullptr || settings.radius_columns <= 0) {
        chunks_.clear();
        return;
    }

    const auto vs           = static_cast<float32>(grid->world_units_per_voxel());
    const float32 col_units = static_cast<float32>(column_side) * vs;
    const float32 fade_end  = static_cast<float32>(settings.radius_columns) * col_units;
    const float32 fade_from = fade_end * std::clamp(settings.fade_share, 0.0F, 0.99F);

    const vec3f eye = camera.get_position();
    const int32 ex  = floor_div(eye.x, col_units);
    const int32 ez  = floor_div(eye.z, col_units);
    const int32 r   = settings.radius_columns;

    std::vector<const chunk_grass*> visible;
    int32 refreshes = 0;

    for (int32 dx = -r; dx <= r; ++dx) {
        for (int32 dz = -r; dz <= r; ++dz) {
            const vec2i column{ex + dx, ez + dz};
            const float32 x0 = static_cast<float32>(column.x) * col_units;
            const float32 z0 = static_cast<float32>(column.y) * col_units;
            const float32 nx = std::clamp(eye.x, x0, x0 + col_units) - eye.x;
            const float32 nz = std::clamp(eye.z, z0, z0 + col_units) - eye.z;
            if ((nx * nx) + (nz * nz) > fade_end * fade_end) {
                continue;
            }

            for (const int32 cy : grid->column_levels(column)) {
                const vec3i coord{column.x, cy, column.y};
                const ecs::chunk* c = grid->find_chunk(coord);
                if (c == nullptr || c->get_volume()->cover().empty()) {
                    continue;
                }
                const auto& volume = *c->get_volume();

                auto& entry = chunks_[coord];
                const ecs::chunk* above = grid->find_chunk(coord + vec3i{0, 1, 0});
                const asset::light_field* sky_above =
                    above != nullptr ? above->get_volume()->get_sky_light() : nullptr;
                const bool built = entry.seen != 0;
                const bool stale = !built || entry.revision != volume.cover().revision() ||
                                   entry.sky != volume.get_sky_light() ||
                                   entry.block != volume.get_block_light() || entry.sky_above != sky_above;
                if (stale && refreshes < refreshes_per_frame) {
                    refresh_chunk_(world, *grid, coord, *c, entry);
                    ++refreshes;
                } else if (!built) {
                    chunks_.erase(coord);
                    continue;
                }
                entry.seen = frame_number_;

                const vec3f low{x0, static_cast<float32>(cy * column_side) * vs, z0};
                const spatial::aabb bounds{
                    low, low + vec3f{col_units, col_units + vs, col_units}
                };
                if (!camera.get_frustum().intersects(bounds)) {
                    continue;
                }
                visible.push_back(&entry);
            }
        }
    }

    std::erase_if(chunks_, [this](const auto& item) { return item.second.seen != frame_number_; });

    mesh_counts_.assign(meshes_.size(), 0);
    uint32 total = 0;
    for (const chunk_grass* entry : visible) {
        for (const auto& [mesh, instance] : entry->instances) {
            ++mesh_counts_[mesh];
            ++total;
        }
    }

    auto& buffers = frames_[frame];
    ensure_frame_buffers_(buffers, std::max(total, 1U));

    std::vector<uint32> cursor(meshes_.size(), 0);
    uint32 running = 0;
    for (uint32 m = 0; m < meshes_.size(); ++m) {
        cursor[m] = running;
        if (mesh_counts_[m] > 0) {
            draws_.push_back({.mesh = static_cast<uint16>(m), .first_instance = running, .count = mesh_counts_[m]});
        }
        running += mesh_counts_[m];
    }

    auto* out = static_cast<grass_instance*>(buffers.instances->map());
    for (const chunk_grass* entry : visible) {
        for (const auto& [mesh, instance] : entry->instances) {
            out[cursor[mesh]++] = instance;
        }
    }

    const vec2f dir   = wind.direction;
    const float32 len = std::max(std::sqrt((dir.x * dir.x) + (dir.y * dir.y)), 0.0001F);

    push_.wind  = vec4f{dir.x / len, dir.y / len, wind.grass_bend, wind_time};
    push_.eye   = vec4f{eye.x, eye.y, eye.z, fade_from};
    push_.shape = vec4f{
        fade_end, static_cast<float32>(ecs::grass_tuft_footprint) * 0.5F,
        static_cast<float32>(ecs::grass_tuft_max_height), wind.speed
    };

    stats_.instances = total;
    stats_.meshes    = static_cast<uint32>(meshes_.size());
    stats_.draws     = static_cast<uint32>(draws_.size());
    stats_.chunks    = static_cast<uint32>(visible.size());
}

auto grass_renderer::draw(
    vk::CommandBuffer cmd, uint32 frame, const grass_bound_sets& sets
) -> void {
    if (draws_.empty()) {
        return;
    }

    cmd.bindPipeline(vk::PipelineBindPoint::eGraphics, pipeline_);
    const std::array bound{
        sets.uniform, frames_[frame].set, sets.shadow, sets.lights, sets.palette, sets.occupancy,
        sets.light_cache
    };
    cmd.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, pipeline_layout_, 0, bound, nullptr);
    cmd.pushConstants(
        pipeline_layout_, vk::ShaderStageFlagBits::eVertex, 0, sizeof(grass_push_constants), &push_
    );
    cmd.bindIndexBuffer(indices_->get_buffer(), 0, vk::IndexType::eUint32);

    for (const mesh_draw& d : draws_) {
        const tuft_mesh& m = meshes_[d.mesh];
        if (m.quad_count == 0) {
            continue;
        }
        cmd.drawIndexed(m.quad_count * 6, d.count, 0, static_cast<int32>(m.quad_offset * 4), d.first_instance);
    }
}

}  // namespace vw::gfx
