module vw.gfx;

import std;
import vulkan;
import vw.core;
import vw.asset;
import vw.ecs;
import vw.world;
import vw.platform;
import :vk;

namespace vw::gfx {

namespace {

auto key_of(entity ent) -> instance_key {
    return instance_key{.id = ent.index, .generation = ent.generation};
}

auto entity_of(instance_key key) -> entity {
    return entity{.index = key.id, .generation = key.generation};
}

auto sorted_erase(std::vector<entity>& v, entity e) -> void {
    auto it = std::lower_bound(v.begin(), v.end(), e);
    if (it != v.end() && *it == e) {
        v.erase(it);
    }
}

template <typename Iter>
auto sorted_merge_range(
    std::vector<entity>& dst, Iter first, Iter last
) -> void {
    if (first == last) return;
    auto old_size = static_cast<std::ptrdiff_t>(dst.size());
    dst.insert(dst.end(), first, last);
    std::sort(dst.begin() + old_size, dst.end());
    std::inplace_merge(dst.begin(), dst.begin() + old_size, dst.end());
    dst.erase(std::unique(dst.begin(), dst.end()), dst.end());
}

}  // namespace

combined_buffer_pool::combined_buffer_pool(
    vulkan_context& context,
    deletion_queue& deletion,
    vk::DescriptorPool descriptor_pool,
    vk::DescriptorSetLayout descriptor_set_layout,
    vk::DescriptorSetLayout compute_descriptor_set_layout,
    model_occupancy_buffer& model_volumes
)
    : context_(&context)
    , model_volumes_(&model_volumes)
    , deletion_(&deletion)
    , staging_(context, 32 * 1024 * 1024)
    , descriptor_pool_(descriptor_pool)
    , descriptor_set_layout_(descriptor_set_layout)
    , compute_descriptor_set_layout_(compute_descriptor_set_layout) {}

auto combined_buffer_pool::release_volume_(entity_buffer_info& info) -> void {
    if (info.volume_model == asset::model_identity::invalid_index) {
        return;
    }
    model_volumes_->release(info.volume_model);
    info.volume_model = asset::model_identity::invalid_index;
}

auto combined_buffer_pool::track(
    world_type& world
) -> void {
    const auto& destroyed = world.destroyed();
    destroyed_pending_entities_.insert(destroyed_pending_entities_.end(), destroyed.begin(), destroyed.end());

    const auto& model_changed = world.changed<model_component>();
    sorted_merge_range(mesh_pending_entities_, model_changed.begin(), model_changed.end());

    const auto& transform_changed = world.changed<transform_component>();
    sorted_merge_range(
        transform_pending_entities_, transform_changed.begin(), transform_changed.end());
}

auto combined_buffer_pool::mark_mesh_ready(
    entity ent
) -> void {
    const auto it = std::lower_bound(mesh_pending_entities_.begin(), mesh_pending_entities_.end(), ent);
    if (it == mesh_pending_entities_.end() || *it != ent) {
        mesh_pending_entities_.insert(it, ent);
    }
}

auto combined_buffer_pool::update(
    world_type& world,
    const camera& camera,
    vk::CommandBuffer cmd,
    mesh_pool& pool
) -> void {
    staging_.begin_frame();
    touched_bounds_.clear();

    stats_.timing.destroyed_ms = measure_ms([&] {
        process_destroyed_(world);
    });

    stats_.timing.meshes_ms = measure_ms([&] {
        update_meshes_(world, camera.get_position(), pool);
    });

    stats_.timing.transforms_ms = measure_ms([&] {
        update_transforms_(world);
    });

    stats_.timing.light_ms = measure_ms([&] {
        update_instance_light_(world);
    });

    stats_.chunk_cull.walk_ms = measure_ms([&] {
        update_chunk_visibility_(world, camera.get_position(), !camera.is_orthographic());
    });

    stats_.timing.staging_flush_ms = measure_ms([&] {
        staging_.flush(cmd);
    });
}

const std::vector<std::unique_ptr<combined_buffer>>& combined_buffer_pool::get_buffers() const {
    return buffers_;
}

auto combined_buffer_pool::process_destroyed_(world_type& world) -> void {
    for (auto ent : destroyed_pending_entities_) {
        hidden_entities_.erase(ent);
        if (auto* info = entity_buffer_infos_.get(ent)) {
            touched_bounds_.push_back(info->bounds);
            release_volume_(*info);
            if (const auto swapped = buffers_[info->buffer_index]->free(key_of(ent))) {
                rewrite_swapped_(world, info->buffer_index, entity_of(*swapped));
            }
            entity_buffer_infos_.remove(ent);
        }
        chunk_links_.erase(ent);
        sorted_erase(mesh_pending_entities_, ent);
        sorted_erase(transform_pending_entities_, ent);
    }
    destroyed_pending_entities_.clear();
}

auto combined_buffer_pool::get_chunk_size_for_mesh(
    uint32 quad_count
) -> buffer_chunk_size {
    uint32 chunk = 64;
    while (chunk < quad_count) {
        chunk += (chunk + 1) / 2;
    }

    return buffer_chunk_size{chunk};
}

auto combined_buffer_pool::get_index_buffer() const -> vk::Buffer {
    return index_buffer_ ? index_buffer_->get_buffer() : nullptr;
}

auto combined_buffer_pool::ensure_index_pattern_(uint32 quads) -> void {
    if (index_buffer_ && quads <= index_quads_) {
        return;
    }

    index_quads_ = quads;

    std::vector<uint32> pattern;
    pattern.reserve(static_cast<std::size_t>(quads) * 6);
    for (uint32 i = 0; i < quads; ++i) {
        const uint32 base = i * 4;
        pattern.insert(pattern.end(), {base, base + 1, base + 2, base + 2, base + 3, base});
    }

    const auto bytes = pattern.size() * sizeof(uint32);

    if (index_upload_) {
        deletion_->retire(std::move(index_upload_));
    }

    index_upload_ = std::make_unique<index_buffer>(*context_, bytes);
    index_upload_->copy_from_vector(pattern);

    auto buffer = std::make_unique<device_index_buffer>(*context_, bytes);
    staging_.copy_buffer(index_upload_->get_buffer(), 0, buffer->get_buffer(), 0, bytes);

    if (index_buffer_) {
        deletion_->retire(std::move(index_buffer_));
    }
    index_buffer_ = std::move(buffer);
}

auto combined_buffer_pool::get_or_create_buffer(
    const buffer_chunk_size& chunk_size
) -> combined_buffer* {
    ensure_index_pattern_(chunk_size.quad_count);

    if (chunk_size_to_buffer_index_.contains(chunk_size)) {
        auto buffer_index = chunk_size_to_buffer_index_[chunk_size];
        return buffers_[buffer_index].get();
    }

    auto buffer_index = buffers_.size();
    buffers_.push_back(
        std::make_unique<combined_buffer>(
            *context_, chunk_size, descriptor_pool_, descriptor_set_layout_,
            compute_descriptor_set_layout_, staging_, *deletion_
        )
    );
    chunk_size_to_buffer_index_[chunk_size] = buffer_index;

    return buffers_[buffer_index].get();
}

auto combined_buffer_pool::mesh_write_budget() const -> uint32 {
    constexpr uint32 estimated_avg_mesh_cost = 32 * 1024;
    constexpr uint32 min_writes              = 4;
    constexpr uint32 max_writes              = 16;

    return std::clamp(
        static_cast<uint32>(staging_.frame_capacity() / estimated_avg_mesh_cost),
        min_writes,
        max_writes
    );
}

auto combined_buffer_pool::update_meshes_(
    world_type& world, const vec3f& camera_pos, mesh_pool& pool
) -> void {
    sort_keys_.clear();
    sort_keys_.reserve(mesh_pending_entities_.size());
    for (entity ent : mesh_pending_entities_) {
        float32 dist_sq = 0.0f;
        if (world.has<spatial_component>(ent)) {
            const auto diff = world.get<spatial_component>(ent).get_bounds().center() - camera_pos;
            dist_sq = (diff.x * diff.x) + (diff.y * diff.y) + (diff.z * diff.z);
        }
        sort_keys_.emplace_back(dist_sq, ent);
    }

    std::ranges::sort(sort_keys_, {}, &std::pair<float32, entity>::first);

    entities_to_process_.clear();
    entities_to_process_.reserve(sort_keys_.size());
    for (const auto& [dist_sq, ent] : sort_keys_) {
        entities_to_process_.push_back(ent);
    }

    const uint32 max_mesh_writes = mesh_write_budget();
    uint32 mesh_writes           = 0;

    merge_buffer_.clear();
    merge_buffer_.reserve(entities_to_process_.size());
    uploaded_models_.clear();

    for (std::size_t i = 0; i < entities_to_process_.size(); ++i) {
        entity ent = entities_to_process_[i];

        if (mesh_writes >= max_mesh_writes) {
            merge_buffer_.insert(
                merge_buffer_.end(),
                entities_to_process_.begin() + static_cast<std::ptrdiff_t>(i),
                entities_to_process_.end());
            break;
        }

        const bool has_model     = world.has<model_component>(ent);
        const bool has_transform = world.has<transform_component>(ent);

        if (!has_model || !has_transform) {
            hidden_entities_.erase(ent);

            if (auto* buffer_info = entity_buffer_infos_.get(ent)) {
                release_volume_(*buffer_info);
                if (const auto swapped = buffers_[buffer_info->buffer_index]->free(key_of(ent))) {
                    rewrite_swapped_(world, buffer_info->buffer_index, entity_of(*swapped));
                }
                entity_buffer_infos_.remove(ent);
            }
            continue;
        }

        const auto& model_comp = world.get<model_component>(ent);
        if (!model_comp.has_model()) {
            merge_buffer_.push_back(ent);
            continue;
        }

        auto model_id    = model_comp.get_identity();
        const int32 step = entity_lod_step(model_comp);
        auto mesh_ptr    = pool.get(model_id, step);
        if (!mesh_ptr) {
            merge_buffer_.push_back(ent);
            continue;
        }

        chunk_links_[ent] = mesh_ptr->links;

        const auto quad_count = static_cast<uint32>(mesh_ptr->quads.size());

        if (quad_count == 0) {
            continue;
        }

        buffer_chunk_size required_chunk_size = get_chunk_size_for_mesh(quad_count);

        const vk::DeviceSize mesh_staging_cost = (quad_count * sizeof(quad)) + sizeof(uint32) +
            sizeof(draw_command) + (sizeof(mat4f) * 2);

        const auto& transform_comp = world.get<transform_component>(ent);
        const mat4f transform_matrix =
            model_matrix(transform_comp, world.get<model_component>(ent));

        vw::spatial::aabb ent_bounds{};
        if (world.has<spatial_component>(ent)) {
            ent_bounds = world.get<spatial_component>(ent).get_bounds();
        }

        if (auto* existing = entity_buffer_infos_.get(ent)) {
            auto& buffer_info = *existing;
            auto& buffer      = buffers_[buffer_info.buffer_index];

            if (buffer_info.chunk_size == required_chunk_size) {
                const auto& ent_alloc = buffer->get_allocation(key_of(ent));
                if (ent_alloc.key == mesh_key_of(model_id, mesh_ptr->lod_step)) {
                    if (staging_.available() < mesh_staging_cost) {
                        merge_buffer_.push_back(ent);
                        continue;
                    }
                    buffer->write_mesh(model_id, *mesh_ptr);
                    if (buffer_info.volume_model != asset::model_identity::invalid_index) {
                        const auto fresh = model_volumes_->refresh(*model_comp.get_model());
                        if (fresh != buffer_info.corners) {
                            buffer_info.corners = fresh;
                            buffer->write_transform(
                                key_of(ent), transform_matrix, ent_bounds,
                                instance_shading{.light = buffer_info.light, .corners = fresh}
                            );
                        }
                    }
                    uploaded_models_.emplace_back(model_id, step);
                    touched_bounds_.push_back(buffer_info.bounds);
                    touched_bounds_.push_back(ent_bounds);
                    buffer_info.bounds = ent_bounds;
                    ++mesh_writes;
                    continue;
                }
            }

            if (staging_.available() < mesh_staging_cost) {
                merge_buffer_.push_back(ent);
                continue;
            }

            touched_bounds_.push_back(buffer_info.bounds);
            release_volume_(buffer_info);

            if (const auto swapped = buffer->free(key_of(ent))) {
                rewrite_swapped_(world, buffer_info.buffer_index, entity_of(*swapped));
            }
        } else if (staging_.available() < mesh_staging_cost) {
            merge_buffer_.push_back(ent);
            continue;
        }

        auto* buffer            = get_or_create_buffer(required_chunk_size);
        const auto buffer_index = chunk_size_to_buffer_index_[required_chunk_size];

        const bool on_world_grid = model_comp.get_chunk() != nullptr;
        const auto corners       = on_world_grid
            ? instance_corners::on_world_grid()
            : model_volumes_->acquire(*model_comp.get_model());

        buffer->allocate(
            key_of(ent), model_id, *mesh_ptr, transform_matrix, ent_bounds, corners
        );
        uploaded_models_.emplace_back(model_id, step);

        entity_buffer_infos_.emplace(
            ent,
            entity_buffer_info{
                .chunk_size   = required_chunk_size,
                .buffer_index = buffer_index,
                .bounds       = ent_bounds,
                .lit_by_world = !on_world_grid,
                .corners      = corners,
                .volume_model = corners.has_volume() ? model_id.index
                                                     : asset::model_identity::invalid_index,
            }
        );
        touched_bounds_.push_back(ent_bounds);
        ++mesh_writes;
    }

    std::sort(merge_buffer_.begin(), merge_buffer_.end());
    mesh_pending_entities_.swap(merge_buffer_);
    stats_.mesh_pending = static_cast<uint32>(mesh_pending_entities_.size());

    evict_uploaded_(world, pool);
}

auto combined_buffer_pool::evict_uploaded_(
    world_type& world, mesh_pool& pool
) -> void {
    if (uploaded_models_.empty()) {
        return;
    }

    awaited_models_.clear();
    for (entity ent : mesh_pending_entities_) {
        if (!world.has<model_component>(ent)) {
            continue;
        }
        const auto& model_comp = world.get<model_component>(ent);
        if (model_comp.has_model()) {
            awaited_models_.insert(model_comp.get_identity());
        }
    }

    for (const auto& [model_id, step] : uploaded_models_) {
        if (!awaited_models_.contains(model_id)) {
            pool.evict(model_id, step);
        }
    }
}

auto combined_buffer_pool::hide_marked_() -> void {
    for (const auto ent : hidden_entities_) {
        const auto* info = entity_buffer_infos_.get(ent);
        if (info == nullptr) {
            continue;
        }

        const auto buffer = info->buffer_index;
        const auto slot   = buffers_[buffer]->get_allocation(key_of(ent)).instance_index;
        visibility_flags_[buffer][slot] = 0U;
    }
}

auto combined_buffer_pool::update_chunk_visibility_(
    world_type& world, const vec3f& camera_pos, bool sight_starts_at_a_point
) -> void {
    visibility_flags_.resize(buffers_.size());
    for (std::size_t i = 0; i < buffers_.size(); ++i) {
        visibility_flags_[i].assign(buffers_[i]->get_stats().instance_capacity, 1U);
    }

    hide_marked_();

    stats_.chunk_cull = chunk_cull_stats{};

    auto* grid = world.system<ecs::world_grid_system>().grid();
    if (!chunk_cull_enabled_ || grid == nullptr || !sight_starts_at_a_point) {
        for (std::size_t i = 0; i < buffers_.size(); ++i) {
            buffers_[i]->write_visibility(visibility_flags_[i]);
        }
        return;
    }

    const auto slot_of = [&](entity ent) -> std::optional<std::pair<std::size_t, uint32>> {
        if (!ent.is_valid()) {
            return std::nullopt;
        }
        const auto* info = entity_buffer_infos_.get(ent);
        if (info == nullptr) {
            return std::nullopt;
        }
        const auto index =
            buffers_[info->buffer_index]->get_allocation(key_of(ent)).instance_index;
        return std::pair{info->buffer_index, index};
    };

    vec3i lo{std::numeric_limits<int32>::max(), std::numeric_limits<int32>::max(),
             std::numeric_limits<int32>::max()};
    vec3i hi{std::numeric_limits<int32>::lowest(), std::numeric_limits<int32>::lowest(),
             std::numeric_limits<int32>::lowest()};

    column_top_.clear();

    grid->for_each_chunk([&](vec3i coord, const ecs::chunk& c) {
        const auto ent = c.get_entity();

        const vec2i column{coord.x, coord.z};
        if (const auto it = column_top_.find(column);
            it == column_top_.end() || it->second < coord.y) {
            column_top_[column] = coord.y;
        }

        lo = vec3i{std::min(lo.x, coord.x), std::min(lo.y, coord.y), std::min(lo.z, coord.z)};
        hi = vec3i{std::max(hi.x, coord.x), std::max(hi.y, coord.y), std::max(hi.z, coord.z)};

        if (const auto it = chunk_links_.find(ent); it != chunk_links_.end()) {
            ++stats_.chunk_cull.known_links;
            if (it->second.is_sealed()) {
                ++stats_.chunk_cull.sealed;
            }
            for (const auto& cell : it->second.cells) {
                if (cell.merged) {
                    ++stats_.chunk_cull.merged;
                }
                stats_.chunk_cull.max_pockets = std::max(
                    stats_.chunk_cull.max_pockets, static_cast<uint32>(cell.pockets.size())
                );
            }
        }

        if (const auto slot = slot_of(ent)) {
            ++stats_.chunk_cull.chunks;
            visibility_flags_[slot->first][slot->second] = 0U;
        }
    });

    if (stats_.chunk_cull.chunks == 0) {
        for (std::size_t i = 0; i < buffers_.size(); ++i) {
            buffers_[i]->write_visibility(visibility_flags_[i]);
        }
        return;
    }

    const vec3i camera_voxel{
        static_cast<int32>(std::floor(camera_pos.x)),
        static_cast<int32>(std::floor(camera_pos.y)),
        static_cast<int32>(std::floor(camera_pos.z)),
    };
    const vec3i camera_chunk = grid->world_to_chunk_coord(camera_voxel);

    lo.y = std::min(lo.y, camera_chunk.y);
    hi.y = std::max(hi.y + 1, camera_chunk.y);

    constexpr int32 per_side = vw::asset::chunk_links::cells_per_side;
    constexpr int32 cell_voxels =
        vw::asset::chunk_links::cell_size * 1;

    const auto to_chunk = [](int32 cell) -> int32 {
        return cell >= 0 ? cell / per_side : (cell - per_side + 1) / per_side;
    };
    const auto to_sub = [&to_chunk](int32 cell) -> int32 {
        return cell - (to_chunk(cell) * per_side);
    };

    const vec3i cell_lo{lo.x * per_side, lo.y * per_side, lo.z * per_side};
    const vec3i cell_hi{
        ((hi.x + 1) * per_side) - 1, ((hi.y + 1) * per_side) - 1,
        ((hi.z + 1) * per_side) - 1};

    const int32 scaled_cell = cell_voxels * grid->world_units_per_voxel();
    const auto cell_of      = [scaled_cell](int32 world) -> int32 {
        return world >= 0 ? world / scaled_cell : (world - scaled_cell + 1) / scaled_cell;
    };
    const vec3i origin{
        cell_of(camera_voxel.x), cell_of(camera_voxel.y), cell_of(camera_voxel.z)};

    const bool origin_in_frame = origin.x >= cell_lo.x && origin.y >= cell_lo.y &&
        origin.z >= cell_lo.z && origin.x <= cell_hi.x && origin.y <= cell_hi.y &&
        origin.z <= cell_hi.z;
    if (!origin_in_frame) {
        stats_.chunk_cull.visible = stats_.chunk_cull.chunks;
        for (std::size_t i = 0; i < buffers_.size(); ++i) {
            std::ranges::fill(visibility_flags_[i], 1U);
            buffers_[i]->write_visibility(visibility_flags_[i]);
        }
        return;
    }

    ecs::walk_visible_chunks(
        chunk_walk_scratch_, origin, cell_lo, cell_hi,
        [&](vec3i cell) -> vw::ecs::cell_lookup {
            static const vw::asset::cell_links sealed{};

            auto* c = grid->get_chunk(
                vec3i{to_chunk(cell.x), to_chunk(cell.y), to_chunk(cell.z)});
            if (c == nullptr) {
                return vw::ecs::cell_lookup::nothing_placed();
            }
            if (c->is_solid()) {
                return &sealed;
            }
            const auto it = chunk_links_.find(c->get_entity());
            if (it == chunk_links_.end()) {
                return vw::ecs::cell_lookup::placed_without_links();
            }
            return &it->second.cells[vw::asset::chunk_links::cell_index(
                to_sub(cell.x), to_sub(cell.y), to_sub(cell.z))];
        },
        [&, world_top = cell_hi.y](vec3i cell) -> bool {
            const auto it = column_top_.find(vec2i{to_chunk(cell.x), to_chunk(cell.z)});
            if (it == column_top_.end()) {
                return cell.y >= world_top;
            }
            return to_chunk(cell.y) > it->second;
        },
        [&](const vw::asset::chunk_pocket& pocket) -> bool {
            const int32 voxels = grid->world_units_per_voxel();
            const auto local   = [&](int32 world, int32 cell) -> int32 {
                return (world / voxels) - (cell * vw::asset::chunk_links::cell_size);
            };
            return pocket.holds(
                local(camera_voxel.x, origin.x), local(camera_voxel.y, origin.y),
                local(camera_voxel.z, origin.z),
                vw::asset::chunk_links::cell_size / vw::asset::chunk_pocket::volume_span
            );
        },
        [&](vec3i cell) {
            ++stats_.chunk_cull.visited;

            auto* c = grid->get_chunk(
                vec3i{to_chunk(cell.x), to_chunk(cell.y), to_chunk(cell.z)});
            if (c == nullptr) {
                ++stats_.chunk_cull.visited_empty;
                return;
            }
            if (const auto slot = slot_of(c->get_entity())) {
                if (visibility_flags_[slot->first][slot->second] == 0U) {
                    ++stats_.chunk_cull.visible;
                    visibility_flags_[slot->first][slot->second] = 1U;
                }
            }
        }
    );

    for (std::size_t i = 0; i < buffers_.size(); ++i) {
        buffers_[i]->write_visibility(visibility_flags_[i]);
    }
}

auto combined_buffer_pool::update_transforms_(
    world_type& world
) -> void {
    entities_to_process_.assign(
        transform_pending_entities_.begin(), transform_pending_entities_.end());

    merge_buffer_.clear();
    merge_buffer_.reserve(entities_to_process_.size());

    for (std::size_t i = 0; i < entities_to_process_.size(); ++i) {
        entity ent = entities_to_process_[i];

        if (world.has<model_component>(ent) && !world.get<model_component>(ent).is_visible()) {
            hidden_entities_.insert(ent);
        } else {
            hidden_entities_.erase(ent);
        }

        auto* stored_info = entity_buffer_infos_.get(ent);
        if (stored_info == nullptr) {
            continue;
        }

        const bool has_model     = world.has<model_component>(ent);
        const bool has_transform = world.has<transform_component>(ent);
        if (!has_model || !has_transform) {
            continue;
        }

        if (staging_.available() < sizeof(mat4f) * 2) {
            merge_buffer_.insert(
                merge_buffer_.end(),
                entities_to_process_.begin() + static_cast<std::ptrdiff_t>(i),
                entities_to_process_.end());
            break;
        }

        auto& info = *stored_info;
        const auto& transform_comp = world.get<transform_component>(ent);
        vw::spatial::aabb tr_bounds{};
        if (world.has<spatial_component>(ent)) {
            tr_bounds = world.get<spatial_component>(ent).get_bounds();
        }
        buffers_[info.buffer_index]->write_transform(
            key_of(ent), model_matrix(transform_comp, world.get<model_component>(ent)), tr_bounds,
            instance_shading{.light = info.light, .corners = info.corners});
        touched_bounds_.push_back(info.bounds);
        touched_bounds_.push_back(tr_bounds);
        info.bounds = tr_bounds;
    }

    std::sort(merge_buffer_.begin(), merge_buffer_.end());
    transform_pending_entities_.swap(merge_buffer_);
    stats_.transform_pending = static_cast<uint32>(transform_pending_entities_.size());
}

auto combined_buffer_pool::rewrite_swapped_(
    world_type& world, std::size_t buffer_index, entity swapped
) -> void {
    if (!world.has<transform_component>(swapped)) {
        return;
    }

    vw::spatial::aabb bounds{};
    if (world.has<spatial_component>(swapped)) {
        bounds = world.get<spatial_component>(swapped).get_bounds();
    }

    instance_shading shading{};
    if (const auto* info = entity_buffer_infos_.get(swapped)) {
        shading = {.light = info->light, .corners = info->corners};
    }

    buffers_[buffer_index]->write_transform(
        key_of(swapped),
        model_matrix(world.get<transform_component>(swapped), world.get<model_component>(swapped)),
        bounds, shading
    );
}

auto combined_buffer_pool::update_instance_light_(
    world_type& world
) -> void {
    constexpr float32 light_step = 1.0f / 512.0f;

    const auto* grid = world.system<ecs::world_grid_system>().grid();
    if (grid == nullptr) {
        return;
    }

    const auto& entities = entity_buffer_infos_.entities();
    for (uint32 slot = 0; slot < entity_buffer_infos_.size(); ++slot) {
        auto& info = entity_buffer_infos_.at(slot);
        if (!info.lit_by_world) {
            continue;
        }

        const entity ent = entities[slot];
        if (!world.has<spatial_component>(ent)) {
            continue;
        }

        const world_light target =
            grid->light_at(world.get<spatial_component>(ent).get_bounds().center());
        if (std::abs(target.sky - info.light.sky) < light_step &&
            std::abs(target.block - info.light.block) < light_step) {
            continue;
        }

        if (staging_.available() < sizeof(vec4f)) {
            return;
        }

        buffers_[info.buffer_index]->write_light(key_of(ent), target);
        info.light = target;
    }
}

auto combined_buffer_pool::get_stats() const -> const combined_buffer_pool_stats& {
    stats_.quad_load_min     = 0.0f;
    stats_.quad_load_max     = 0.0f;
    stats_.quad_load_avg     = 0.0f;
    stats_.mesh_capacity     = 0;
    stats_.mesh_count        = 0;
    stats_.instance_capacity = 0;
    stats_.instance_count    = 0;
    stats_.buffers.clear();

    if (buffers_.empty()) {
        return stats_;
    }

    float32 load_avg_sum = 0.0f;

    for (const auto& buffer : buffers_) {
        const auto& buffer_stats = buffer->get_stats();

        stats_.buffers.push_back(buffer_stats);

        if (buffer_stats.quad_load_min < stats_.quad_load_min || stats_.quad_load_min == 0.0f) {
            stats_.quad_load_min = buffer_stats.quad_load_min;
        }
        if (buffer_stats.quad_load_max > stats_.quad_load_max) {
            stats_.quad_load_max = buffer_stats.quad_load_max;
        }

        load_avg_sum += buffer_stats.quad_load_avg;

        stats_.mesh_capacity += buffer_stats.mesh_capacity;
        stats_.mesh_count += buffer_stats.mesh_count;
        stats_.instance_capacity += buffer_stats.instance_capacity;
        stats_.instance_count += buffer_stats.instance_count;
    }

    stats_.quad_load_avg = load_avg_sum / buffers_.size();

    return stats_;
}

}  // namespace vw::gfx
