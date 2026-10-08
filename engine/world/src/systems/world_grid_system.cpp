module vw.world;

import std;
import vw.core;
import vw.asset;

namespace vw::ecs {

world_grid_system::world_grid_system(
    world& w
)
    : world_(&w) {}

world_grid_system::~world_grid_system() = default;

world_grid_system::world_grid_system(world_grid_system&&) noexcept = default;

auto world_grid_system::operator=(world_grid_system&&) noexcept -> world_grid_system& = default;

auto world_grid_system::set_grid(
    std::unique_ptr<world_grid> grid
) -> void {
    clear_grid_transient_state_(columns_);
    columns_.grid     = std::move(grid);
    columns_.reseeded = true;
}

auto world_grid_system::set_loader(
    std::unique_ptr<chunk_loader> loader
) -> void {
    clear_loader_transient_state_(columns_);
    columns_.loader   = std::move(loader);
    columns_.reseeded = true;
}

auto world_grid_system::grid() -> world_grid* {
    return columns_.grid.get();
}

auto world_grid_system::grid() const -> const world_grid* {
    return columns_.grid.get();
}

auto world_grid_system::loader() -> chunk_loader* {
    return columns_.loader.get();
}

auto world_grid_system::loader() const -> const chunk_loader* {
    return columns_.loader.get();
}

auto world_grid_system::has_grid() const -> bool {
    return columns_.grid != nullptr;
}

auto world_grid_system::has_loader() const -> bool {
    return columns_.loader != nullptr;
}

auto world_grid_system::get_loader_stats() const -> column_gen_stats {
    return columns_.loader ? columns_.loader->get_gen_stats() : column_gen_stats{};
}

auto world_grid_system::get_stats() const -> const world_grid_system_stats& {
    return stats_;
}

auto world_grid_system::shutdown() -> void {
    columns_.grid.reset();
    columns_.loader.reset();
}

auto world_grid_system::update(float32) -> void {
    if (!columns_.grid || !columns_.loader) {
        return;
    }

    stats_.stage_ms           = 0.0F;
    stats_.integrate_ms       = 0.0F;
    stats_.request_columns_ms = 0.0F;

    auto& reg = world_->registry();

    run_layer_(columns_);
    restore_lod_boundaries_();
    update_grid_stats_();

    const bool view_changed =
        !reg.requested<world_view_component>().empty() && process_dirty_entities_();
    const bool view_moved = std::exchange(columns_.reseeded, false) || view_changed;

    if (view_moved) {
        stats_.rebuild_active_ms = measure_ms([&] { rebuild_active_set_(); });

        stats_.unload_ms = measure_ms([&] { unload_inactive_columns_(columns_); });

        std::swap(columns_.active_columns, columns_.pending_active_columns);
        rebuild_pending_requests_(columns_, columns_.camera_column);
        stats_.active_count = static_cast<uint32>(columns_.active_columns.size());
    }

    reg.clear_requested<world_view_component>();
}

auto world_grid_system::run_layer_(
    column_layer& layer
) -> void {
    if (layer.grid == nullptr || layer.loader == nullptr) {
        return;
    }

    stats_.stage_ms += measure_ms([&] { stage_completed_columns_(layer); });
    stats_.integrate_ms += measure_ms([&] { integrate_completed_columns_(layer); });
    stats_.request_columns_ms += measure_ms([&] { dispatch_column_requests_(layer); });
}

namespace {

// см. docs/world.md#путь-колонки
constexpr vec2i column_neighbor_offsets[4] = {{1, 0}, {-1, 0}, {0, 1}, {0, -1}};
}  // namespace

auto world_grid_system::fill_face_(
    column_layer& layer, asset::chunk_volume& vol, vec3i chunk_coord, face_direction face
) -> bool {
    const auto at = chunk_coord + offset_of(face);

    if (auto* neighbor = model_at_(layer, at)) {
        vol.set_boundary_slice(face, *neighbor);
        return true;
    }

    const auto top = column_top_(layer, vec2i{at.x, at.z});
    if (top.has_value() && at.y > *top) {
        vol.set_boundary_air(face);
        return true;
    }

    return false;
}

auto world_grid_system::refresh_boundary_(
    column_layer& layer, vec3i chunk_coord
) -> bool {
    auto* placed = layer.grid->get_chunk(chunk_coord);
    if (placed == nullptr) {
        return false;
    }

    auto& vol = *placed->get_volume();

    bool filled = false;
    for (const face_direction face : all_face_directions) {
        if (vol.has_boundary_slice(face)) {
            continue;
        }
        if (fill_face_(layer, vol, chunk_coord, face)) {
            filled = true;
        }
    }

    return filled;
}

auto world_grid_system::restore_lod_boundaries_() -> void {
    if (columns_.grid == nullptr) {
        return;
    }

    auto& reg       = world_->registry();
    const auto& lod = world_->system<lod_system>();

    stats_.boundary_restored = 0;

    for (const entity ent : lod.levels_changed_this_frame()) {
        if (!reg.has<transform_component>(ent)) {
            continue;
        }

        const auto at = reg.get<transform_component>(ent).get_position();
        const auto cc = columns_.grid->world_to_chunk_coord(
            vec3i{static_cast<int32>(at.x), static_cast<int32>(at.y), static_cast<int32>(at.z)}
        );

        auto* placed = columns_.grid->get_chunk(cc);
        if (placed == nullptr || placed->get_entity() != ent) {
            continue;
        }

        if (refresh_boundary_(columns_, cc)) {
            ++stats_.boundary_restored;
        }
    }
}

auto world_grid_system::column_available_(
    column_layer& layer, vec2i coord
) const -> bool {
    return layer.staged_columns.contains(coord) || layer.grid->has_column(coord);
}

auto world_grid_system::within_draw_(
    const column_layer& layer, vec2i coord
) const -> bool {
    const auto d = coord - layer.camera_column;
    return std::abs(d.x) <= layer.draw_distance && std::abs(d.y) <= layer.draw_distance;
}

auto world_grid_system::column_ready_(
    column_layer& layer, vec2i coord
) const -> bool {
    return std::ranges::all_of(column_neighbor_offsets, [&](vec2i offset) -> bool {
        return column_available_(layer, coord + offset);
    });
}

auto world_grid_system::queue_if_ready_(
    column_layer& layer, vec2i coord
) -> void {
    const auto it = layer.staged_columns.find(coord);
    if (it == layer.staged_columns.end()) {
        return;
    }

    if (it->second->get_phase() == column_phase::complete) {
        return;
    }
    if (it->second->get_all_chunk_data().empty() || !column_ready_(layer, coord)) {
        return;
    }

    it->second->set_phase(column_phase::complete);
    layer.ready_columns.push_back(coord);
}

auto world_grid_system::column_top_(
    column_layer& layer, vec2i coord
) -> std::optional<int32> {
    if (const auto it = layer.staged_columns.find(coord); it != layer.staged_columns.end()) {
        const auto& chunks = it->second->get_all_chunk_data();
        if (chunks.empty()) {
            return std::nullopt;
        }
        return chunks.begin()->first;
    }

    const auto levels = layer.grid->column_levels(coord);
    if (levels.empty()) {
        return std::nullopt;
    }
    return levels.back();
}

auto world_grid_system::stage_completed_columns_(
    column_layer& layer
) -> void {
    while (true) {
        auto col = layer.loader->try_pop_completed();
        if (!col) {
            break;
        }

        const auto coord = col->get_coord();
        if (!layer.active_columns.contains(coord) && !layer.pending_active_columns.contains(coord)) {
            continue;
        }

        layer.staged_columns[coord] = std::move(col);

        queue_if_ready_(layer, coord);
        for (auto offset : column_neighbor_offsets) {
            queue_if_ready_(layer, coord + offset);
        }
    }
}

auto world_grid_system::model_at_(
    column_layer& layer, vec3i chunk_coord
) const -> asset::model* {
    if (auto* placed = layer.grid->get_chunk(chunk_coord)) {
        return placed->get_model().get();
    }

    const auto it = layer.staged_columns.find(vec2i{chunk_coord.x, chunk_coord.z});
    if (it == layer.staged_columns.end()) {
        return nullptr;
    }

    const auto* data = it->second->get_chunk_data(chunk_coord.y);
    return data != nullptr ? data->volume->shared_voxels().get() : nullptr;
}

auto world_grid_system::integrate_completed_columns_(
    column_layer& layer
) -> void {
    static constexpr int32 max_columns_per_frame = 1;

    float32 boundary_from_total = 0.0f;
    float32 chunk_create_total  = 0.0f;
    int32 processed_columns     = 0;

    while (processed_columns < max_columns_per_frame && !layer.ready_columns.empty()) {
        // см. docs/world.md#путь-колонки
        const auto nearest = std::ranges::min_element(
            layer.ready_columns, {}, [&layer](vec2i at) -> int64 {
                const vec2i away = at - layer.camera_column;
                return (int64{away.x} * away.x) + (int64{away.y} * away.y);
            }
        );
        const auto coord = *nearest;
        *nearest         = layer.ready_columns.back();
        layer.ready_columns.pop_back();

        const auto it = layer.staged_columns.find(coord);
        if (it == layer.staged_columns.end()) {
            continue;
        }

        if (!within_draw_(layer, coord)) {
            continue;
        }

        if (!column_ready_(layer, coord)) {
            it->second->set_phase(column_phase::terrain);
            continue;
        }

        boundary_from_total += measure_ms([&] -> auto {
            for (auto& [y, cd] : it->second->get_all_chunk_data()) {
                for (const face_direction face : all_face_directions) {
                    static_cast<void>(fill_face_(layer, *cd.volume, cd.coord, face));
                }
            }
        });

        auto col = std::move(it->second);
        layer.staged_columns.erase(it);

        std::vector<int32> y_levels;

        for (auto& [y, cd] : col->get_all_chunk_data()) {
            chunk_create_total += measure_ms([&] -> auto {
                layer.grid->place_chunk(cd.coord, std::move(cd.volume));
            });

            y_levels.push_back(y);
        }

        std::sort(y_levels.begin(), y_levels.end());
        layer.grid->register_column(coord, std::move(y_levels));
        ++processed_columns;
    }

    stats_.boundary_from_ms = boundary_from_total;
    stats_.chunk_create_ms  = chunk_create_total;
}

auto world_grid_system::dispatch_column_requests_(
    column_layer& layer
) -> void {
    static constexpr int32 max_requests_per_frame = 8;

    // см. docs/world.md#потолок-работы-в-полёте
    static constexpr uint32 max_columns_in_flight = 96;

    const uint32 in_flight = layer.loader->pending_count();

    int32 requests = 0;
    while (!layer.pending_requests.empty() && requests < max_requests_per_frame &&
           in_flight < max_columns_in_flight) {
        auto coord = layer.pending_requests.back();
        layer.pending_requests.pop_back();
        if (layer.loader->request(coord)) {
            ++requests;
        }
    }
}

auto world_grid_system::update_grid_stats_() -> void {
    stats_.active_count      = static_cast<uint32>(columns_.active_columns.size());
    stats_.pending_count     = columns_.loader->pending_count();
    stats_.loaded_count      = columns_.grid->chunk_count();
    stats_.drawn_count       = columns_.grid->drawn_chunk_count();
    stats_.staged_count      = static_cast<uint32>(columns_.staged_columns.size());
    stats_.ready_count       = static_cast<uint32>(columns_.ready_columns.size());
    stats_.rebuild_active_ms = 0.0f;
    stats_.unload_ms         = 0.0f;
}

auto world_grid_system::process_dirty_entities_() -> bool {
    auto& reg         = world_->registry();
    bool chunks_dirty = false;
    for (auto ent : reg.requested<world_view_component>()) {
        if (process_dirty_entity_(ent)) {
            chunks_dirty = true;
        }
        reg.notify_changed<world_view_component>(ent);
    }
    return chunks_dirty;
}

auto world_grid_system::rebuild_active_set_() -> void {
    columns_.pending_active_columns.clear();

    const auto dist = columns_.draw_distance + apron_columns;
    for (int32 dx = -dist; dx <= dist; ++dx) {
        for (int32 dz = -dist; dz <= dist; ++dz) {
            columns_.pending_active_columns.insert(
                {columns_.camera_column.x + dx, columns_.camera_column.y + dz}
            );
        }
    }
}

auto world_grid_system::demote_column_(
    column_layer& layer, vec2i coord
) -> void {
    auto col = std::make_unique<gen_column>(coord.x, coord.y);

    for (int32 y : layer.grid->column_levels(coord)) {
        const vec3i chunk_coord{coord.x, y, coord.y};
        if (auto* placed = layer.grid->get_chunk(chunk_coord)) {
            col->create_chunk(y, chunk_data{chunk_coord, placed->get_volume()});
        }
    }

    layer.grid->unload_column(coord);

    col->set_phase(column_phase::terrain);
    layer.staged_columns[coord] = std::move(col);
}

auto world_grid_system::unload_inactive_columns_(
    column_layer& layer
) -> void {
    for (const auto& coord : layer.active_columns) {
        if (!layer.pending_active_columns.contains(coord)) {
            layer.grid->unload_column(coord);
        } else if (!within_draw_(layer, coord) && layer.grid->has_column(coord)) {
            demote_column_(layer, coord);
        }
    }

    std::erase_if(layer.staged_columns, [&layer](const auto& entry) -> bool {
        return !layer.pending_active_columns.contains(entry.first);
    });

    std::erase_if(layer.ready_columns, [&layer](vec2i coord) -> bool {
        return !layer.staged_columns.contains(coord);
    });

    for (const auto& [coord, col] : layer.staged_columns) {
        queue_if_ready_(layer, coord);

        if (col->get_phase() == column_phase::complete && within_draw_(layer, coord) &&
            std::ranges::find(layer.ready_columns, coord) == layer.ready_columns.end()) {
            layer.ready_columns.push_back(coord);
        }
    }
}

auto world_grid_system::process_dirty_entity_(
    entity ent
) -> bool {
    auto& reg = world_->registry();
    if (!reg.has<world_view_component>(ent) ||
        !reg.has<transform_component>(ent)) {
        return false;
    }

    auto& wv       = reg.get<world_view_component>(ent);
    const auto& tc = reg.get<transform_component>(ent);
    auto pos       = tc.get_position();

    auto new_chunk_coord = columns_.grid->world_to_chunk_coord({
        static_cast<int32>(pos.x),
        static_cast<int32>(pos.y),
        static_cast<int32>(pos.z)
    });

    bool changed    = wv.dirty_ || wv.chunk_coord_ != new_chunk_coord;
    wv.chunk_coord_ = new_chunk_coord;
    wv.dirty_       = false;

    columns_.camera_column = {new_chunk_coord.x, new_chunk_coord.z};
    columns_.draw_distance = static_cast<int32>(wv.get_view_distance());

    return changed;
}

world_grid_system::view_modifier::view_modifier(
    world_grid_system* system, entity ent
)
    : system_(system), entity_(ent) {}

auto world_grid_system::modify_view(
    entity ent
) -> view_modifier {
    return view_modifier(this, ent);
}

auto world_grid_system::view_modifier::set_view_distance(
    uint32 distance
) -> view_modifier& {
    auto& reg = system_->world_->registry();
    if (!reg.has<world_view_component>(entity_)) {
        return *this;
    }

    auto& wv          = reg.get<world_view_component>(entity_);
    wv.view_distance_ = distance;
    reg.request_change<world_view_component>(entity_);

    return *this;
}

auto world_grid_system::rebuild_pending_requests_(
    column_layer& layer, vec2i camera_column
) -> void {
    layer.pending_requests.clear();

    for (const auto& coord : layer.active_columns) {
        if (!column_available_(layer, coord) && !layer.loader->is_pending(coord)) {
            layer.pending_requests.push_back(coord);
        }
    }

    std::sort(
        layer.pending_requests.begin(), layer.pending_requests.end(),
        [&camera_column](const vec2i& a, const vec2i& b) {
            auto da     = a - camera_column;
            auto db     = b - camera_column;
            auto dist_a = da.x * da.x + da.y * da.y;
            auto dist_b = db.x * db.x + db.y * db.y;
            return dist_a > dist_b;
        }
    );
}

auto world_grid_system::clear_grid_transient_state_(
    column_layer& layer
) -> void {
    layer.pending_requests.clear();
    layer.staged_columns.clear();
    layer.ready_columns.clear();
    layer.active_columns.clear();
    layer.pending_active_columns.clear();
}

auto world_grid_system::clear_loader_transient_state_(
    column_layer& layer
) -> void {
    layer.pending_requests.clear();
}

}  // namespace vw::ecs
