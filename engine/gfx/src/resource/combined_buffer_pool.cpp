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
    vk::DescriptorSetLayout compute_descriptor_set_layout
)
    : context_(&context)
    , deletion_(&deletion)
    , staging_(context, 32 * 1024 * 1024)
    , descriptor_pool_(descriptor_pool)
    , descriptor_set_layout_(descriptor_set_layout)
    , compute_descriptor_set_layout_(compute_descriptor_set_layout) {}

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

    stats_.chunk_cull.walk_ms = measure_ms([&] {
        update_chunk_visibility_(world, camera.get_position());
    });

    stats_.timing.staging_flush_ms = measure_ms([&] {
        staging_.flush(cmd);
    });
}

const std::vector<std::unique_ptr<combined_buffer>>& combined_buffer_pool::get_buffers() const {
    return buffers_;
}

auto combined_buffer_pool::process_destroyed_(world_type& world) -> void {
    for (auto ent : world.destroyed()) {
        hidden_entities_.erase(ent);
        if (entity_buffer_infos_.contains(ent)) {
            auto& info = entity_buffer_infos_[ent];
            touched_bounds_.push_back(info.bounds);
            auto swapped = buffers_[info.buffer_index]->free(ent);
            if (swapped && world.has<transform_component>(*swapped)) {
                auto& tc = world.get<transform_component>(*swapped);
                vw::spatial::aabb bounds{};
                if (world.has<spatial_component>(*swapped)) {
                    bounds = world.get<spatial_component>(*swapped)
                                 .get_bounds();
                }
                buffers_[info.buffer_index]->write_transform(
                    *swapped, model_matrix(tc, world.get<model_component>(*swapped)), bounds);
            }
            entity_buffer_infos_.erase(ent);
        }
        chunk_links_.erase(ent);
        sorted_erase(mesh_pending_entities_, ent);
        sorted_erase(transform_pending_entities_, ent);
    }
}

// Шаг в полтора раза, а не вдвое. Меш занимает весь свой класс независимо от того,
// сколько из него использует, поэтому удвоение оставляет в среднем четверть каждого
// слота пустой, а на классе с самыми большими чанками замерено 43%. Степени двойки
// здесь никому не нужны: геометрия — это storage-буфер, читаемый по индексу, а не
// вершинная привязка.
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

// Один шаблон на все меши всех буферов: квад i — это 4i+0, 4i+1, 4i+2, 4i+2, 4i+3,
// 4i+0, а команда отрисовки сдвигает его на нужные квады. Длины ему хватает по
// самому большому классу размера: длиннее своего слота меш не бывает.
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

    // Не через кадровое кольцо staging. У того кольца бюджет на кадр, а проверка
    // границ — assert, поэтому в релизной сборке мегабайт индексов уезжал прямо за
    // окно и поверх данных следующего кадра: испорченные команды отрисовки и мировой
    // проход в тридцать пять миллисекунд. Вместо этого — собственный host-visible
    // буфер с копированием на стороне устройства. Списывается, а не отпускается:
    // копия пока только записана в команды, а классов размера в одном кадре может
    // всплыть несколько, и освобождение источника под ожидающей копией — это висячий
    // дескриптор в vkCmdCopyBuffer.
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

