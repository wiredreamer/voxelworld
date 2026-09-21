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
    clear_grid_transient_state_();
    near_.grid = std::move(grid);
}

auto world_grid_system::set_loader(
    std::unique_ptr<chunk_loader> loader, job_system& jobs
) -> void {
    clear_loader_transient_state_();
    near_.loader = std::move(loader);
    baker_  = near_.loader != nullptr
                  ? std::make_unique<light_baker>(world_->voxel_types(), jobs)
                  : nullptr;
}

auto world_grid_system::grid() -> world_grid* {
    return near_.grid.get();
}

auto world_grid_system::grid() const -> const world_grid* {
    return near_.grid.get();
}

auto world_grid_system::loader() -> chunk_loader* {
    return near_.loader.get();
}

auto world_grid_system::loader() const -> const chunk_loader* {
    return near_.loader.get();
}

auto world_grid_system::has_grid() const -> bool {
    return near_.grid != nullptr;
}

auto world_grid_system::has_loader() const -> bool {
    return near_.loader != nullptr;
}

auto world_grid_system::get_loader_stats() const -> column_gen_stats {
    return near_.loader ? near_.loader->get_gen_stats() : column_gen_stats{};
}

auto world_grid_system::get_light_stats() const -> light_stats {
    return baker_ ? baker_->get_stats() : light_stats{};
}

auto world_grid_system::get_stats() const -> const world_grid_system_stats& {
    return stats_;
}

auto world_grid_system::shutdown() -> void {
    near_.grid.reset();
    baker_.reset();
    near_.loader.reset();
}

auto world_grid_system::update(float32) -> void {
    if (!near_.grid || !near_.loader) {
        return;
    }

    auto& reg       = world_->registry();
    stats_.stage_ms = measure_ms([&] { stage_completed_columns_(); });
    stats_.light_apply_ms = measure_ms([&] {
        collect_lit_columns_();
        relight_dirty_columns_();
    });
    stats_.integrate_ms       = measure_ms([&] { integrate_completed_columns_(); });
    stats_.request_columns_ms = measure_ms([&] { dispatch_column_requests_(); });
    update_grid_stats_();

    if (reg.requested<world_view_component>().empty()) {
        return;
    }

    if (process_dirty_entities_()) {
        vec2i camera_column{};
        stats_.rebuild_active_ms = measure_ms([&] { camera_column = rebuild_active_set_(); });
        stats_.unload_ms         = measure_ms([&] { unload_inactive_columns_(); });
        std::swap(near_.active_columns, near_.pending_active_columns);
        rebuild_pending_requests_(camera_column);
        stats_.active_count = static_cast<uint32>(near_.active_columns.size());
    }

    reg.clear_requested<world_view_component>();
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

auto world_grid_system::column_available_(
    vec2i coord
) const -> bool {
    return near_.staged_columns.contains(coord) || near_.grid->has_column(coord);
}

auto world_grid_system::within_draw_(
    vec2i coord
) const -> bool {
    const auto d = coord - camera_column_;
    return std::abs(d.x) <= draw_distance_ && std::abs(d.y) <= draw_distance_;
}

auto world_grid_system::column_ready_(
    vec2i coord
) const -> bool {
    return std::ranges::all_of(column_neighbor_offsets, [&](vec2i offset) -> bool {
        return column_available_(coord + offset);
    });
}

auto world_grid_system::queue_if_ready_(
    vec2i coord
) -> void {
    const auto it = near_.staged_columns.find(coord);
    if (it == near_.staged_columns.end()) {
        return;
    }

    const auto phase = it->second->get_phase();
    if (phase == column_phase::complete || phase == column_phase::lighting) {
        return;
    }
    if (!column_ready_(coord)) {
        return;
    }

    if (already_lit_(*it->second)) {
        it->second->set_phase(column_phase::complete);
        near_.ready_columns.push_back(coord);
        return;
    }

    if (dispatch_light_(coord)) {
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
    vec2i coord
) -> std::optional<int32> {
    if (const auto it = near_.staged_columns.find(coord); it != near_.staged_columns.end()) {
        const auto& chunks = it->second->get_all_chunk_data();
        if (chunks.empty()) {
            return std::nullopt;
        }
        return chunks.rbegin()->first;
    }

    const auto levels = near_.grid->column_levels(coord);
    if (levels.empty()) {
        return std::nullopt;
    }
    return levels.front();
}

auto world_grid_system::column_stack_(
    vec2i coord, int32 bottom
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

    if (const auto it = near_.staged_columns.find(coord); it != near_.staged_columns.end()) {
        for (auto& [y, cd] : it->second->get_all_chunk_data()) {
            put(y, cd.volume->shared_voxels());
        }
        return stack;
    }

    for (int32 y : near_.grid->column_levels(coord)) {
        if (auto* placed = near_.grid->get_chunk(vec3i{coord.x, y, coord.y})) {
            put(y, placed->get_model());
        }
    }
    return stack;
}

auto world_grid_system::dispatch_light_(
    vec2i coord
) -> bool {
    if (baker_ == nullptr) {
        return false;
    }
    if (baker_->is_pending(coord)) {
        return true;
    }

    int32 bottom = std::numeric_limits<int32>::max();
    for (auto offset : column_neighbor_offsets) {
        if (const auto at = column_bottom_(coord + offset)) {
            bottom = std::min(bottom, *at);
        }
    }
    const auto own = column_bottom_(coord);
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
            job.around[slot] = column_stack_(coord + vec2i{dx, dz}, bottom);
        }
    }

    return baker_->request(std::move(job));
}

