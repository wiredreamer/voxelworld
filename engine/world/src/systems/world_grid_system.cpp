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
    columns_.grid = std::move(grid);
}

auto world_grid_system::set_loader(
    std::unique_ptr<chunk_loader> loader, job_system& jobs
) -> void {
    clear_loader_transient_state_(columns_);
    columns_.loader = std::move(loader);
    columns_.baker  = columns_.loader != nullptr
                  ? std::make_unique<light_baker>(world_->voxel_types(), jobs)
                  : nullptr;
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

auto world_grid_system::get_light_stats() const -> light_stats {
    return columns_.baker ? columns_.baker->get_stats() : light_stats{};
}

auto world_grid_system::get_stats() const -> const world_grid_system_stats& {
    return stats_;
}

auto world_grid_system::shutdown() -> void {
    columns_.grid.reset();
    columns_.baker.reset();
    columns_.loader.reset();
}

auto world_grid_system::update(float32) -> void {
    if (!columns_.grid || !columns_.loader) {
        return;
    }

    stats_.stage_ms           = 0.0F;
    stats_.light_apply_ms     = 0.0F;
    stats_.integrate_ms       = 0.0F;
    stats_.request_columns_ms = 0.0F;

    auto& reg = world_->registry();

    run_layer_(columns_);
    restore_lod_boundaries_();
    update_grid_stats_();

    const bool view_moved =
        !reg.requested<world_view_component>().empty() && process_dirty_entities_();

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
    stats_.light_apply_ms += measure_ms([&] {
        collect_lit_columns_(layer);
        relight_dirty_columns_(layer);
    });
    stats_.integrate_ms += measure_ms([&] { integrate_completed_columns_(layer); });
    stats_.request_columns_ms += measure_ms([&] { dispatch_column_requests_(layer); });
}

namespace {

constexpr vec2i column_neighbor_offsets[8] = {
    {1, 0},    //
    {-1, 0},   //
    {0, 1},    //
    {0, -1},   //
    {1, 1},    //
    {1, -1},   //
    {-1, 1},   //
    {-1, -1}
};
}  // namespace

auto world_grid_system::fill_shell_(
    column_layer& layer, asset::chunk_volume& vol, vec3i chunk_coord, vec3i step
) -> bool {
    const auto at = chunk_coord + step;

    if (auto* neighbor = model_at_(layer, at)) {
        vol.set_boundary_shell(step, *neighbor);
        return true;
    }

    const auto top = column_top_(layer, vec2i{at.x, at.z});
    if (top.has_value() && at.y > *top) {
        vol.set_boundary_shell_air(step);
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
    for (const vec3i step : all_shell_steps()) {
        if (vol.has_boundary_shell(step)) {
            continue;
        }
        if (fill_shell_(layer, vol, chunk_coord, step)) {
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

    const auto phase = it->second->get_phase();
    if (phase == column_phase::complete || phase == column_phase::lighting) {
        return;
    }
    if (!column_ready_(layer, coord)) {
        return;
    }

    if (already_lit_(*it->second)) {
        it->second->set_phase(column_phase::complete);
        layer.ready_columns.push_back(coord);
        return;
    }

    if (dispatch_light_(layer, coord)) {
        it->second->set_phase(column_phase::lighting);
    }
}

auto world_grid_system::already_lit_(
    gen_column& col
) -> bool {
    const auto& chunks = col.get_all_chunk_data();
    return !chunks.empty() && std::ranges::all_of(chunks, [](const auto& entry) -> bool {
        return entry.second.volume->has_sky_light();
    });
}

auto world_grid_system::column_bottom_(
    column_layer& layer, vec2i coord
) -> std::optional<int32> {
    if (const auto it = layer.staged_columns.find(coord); it != layer.staged_columns.end()) {
        const auto& chunks = it->second->get_all_chunk_data();
        if (chunks.empty()) {
            return std::nullopt;
        }
        return chunks.rbegin()->first;
    }

    const auto levels = layer.grid->column_levels(coord);
    if (levels.empty()) {
        return std::nullopt;
    }
    return levels.front();
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

auto world_grid_system::column_stack_(
    column_layer& layer, vec2i coord, int32 bottom
) -> std::vector<std::shared_ptr<asset::model>> {
    std::vector<std::shared_ptr<asset::model>> stack;

    const auto put = [&](int32 y, std::shared_ptr<asset::model> mdl) -> void {
        if (y < bottom) {
            return;
        }
        const auto slot = static_cast<std::size_t>(y - bottom);
        if (stack.size() <= slot) {
            stack.resize(slot + 1);
        }
        stack[slot] = std::move(mdl);
    };

    if (const auto it = layer.staged_columns.find(coord); it != layer.staged_columns.end()) {
        for (auto& [y, cd] : it->second->get_all_chunk_data()) {
            put(y, cd.volume->shared_voxels());
        }
        return stack;
    }

    for (int32 y : layer.grid->column_levels(coord)) {
        if (auto* placed = layer.grid->get_chunk(vec3i{coord.x, y, coord.y})) {
            put(y, placed->get_model());
        }
    }
    return stack;
}

auto world_grid_system::dispatch_light_(
    column_layer& layer, vec2i coord
) -> bool {
    if (layer.baker == nullptr) {
        return false;
    }
    if (layer.baker->is_pending(coord)) {
        return true;
    }

    int32 bottom = std::numeric_limits<int32>::max();
    for (auto offset : column_neighbor_offsets) {
        if (const auto at = column_bottom_(layer, coord + offset)) {
            bottom = std::min(bottom, *at);
        }
    }
    const auto own = column_bottom_(layer, coord);
    if (!own) {
        return false;
    }
    bottom = std::min(bottom, *own);

    light_request job;
    job.coord    = coord;
    job.bottom_y = bottom;

    for (int32 dz = -1; dz <= 1; ++dz) {
        for (int32 dx = -1; dx <= 1; ++dx) {
            const auto slot  = static_cast<std::size_t>(((dz + 1) * 3) + (dx + 1));
            job.around[slot] = column_stack_(layer, coord + vec2i{dx, dz}, bottom);
        }
    }

    return layer.baker->request(std::move(job));
}

auto world_grid_system::collect_lit_columns_(
    column_layer& layer
) -> void {
    if (layer.baker == nullptr) {
        return;
    }

    while (auto result = layer.baker->try_pop_completed()) {
        const auto it = layer.staged_columns.find(result->coord);
        if (it == layer.staged_columns.end()) {
            apply_relit_column_(layer, *result);
            continue;
        }

        for (auto& [y, cd] : it->second->get_all_chunk_data()) {
            const auto slot = static_cast<std::size_t>(y - result->bottom_y);
            if (slot < result->sky.size()) {
                cd.volume->set_sky_light(std::move(result->sky[slot]));
                cd.volume->set_block_light(std::move(result->block[slot]));
            }
        }

        it->second->set_phase(column_phase::complete);
        layer.ready_columns.push_back(result->coord);
    }
}

auto world_grid_system::apply_relit_column_(
    column_layer& layer, light_result& result
) -> void {
    for (int32 y : layer.grid->column_levels(result.coord)) {
        const auto slot = static_cast<std::size_t>(y - result.bottom_y);
        if (slot >= result.sky.size()) {
            continue;
        }

        const vec3i at{result.coord.x, y, result.coord.y};
        auto* placed = layer.grid->get_chunk(at);
        if (placed == nullptr) {
            continue;
        }

        auto& vol               = *placed->get_volume();
        const auto* stood_sky   = vol.get_sky_light();
        const auto* stood_block = vol.get_block_light();

        const bool sky_moved   = stood_sky == nullptr || *stood_sky != result.sky[slot];
        const bool block_moved = stood_block == nullptr || *stood_block != result.block[slot];

        if (!sky_moved && !block_moved) {
            continue;
        }

        vol.set_sky_light(std::move(result.sky[slot]));
        vol.set_block_light(std::move(result.block[slot]));
        layer.grid->remesh_drawn_chunk(at);
        static_cast<void>(refresh_boundary_(layer, at));
        ++stats_.relit_chunks;
    }
}

auto world_grid_system::relight_dirty_columns_(
    column_layer& layer
) -> void {
    if (layer.baker == nullptr) {
        return;
    }

    for (vec2i coord : layer.grid->take_light_dirty()) {
        layer.light_dirty.insert(coord);
    }

    if (layer.light_dirty.empty()) {
        return;
    }

    static constexpr int32 max_relights_per_frame = 2;
    int32 started                                 = 0;

    std::erase_if(layer.light_dirty, [&](vec2i coord) -> bool {
        if (!column_available_(layer, coord)) {
            return true;
        }
        if (started >= max_relights_per_frame || layer.baker->is_pending(coord)) {
            return false;
        }
        if (!column_ready_(layer, coord)) {
            return false;
        }

        if (!dispatch_light_(layer, coord)) {
            return true;
        }

        ++started;
        ++stats_.relit_columns;
        return true;
    });

    stats_.relight_backlog = static_cast<uint32>(layer.light_dirty.size());
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
        const auto coord = layer.ready_columns.back();
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
                for (const vec3i step : all_shell_steps()) {
                    static_cast<void>(fill_shell_(layer, *cd.volume, cd.coord, step));
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

    const uint32 in_flight =
        layer.loader->pending_count() + (layer.baker != nullptr ? layer.baker->pending_count() : 0U);

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
    stats_.lighting_count    = columns_.baker != nullptr ? columns_.baker->pending_count() : 0U;
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
    layer.light_dirty.clear();
    layer.active_columns.clear();
    layer.pending_active_columns.clear();
}

auto world_grid_system::clear_loader_transient_state_(
    column_layer& layer
) -> void {
    layer.pending_requests.clear();
}

}  // namespace vw::ecs