auto combined_buffer_pool::update_meshes_(
    world_type& world, const vec3f& camera_pos, mesh_pool& pool
) -> void {
    auto& model_changed = world.changed<model_component>();
    sorted_merge_range(mesh_pending_entities_, model_changed.begin(), model_changed.end());

    // Расстояние — это два обращения к компонентам, поэтому вычисляется по разу на
    // сущность, а не дважды на сравнение.
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

    constexpr uint32 estimated_avg_mesh_cost = 32 * 1024;
    const uint32 max_mesh_writes = std::clamp(
        static_cast<uint32>(staging_.available() / estimated_avg_mesh_cost), 4u, 16u);
    uint32 mesh_writes = 0;

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
            // Флаг скрытия принадлежал снятой модели. Вернувшаяся модель видима, и
            // ветка трансформа его не перечитает, пока трансформ не тронут.
            hidden_entities_.erase(ent);

            if (entity_buffer_infos_.contains(ent)) {
                auto& buffer_info = entity_buffer_infos_[ent];
                auto swapped = buffers_[buffer_info.buffer_index]->free(ent);
                if (swapped && world.has<transform_component>(*swapped)) {
                    auto& tc = world.get<transform_component>(*swapped);
                    vw::spatial::aabb swap_bounds{};
                    if (world.has<spatial_component>(*swapped)) {
                        swap_bounds = world.get<spatial_component>(*swapped)
                                          .get_bounds();
                    }
                    buffers_[buffer_info.buffer_index]->write_transform(
                        *swapped, model_matrix(tc, world.get<model_component>(*swapped)),
                        swap_bounds);
                }
                entity_buffer_infos_.erase(ent);
            }
            continue;
        }

        const auto& model_comp = world.get<model_component>(ent);
        if (!model_comp.has_model()) {
            merge_buffer_.push_back(ent);
            continue;
        }

        auto model_id = model_comp.get_identity();
        auto mesh_ptr = pool.get(model_id);
        if (!mesh_ptr) {
            merge_buffer_.push_back(ent);
            continue;
        }

        // До проверки на пустой меш ниже: у чанка сплошной породы нет геометрии, но
        // взгляд он всё равно перекрывает.
        chunk_links_[ent] = mesh_ptr->links;

        const auto quad_count = static_cast<uint32>(mesh_ptr->quads.size());

        if (quad_count == 0) {
            continue;
        }

        buffer_chunk_size required_chunk_size = get_chunk_size_for_mesh(quad_count);

        // Через staging теперь едет только сам меш: остаток класса размера остаётся
        // как был и не рисуется.
        const vk::DeviceSize mesh_staging_cost = (quad_count * sizeof(quad)) + sizeof(uint32) +
            sizeof(draw_command) + (sizeof(mat4f) * 2);

        const auto& transform_comp = world.get<transform_component>(ent);
        const mat4f transform_matrix =
            model_matrix(transform_comp, world.get<model_component>(ent));

        vw::spatial::aabb ent_bounds{};
        if (world.has<spatial_component>(ent)) {
            ent_bounds = world.get<spatial_component>(ent).get_bounds();
        }

        if (entity_buffer_infos_.contains(ent)) {
            auto& buffer_info = entity_buffer_infos_[ent];
            auto& buffer      = buffers_[buffer_info.buffer_index];

            if (buffer_info.chunk_size == required_chunk_size) {
                const auto& ent_alloc = buffer->get_entity_allocation(ent);
                if (ent_alloc.model_index == model_id.index) {
                    if (staging_.available() < mesh_staging_cost) {
                        merge_buffer_.push_back(ent);
                        continue;
                    }
                    buffer->write_mesh(model_id, *mesh_ptr);
                    uploaded_models_.push_back(model_id);
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

            auto swapped = buffer->free(ent);
            if (swapped && world.has<transform_component>(*swapped)) {
                auto& tc = world.get<transform_component>(*swapped);
                vw::spatial::aabb sw_bounds{};
                if (world.has<spatial_component>(*swapped)) {
                    sw_bounds = world.get<spatial_component>(*swapped)
                                    .get_bounds();
                }
                buffer->write_transform(
                    *swapped, model_matrix(tc, world.get<model_component>(*swapped)), sw_bounds);
            }
        } else if (staging_.available() < mesh_staging_cost) {
            merge_buffer_.push_back(ent);
            continue;
        }

        auto* buffer            = get_or_create_buffer(required_chunk_size);
        const auto buffer_index = chunk_size_to_buffer_index_[required_chunk_size];

        buffer->allocate(ent, model_id, *mesh_ptr, transform_matrix, ent_bounds);
        uploaded_models_.push_back(model_id);

        entity_buffer_infos_[ent] =
            entity_buffer_info{required_chunk_size, buffer_index, ent_bounds};
        touched_bounds_.push_back(ent_bounds);
        ++mesh_writes;
    }

    std::sort(merge_buffer_.begin(), merge_buffer_.end());
    mesh_pending_entities_.swap(merge_buffer_);
    stats_.mesh_pending = static_cast<uint32>(mesh_pending_entities_.size());

    evict_uploaded_(world, pool);
}