auto world_grid_system::collect_lit_columns_() -> void {
    if (baker_ == nullptr) {
        return;
    }

    while (auto result = baker_->try_pop_completed()) {
        const auto it = near_.staged_columns.find(result->coord);
        if (it == near_.staged_columns.end()) {
            apply_relit_column_(*result);
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
        near_.ready_columns.push_back(result->coord);
    }
}

auto world_grid_system::apply_relit_column_(
    light_result& result
) -> void {
    for (int32 y : near_.grid->column_levels(result.coord)) {
        const auto slot = static_cast<std::size_t>(y - result.bottom_y);
        if (slot >= result.sky.size()) {
            continue;
        }

        const vec3i at{result.coord.x, y, result.coord.y};
        auto* placed = near_.grid->get_chunk(at);
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
        near_.grid->remesh_drawn_chunk(at);
        ++stats_.relit_chunks;
    }
}

auto world_grid_system::relight_dirty_columns_() -> void {
    if (baker_ == nullptr) {
        return;
    }

    for (vec2i coord : near_.grid->take_light_dirty()) {
        near_.light_dirty.insert(coord);
    }

    if (near_.light_dirty.empty()) {
        return;
    }

    static constexpr int32 max_relights_per_frame = 2;
    int32 started                                 = 0;

    std::erase_if(near_.light_dirty, [&](vec2i coord) -> bool {
        if (!column_available_(coord)) {
            return true;
        }
        if (started >= max_relights_per_frame || baker_->is_pending(coord)) {
            return false;
        }
        if (!column_ready_(coord)) {
            return false;
        }

        if (!dispatch_light_(coord)) {
            return true;
        }

        ++started;
        ++stats_.relit_columns;
        return true;
    });

    stats_.relight_backlog = static_cast<uint32>(near_.light_dirty.size());
}

auto world_grid_system::stage_completed_columns_() -> void {
    while (true) {
        auto col = near_.loader->try_pop_completed();
        if (!col) {
            break;
        }

        const auto coord = col->get_coord();
        if (!near_.active_columns.contains(coord) && !near_.pending_active_columns.contains(coord)) {
            continue;
        }

        near_.staged_columns[coord] = std::move(col);

        queue_if_ready_(coord);
        for (auto offset : column_neighbor_offsets) {
            queue_if_ready_(coord + offset);
        }
    }
}

auto world_grid_system::model_at_(
    vec3i chunk_coord
) const -> asset::model* {
    if (auto* placed = near_.grid->get_chunk(chunk_coord)) {
        return placed->get_model().get();
    }

    const auto it = near_.staged_columns.find(vec2i{chunk_coord.x, chunk_coord.z});
    if (it == near_.staged_columns.end()) {
        return nullptr;
    }

    const auto* data = it->second->get_chunk_data(chunk_coord.y);
    return data != nullptr ? data->volume->shared_voxels().get() : nullptr;
}

auto world_grid_system::integrate_completed_columns_() -> void {
    static constexpr int32 max_columns_per_frame = 1;

    float32 boundary_from_total = 0.0f;
    float32 chunk_create_total  = 0.0f;
    int32 processed_columns     = 0;

    while (processed_columns < max_columns_per_frame && !near_.ready_columns.empty()) {
        const auto coord = near_.ready_columns.back();
        near_.ready_columns.pop_back();

        const auto it = near_.staged_columns.find(coord);
        if (it == near_.staged_columns.end()) {
            continue;
        }

        if (!column_ready_(coord)) {
            it->second->set_phase(column_phase::terrain);
            continue;
        }

        boundary_from_total += measure_ms([&] -> auto {
            for (auto& [y, cd] : it->second->get_all_chunk_data()) {
                for (const face_direction face : all_face_directions) {
                    if (auto* neighbor = model_at_(cd.coord + offset_of(face))) {
                        cd.volume->set_boundary_slice(face, *neighbor);
                    }
                }
            }
        });

        auto col = std::move(it->second);
        near_.staged_columns.erase(it);

        std::vector<int32> y_levels;

        for (auto& [y, cd] : col->get_all_chunk_data()) {
            chunk_create_total += measure_ms([&] -> auto {
                near_.grid->place_chunk(cd.coord, std::move(cd.volume));
            });

            y_levels.push_back(y);
        }

        std::sort(y_levels.begin(), y_levels.end());
        near_.grid->register_column(coord, std::move(y_levels));
        ++processed_columns;
    }

    stats_.boundary_from_ms = boundary_from_total;
    stats_.chunk_create_ms  = chunk_create_total;
}

auto world_grid_system::dispatch_column_requests_() -> void {
    static constexpr int32 max_requests_per_frame = 8;

    // см. docs/world.md#потолок-работы-в-полёте
    static constexpr uint32 max_columns_in_flight = 96;

    const uint32 in_flight =
        near_.loader->pending_count() + (baker_ != nullptr ? baker_->pending_count() : 0U);

    int32 requests = 0;
    while (!near_.pending_requests.empty() && requests < max_requests_per_frame &&
           in_flight < max_columns_in_flight) {
        auto coord = near_.pending_requests.back();
        near_.pending_requests.pop_back();
        if (near_.loader->request(coord)) {
            ++requests;
        }
    }
}

auto world_grid_system::update_grid_stats_() -> void {
    stats_.active_count      = static_cast<uint32>(near_.active_columns.size());
    stats_.pending_count     = near_.loader->pending_count();
    stats_.loaded_count      = near_.grid->chunk_count();
    stats_.drawn_count       = near_.grid->drawn_chunk_count();
    stats_.staged_count      = static_cast<uint32>(near_.staged_columns.size());
    stats_.lighting_count    = baker_ != nullptr ? baker_->pending_count() : 0U;
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

auto world_grid_system::rebuild_active_set_() -> vec2i {
    auto& reg = world_->registry();
    near_.pending_active_columns.clear();
    vec2i camera_column{};

    for (auto ent : reg.requested<world_view_component>()) {
        if (!reg.has<world_view_component>(ent) ||
            !reg.has<transform_component>(ent)) {
            continue;
        }

        const auto& wv   = reg.get<world_view_component>(ent);
        auto chunk_coord = wv.get_chunk_coord();
        camera_column    = {chunk_coord.x, chunk_coord.z};

        camera_column_ = camera_column;
        draw_distance_ = static_cast<int32>(wv.get_view_distance());

        const auto dist = draw_distance_ + apron_columns;

        for (int32 dx = -dist; dx <= dist; ++dx) {
            for (int32 dz = -dist; dz <= dist; ++dz) {
                int32 cx = camera_column.x + dx;
                int32 cz = camera_column.y + dz;
                near_.pending_active_columns.insert({cx, cz});
            }
        }
    }

    return camera_column;
}

auto world_grid_system::demote_column_(
    vec2i coord
) -> void {
    auto col = std::make_unique<gen_column>(coord.x, coord.y);

    for (int32 y : near_.grid->column_levels(coord)) {
        const vec3i chunk_coord{coord.x, y, coord.y};
        if (auto* placed = near_.grid->get_chunk(chunk_coord)) {
            col->create_chunk(y, chunk_data{chunk_coord, placed->get_volume()});
        }
    }

    near_.grid->unload_column(coord);

    col->set_phase(column_phase::terrain);
    near_.staged_columns[coord] = std::move(col);
}

auto world_grid_system::unload_inactive_columns_() -> void {
    for (const auto& coord : near_.active_columns) {
        if (!near_.pending_active_columns.contains(coord)) {
            near_.grid->unload_column(coord);
        } else if (!within_draw_(coord) && near_.grid->has_column(coord)) {
            demote_column_(coord);
        }
    }

    std::erase_if(near_.staged_columns, [this](const auto& entry) -> bool {
        return !near_.pending_active_columns.contains(entry.first);
    });

    std::erase_if(near_.ready_columns, [this](vec2i coord) -> bool {
        return !near_.staged_columns.contains(coord);
    });

    for (const auto& [coord, col] : near_.staged_columns) {
        queue_if_ready_(coord);
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

    auto new_chunk_coord = near_.grid->world_to_chunk_coord({
        static_cast<int32>(pos.x),
        static_cast<int32>(pos.y),
        static_cast<int32>(pos.z)
    });

    bool changed     = wv.dirty_ || wv.chunk_coord_ != new_chunk_coord;
    wv.chunk_coord_  = new_chunk_coord;
    wv.dirty_        = false;
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
    vec2i camera_column
) -> void {
    near_.pending_requests.clear();

    for (const auto& coord : near_.active_columns) {
        if (!column_available_(coord) && !near_.loader->is_pending(coord)) {
            near_.pending_requests.push_back(coord);
        }
    }

    std::sort(
        near_.pending_requests.begin(), near_.pending_requests.end(),
        [&camera_column](const vec2i& a, const vec2i& b) {
            auto da     = a - camera_column;
            auto db     = b - camera_column;
            auto dist_a = da.x * da.x + da.y * da.y;
            auto dist_b = db.x * db.x + db.y * db.y;
            return dist_a > dist_b;
        }
    );
}

auto world_grid_system::clear_grid_transient_state_() -> void {
    near_.pending_requests.clear();
    near_.staged_columns.clear();
    near_.ready_columns.clear();
    near_.light_dirty.clear();
    near_.active_columns.clear();
    near_.pending_active_columns.clear();
}

auto world_grid_system::clear_loader_transient_state_() -> void {
    near_.pending_requests.clear();
}

}  // namespace vw::ecs
