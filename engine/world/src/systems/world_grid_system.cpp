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
    grid_ = std::move(grid);
}

auto world_grid_system::set_loader(
    std::unique_ptr<chunk_loader> loader
) -> void {
    clear_loader_transient_state_();
    loader_ = std::move(loader);
    baker_  = loader_ != nullptr ? std::make_unique<light_baker>(world_->voxel_types()) : nullptr;
}

auto world_grid_system::grid() -> world_grid* {
    return grid_.get();
}

auto world_grid_system::grid() const -> const world_grid* {
    return grid_.get();
}

auto world_grid_system::loader() -> chunk_loader* {
    return loader_.get();
}

auto world_grid_system::loader() const -> const chunk_loader* {
    return loader_.get();
}

auto world_grid_system::has_grid() const -> bool {
    return grid_ != nullptr;
}

auto world_grid_system::has_loader() const -> bool {
    return loader_ != nullptr;
}

auto world_grid_system::get_loader_stats() const -> column_gen_stats {
    return loader_ ? loader_->get_gen_stats() : column_gen_stats{};
}

auto world_grid_system::get_light_stats() const -> light_stats {
    return baker_ ? baker_->get_stats() : light_stats{};
}

auto world_grid_system::get_stats() const -> const world_grid_system_stats& {
    return stats_;
}

auto world_grid_system::shutdown() -> void {
    grid_.reset();
    baker_.reset();
    loader_.reset();
}

auto world_grid_system::update(float32 /*dt*/) -> void {
    if (!grid_ || !loader_) {
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
        std::swap(active_columns_, pending_active_columns_);
        rebuild_pending_requests_(camera_column);
        stats_.active_count = static_cast<uint32>(active_columns_.size());
    }

    reg.clear_requested<world_view_component>();
}

namespace {
// Все восемь, включая диагонали. Границам всегда хватало четырёх, но небесный
// свет переходит через угол так же охотно, как через сторону, и у колонки,
// освещённой без диагоналей, остаются тёмные клинья на стыке двух швов.
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
    return staged_columns_.contains(coord) || grid_->has_column(coord);
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
    const auto it = staged_columns_.find(coord);
    if (it == staged_columns_.end()) {
        return;
    }

    const auto phase = it->second->get_phase();
    if (phase == column_phase::complete || phase == column_phase::lighting) {
        return;
    }
    if (!column_ready_(coord)) {
        return;
    }