// Копия меша на CPU уходит, как только геометрия оказалась на GPU: колонка рельефа
// — это мегабайты, и второй раз её никто не читает. Чего ей делать нельзя, так это
// уходить, пока в очереди на ту же модель стоит другая сущность. Копия существует
// только в mesh_pool, и сущность, не нашедшая её, возвращается в список ожидающих —
// где её уже никто и никогда не построит, потому что меш заказывается для
// model_component только в тот кадр, когда он меняется.
//
// В это и упирались две сущности, делящие одну модель. Первая загружалась, копию
// отпускали, и все остальные ждали вечно. Сторона GPU проблемой не была никогда:
// combined_buffer ключует геометрию по индексу модели и считает ссылки, поэтому
// вторая сущность делит те же квады, что записала первая.
auto combined_buffer_pool::evict_uploaded_(
    world_type& world, mesh_pool& pool
) -> void {
    if (uploaded_models_.empty()) {
        return;
    }

    // По идентичности, а не по индексу: сущность, стоящая в очереди за более новым
    // поколением той же модели, ждёт другой меш, и придерживать ради неё старый
    // значило бы держать живой память, которую никто не прочтёт.
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

    for (const auto& model_id : uploaded_models_) {
        if (!awaited_models_.contains(model_id)) {
            pool.evict(model_id);
        }
    }
}

// Скрытые объёмы гасятся тем же флагом, что и отсечённые чанки: геометрия остаётся
// в буфере, и вернуть объём — значит лишь снова поднять флаг, а не заказывать меш,
// копия которого на CPU давно отпущена. Обходится только список скрытых, а не все
// модели: в мире их — каждый чанк.
auto combined_buffer_pool::hide_marked_() -> void {
    for (const auto ent : hidden_entities_) {
        const auto it = entity_buffer_infos_.find(ent);
        if (it == entity_buffer_infos_.end()) {
            continue;
        }

        const auto buffer = it->second.buffer_index;
        const auto slot   = buffers_[buffer]->get_entity_allocation(ent).instance_index;
        visibility_flags_[buffer][slot] = 0U;
    }
}