    // Колонка, ушедшая за дальность отрисовки и вернувшаяся, — это свежий
    // gen_column вокруг тех же моделей, поэтому её фаза говорит «рельеф», как бы
    // давно её ни осветили. Помнят об этом модели.
    if (already_lit_(*it->second)) {
        it->second->set_phase(column_phase::complete);
        ready_columns_.push_back(coord);
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
    if (const auto it = staged_columns_.find(coord); it != staged_columns_.end()) {
        const auto& chunks = it->second->get_all_chunk_data();
        if (chunks.empty()) {
            return std::nullopt;
        }
        // Карта идёт сверху вниз, поэтому последняя запись — пол.
        return chunks.rbegin()->first;
    }

    const auto levels = grid_->column_levels(coord);
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

    if (const auto it = staged_columns_.find(coord); it != staged_columns_.end()) {
        for (auto& [y, cd] : it->second->get_all_chunk_data()) {
            put(y, cd.volume->shared_voxels());
        }
        return stack;
    }

    for (int32 y : grid_->column_levels(coord)) {
        if (auto* placed = grid_->get_chunk(vec3i{coord.x, y, coord.y})) {
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

    // Все колонки этого мира стоят на одном полу, поэтому его даст любая из
    // девяти. Взять самую нижнюю — тот же ответ, но на одно допущение меньше.
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
        const auto it = staged_columns_.find(result->coord);
        if (it == staged_columns_.end()) {
            // В накопителе её нет, значит это перезаливка колонки, уже стоящей в
            // мире, — или той, что с тех пор выгружена; от такой
            // apply_relit_column_ ничего не находит и отбрасывает результат.
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
        ready_columns_.push_back(result->coord);
    }
}

// Перезалитая колонка уже стоит в мире с мешами, построенными под её старый свет,
// поэтому каждый чанк, чей свет действительно сдвинулся, надо мешить заново.
//
// У большинства он не сдвинулся. Перезаливка запекает всю колонку, потому что
// небесный свет после правки может измениться где угодно ниже, но лопата под
// землёй не меняет ничего: порода была тёмной и осталась тёмной, и сравнение полей
// — это то, что не даёт копке шахты перестраивать девять чанков на удар.
//
// Сравниваются оба канала, и чанк мешится один раз, если сдвинулся хоть один.
// Зажжённый в запертой комнате факел двигает канал блоков и не двигает небесный;
// дыра в крыше — наоборот.
auto world_grid_system::apply_relit_column_(
    light_result& result
) -> void {
    for (int32 y : grid_->column_levels(result.coord)) {
        const auto slot = static_cast<std::size_t>(y - result.bottom_y);
        if (slot >= result.sky.size()) {
            continue;
        }

        const vec3i at{result.coord.x, y, result.coord.y};
        auto* placed = grid_->get_chunk(at);
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
        grid_->remesh_drawn_chunk(at);
        ++stats_.relit_chunks;
    }
}

// Правки называют испорченные ими колонки; здесь они отправляются обратно через
// того же пекаря, который освещал их изначально. Ничего инкрементального тут не
// происходит: колонка заливается заново с нуля, и именно поэтому ответ после
// правки в точности тот, с каким колонку сгенерировали бы.
//
// Колонка, освещаемая прямо сейчас, остаётся грязной, а не спрашивается повторно:
// задача в полёте стартовала от вокселей, которые с тех пор сдвинулись, поэтому её
// результат уже неверен, и следующий кадр всё равно закажет новую.
auto world_grid_system::relight_dirty_columns_() -> void {
    if (baker_ == nullptr) {
        return;
    }

    for (vec2i coord : grid_->take_light_dirty()) {
        light_dirty_.insert(coord);
    }

    if (light_dirty_.empty()) {
        return;
    }

    // Перезаливки соперничают со стримингом за тех же четырёх воркеров, а земля
    // под камерой важнее, чем шахта, осветившаяся кадром раньше.
    static constexpr int32 max_relights_per_frame = 2;
    int32 started                                 = 0;

    std::erase_if(light_dirty_, [&](vec2i coord) -> bool {
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

    stats_.relight_backlog = static_cast<uint32>(light_dirty_.size());
}

auto world_grid_system::stage_completed_columns_() -> void {
    while (true) {
        auto col = loader_->try_pop_completed();
        if (!col) {
            break;
        }

        const auto coord = col->get_coord();
        if (!active_columns_.contains(coord) && !pending_active_columns_.contains(coord)) {
            continue;
        }

        staged_columns_[coord] = std::move(col);

        // Постановка этой колонки может достроить окрестность любой соседней — и
        // её собственную.
        queue_if_ready_(coord);
        for (auto offset : column_neighbor_offsets) {
            queue_if_ready_(coord + offset);
        }
    }
}

auto world_grid_system::model_at_(
    vec3i chunk_coord
) const -> asset::model* {
    if (auto* placed = grid_->get_chunk(chunk_coord)) {
        return placed->get_model().get();
    }

    const auto it = staged_columns_.find(vec2i{chunk_coord.x, chunk_coord.z});
    if (it == staged_columns_.end()) {
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

    while (processed_columns < max_columns_per_frame && !ready_columns_.empty()) {
        const auto coord = ready_columns_.back();
        ready_columns_.pop_back();

        const auto it = staged_columns_.find(coord);
        if (it == staged_columns_.end()) {
            continue;
        }

        // Камера могла сдвинуться с момента постановки в очередь и увести с собой
        // соседа. Тогда обратно в ожидание, а не размещение с дырой сбоку.
        if (!column_ready_(coord)) {
            it->second->set_phase(column_phase::terrain);
            continue;
        }

        // Сначала границы, на всю колонку, и только потом размещение: соседи чанка
        // сверху и снизу — из его же колонки, и если вынуть колонку из накопителя
        // до их чтения, эти грани решат, что смотрят в открытый воздух.
        boundary_from_total += measure_ms([&] -> auto {
            for (auto& [y, cd] : it->second->get_all_chunk_data()) {
                // Каждый сосед, который вообще когда-либо появится, есть уже
                // сейчас — в накопителе или размещённый, — поэтому это последнее
                // слово о границе чанка. Отсутствующий отсутствует навсегда (это
                // открытое небо над колонкой пониже) и читается воздухом.
                for (int32 fd = 0; fd < 6; ++fd) {
                    if (auto* neighbor = model_at_(cd.coord + boundary_face_offsets[fd])) {
                        cd.volume->set_boundary_slice(fd, *neighbor);
                    }
                }
            }
        });

        auto col = std::move(it->second);
        staged_columns_.erase(it);

        std::vector<int32> y_levels;

        for (auto& [y, cd] : col->get_all_chunk_data()) {
            chunk_create_total += measure_ms([&] -> auto {
                grid_->place_chunk(cd.coord, std::move(cd.volume));
            });

            y_levels.push_back(y);
        }

        std::sort(y_levels.begin(), y_levels.end());
        grid_->register_column(coord, std::move(y_levels));
        ++processed_columns;
    }

    stats_.boundary_from_ms = boundary_from_total;
    stats_.chunk_create_ms  = chunk_create_total;
}

auto world_grid_system::dispatch_column_requests_() -> void {
    static constexpr int32 max_requests_per_frame = 8;

    // Потолок на работу в полёте, а не только на начатую за кадр. У загрузчика не
    // было ни того, ни другого: четыре воркера генерировали всё, что заказали, а
    // колонка в состоянии «сгенерирована, но не размещена» держит девять моделей и
    // около 280 страниц. При движении вперёд очередь в обычном прогоне доходила до
    // пика в 482 колонки, и одна заминка главного потока — хватает секунды —
    // уводила это за адресуемый предел пула страниц и убивала процесс. Вместо
    // этого обратное давление: ничего нового не заказывается, пока не пришло
    // заказанное.
    static constexpr uint32 max_columns_in_flight = 96;

    // Свет — второй этап со своей очередью, и колонка, ждущая освещения, держит те
    // же девять моделей, что и ждущая генерации. Считать один загрузчик значило бы
    // позволить генерации от него оторваться.
    const uint32 in_flight =
        loader_->pending_count() + (baker_ != nullptr ? baker_->pending_count() : 0U);

    int32 requests = 0;
    while (!pending_requests_.empty() && requests < max_requests_per_frame &&
           in_flight < max_columns_in_flight) {
        auto coord = pending_requests_.back();
        pending_requests_.pop_back();
        if (loader_->request(coord)) {
            ++requests;
        }
    }
}

auto world_grid_system::update_grid_stats_() -> void {
    stats_.active_count      = static_cast<uint32>(active_columns_.size());
    stats_.pending_count     = loader_->pending_count();
    stats_.loaded_count      = grid_->chunk_count();
    stats_.drawn_count       = grid_->drawn_chunk_count();
    stats_.staged_count      = static_cast<uint32>(staged_columns_.size());
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
    pending_active_columns_.clear();
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

        // На колонку дальше того, что будет нарисовано: кольцо генерируется, чтобы
        // кольцо внутри знало свои границы и было смешено раз и навсегда.
        const auto dist = draw_distance_ + apron_columns;

        for (int32 dx = -dist; dx <= dist; ++dx) {
            for (int32 dz = -dist; dz <= dist; ++dz) {
                int32 cx = camera_column.x + dx;
                int32 cz = camera_column.y + dz;
                pending_active_columns_.insert({cx, cz});
            }
        }
    }

    return camera_column;
}

auto world_grid_system::demote_column_(
    vec2i coord
) -> void {
    // Вне дальности отрисовки, но всё ещё нужна как сосед. Её меши, сущности и
    // инстансы уходят, модели остаются, поэтому возвращение не стоит ничего, а
    // колонка не генерируется дважды.
    auto col = std::make_unique<gen_column>(coord.x, coord.y);

    for (int32 y : grid_->column_levels(coord)) {
        const vec3i chunk_coord{coord.x, y, coord.y};
        if (auto* placed = grid_->get_chunk(chunk_coord)) {
            col->create_chunk(y, chunk_data{chunk_coord, placed->get_volume()});
        }
    }

    grid_->unload_column(coord);

    col->set_phase(column_phase::terrain);
    staged_columns_[coord] = std::move(col);
}

auto world_grid_system::unload_inactive_columns_() -> void {
    for (const auto& coord : active_columns_) {
        if (!pending_active_columns_.contains(coord)) {
            grid_->unload_column(coord);
        } else if (!within_draw_(coord) && grid_->has_column(coord)) {
            demote_column_(coord);
        }
    }

    std::erase_if(staged_columns_, [this](const auto& entry) -> bool {
        return !pending_active_columns_.contains(entry.first);
    });

    // Колонка в очереди, у которой только что пропал сосед, проверяется заново при
    // размещении, поэтому из очереди достаточно убрать исчезнувшие.
    std::erase_if(ready_columns_, [this](vec2i coord) -> bool {
        return !staged_columns_.contains(coord);
    });

    // Камера сдвинулась, поэтому колонки, ждавшие только соседа, могли стать
    // полными, а разжалованные — снова попасть в дальность. Пометка лишь
    // выставляет фазу, так что обходить таблицу по ходу дела безопасно.
    for (const auto& [coord, col] : staged_columns_) {
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

    auto new_chunk_coord = grid_->world_to_chunk_coord({
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
    pending_requests_.clear();

    for (const auto& coord : active_columns_) {
        if (!column_available_(coord) && !loader_->is_pending(coord)) {
            pending_requests_.push_back(coord);
        }
    }

    std::sort(
        pending_requests_.begin(), pending_requests_.end(),
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
    pending_requests_.clear();
    staged_columns_.clear();
    ready_columns_.clear();
    light_dirty_.clear();
    active_columns_.clear();
    pending_active_columns_.clear();
}

auto world_grid_system::clear_loader_transient_state_() -> void {
    pending_requests_.clear();
}

}  // namespace vw::ecs