auto combined_buffer_pool::update_chunk_visibility_(
    world_type& world, const vec3f& camera_pos
) -> void {
    // Всё начинается видимым. Скрываются только чанки: у персонажа или предмета нет
    // ни связности, ни места в обходе.
    visibility_flags_.resize(buffers_.size());
    for (std::size_t i = 0; i < buffers_.size(); ++i) {
        visibility_flags_[i].assign(buffers_[i]->get_stats().instance_capacity, 1U);
    }

    hide_marked_();

    stats_.chunk_cull = chunk_cull_stats{};

    auto* grid = world.system<ecs::world_grid_system>().grid();
    if (!chunk_cull_enabled_ || grid == nullptr) {
        for (std::size_t i = 0; i < buffers_.size(); ++i) {
            buffers_[i]->write_visibility(visibility_flags_[i]);
        }
        return;
    }

    const auto slot_of = [&](entity ent) -> std::optional<std::pair<std::size_t, uint32>> {
        if (!ent.is_valid()) {
            return std::nullopt;
        }
        const auto it = entity_buffer_infos_.find(ent);
        if (it == entity_buffer_infos_.end()) {
            return std::nullopt;
        }
        const auto index = buffers_[it->second.buffer_index]->get_entity_allocation(ent).instance_index;
        return std::pair{it->second.buffer_index, index};
    };

    // Скрыть все чанки, а затем позволить обходу вернуть те, до которых достаёт
    // взгляд. Тот же проход собирает вертикальный размах мира: выше и ниже него
    // чанков нет вовсе, и обход разошёлся бы по этой пустоте и спустился бы куда
    // угодно.
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

    // Открытое небо над миром реально, и камера стоит в нём. Под миром же нет ничего
    // вовсе, а поскольку незагруженные чанки считаются открытыми, пуск обхода в этот
    // пустой слой позволяет ему пройти под всем и подняться с другой стороны — так
    // здесь и намерили в первый раз 0% скрытого. Пол обхода — это пол загруженного
    // мира.
    lo.y = std::min(lo.y, camera_chunk.y);
    hi.y = std::max(hi.y + 1, camera_chunk.y);

    // Дальше обход работает в ячейках связности, по нескольку на чанк.
    constexpr int32 per_side = vw::asset::chunk_links::cells_per_side;
    constexpr int32 cell_voxels =
        vw::asset::chunk_links::cell_size * 1;  // in voxels, before voxel_scale

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

    // Ячейка, в которой стоит камера, внутри её чанка.
    const int32 scaled_cell = cell_voxels * grid->voxel_scale();
    const auto cell_of      = [scaled_cell](int32 world) -> int32 {
        return world >= 0 ? world / scaled_cell : (world - scaled_cell + 1) / scaled_cell;
    };
    const vec3i origin{
        cell_of(camera_voxel.x), cell_of(camera_voxel.y), cell_of(camera_voxel.z)};

    ecs::walk_visible_chunks(
        origin, cell_lo, cell_hi,
        [&](vec3i cell) -> const vw::asset::cell_links* {
            // Сплошную породу никогда не мешат, поэтому собственных связей у неё
            // нет, — но это ровно то, чего обходу нельзя считать неизвестным:
            // неизвестное читается как полностью открытое, и взгляд прошёл бы прямо
            // сквозь коренную породу.
            static const vw::asset::cell_links sealed{};

            auto* c = grid->get_chunk(
                vec3i{to_chunk(cell.x), to_chunk(cell.y), to_chunk(cell.z)});
            if (c == nullptr) {
                return nullptr;  // not loaded
            }
            if (c->is_solid()) {
                return &sealed;
            }
            const auto it = chunk_links_.find(c->get_entity());
            if (it == chunk_links_.end()) {
                return nullptr;  // not meshed yet, so nothing is known
            }
            return &it->second.cells[vw::asset::chunk_links::cell_index(
                to_sub(cell.x), to_sub(cell.y), to_sub(cell.z))];
        },
        [&, world_top = cell_hi.y](vec3i cell) -> bool {
            // Небо — это всё, что выше верхушки данной колонки мира. Загруженная
            // область — диск внутри квадратной коробки, поэтому колонок по углам нет
            // вовсе, и объявление их небом на любой высоте позволяло обходу
            // спуститься по углам и разойтись обратно под миром. Безопасный ответ
            // там только один — «выше всего».
            const auto it = column_top_.find(vec2i{to_chunk(cell.x), to_chunk(cell.z)});
            if (it == column_top_.end()) {
                return cell.y >= world_top;
            }
            return to_chunk(cell.y) > it->second;
        },
        [&](const vw::asset::chunk_pocket& pocket) -> bool {
            // В каком кармане своей ячейки стоит камера.
            const int32 voxels = grid->voxel_scale();
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
    auto& transform_changed = world.changed<transform_component>();
    sorted_merge_range(
        transform_pending_entities_, transform_changed.begin(), transform_changed.end());

    entities_to_process_.assign(
        transform_pending_entities_.begin(), transform_pending_entities_.end());

    merge_buffer_.clear();
    merge_buffer_.reserve(entities_to_process_.size());

    for (std::size_t i = 0; i < entities_to_process_.size(); ++i) {
        entity ent = entities_to_process_[i];

        // До проверки буфера: скрыть можно и то, что ещё не загружено, и флаг
        // обязан дождаться его появления.
        if (world.has<model_component>(ent) && !world.get<model_component>(ent).is_visible()) {
            hidden_entities_.insert(ent);
        } else {
            hidden_entities_.erase(ent);
        }

        if (!entity_buffer_infos_.contains(ent)) {
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

        auto& info = entity_buffer_infos_[ent];
        const auto& transform_comp = world.get<transform_component>(ent);
        vw::spatial::aabb tr_bounds{};
        if (world.has<spatial_component>(ent)) {
            tr_bounds = world.get<spatial_component>(ent).get_bounds();
        }
        buffers_[info.buffer_index]->write_transform(
            ent, model_matrix(transform_comp, world.get<model_component>(ent)), tr_bounds);
        touched_bounds_.push_back(info.bounds);
        touched_bounds_.push_back(tr_bounds);
        info.bounds = tr_bounds;
    }

    std::sort(merge_buffer_.begin(), merge_buffer_.end());
    transform_pending_entities_.swap(merge_buffer_);
    stats_.transform_pending = static_cast<uint32>(transform_pending_entities_.size());
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
