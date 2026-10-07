module vw.world;

import std;
import vw.core;
import vw.asset;

namespace vw::ecs {

namespace {

auto fresh_occupancy_serial() -> uint64 {
    static std::atomic<uint64> grids_made{1};
    return grids_made.fetch_add(1) << 32;
}

}  // namespace

world_grid::world_grid(
    world& w, int32 world_units_per_voxel
)
    : world_(&w)
    , world_units_per_voxel_(world_units_per_voxel)
    , occupancy_first_serial_(fresh_occupancy_serial()) {}

auto world_grid::get_voxel(
    vec3i world_pos
) const -> voxel {
    auto cc = world_to_chunk_coord(world_pos);
    auto it = chunks_.find(cc);
    if (it == chunks_.end()) {
        return voxels::air;
    }
    auto lc = world_to_local_coord(world_pos);
    return it->second->get_voxel(lc / world_units_per_voxel_);
}

auto world_grid::light_at(
    const vec3f& world_pos
) const -> world_light {
    constexpr auto full = static_cast<float32>(light_column::max_level);

    const auto vs = static_cast<float32>(world_units_per_voxel_);
    const vec3f cell{
        (world_pos.x / vs) - 0.5F, (world_pos.y / vs) - 0.5F, (world_pos.z / vs) - 0.5F
    };
    const vec3i base{
        static_cast<int32>(std::floor(cell.x)), static_cast<int32>(std::floor(cell.y)),
        static_cast<int32>(std::floor(cell.z))
    };
    const vec3f frac{
        cell.x - static_cast<float32>(base.x), cell.y - static_cast<float32>(base.y),
        cell.z - static_cast<float32>(base.z)
    };

    float32 weight = 0.0F;
    float32 sky    = 0.0F;
    float32 block  = 0.0F;

    for (int32 corner = 0; corner < 8; ++corner) {
        const vec3i step{corner & 1, (corner >> 1) & 1, (corner >> 2) & 1};
        const vec3i voxel_pos = base + step;
        const float32 w = (step.x != 0 ? frac.x : 1.0F - frac.x) *
                          (step.y != 0 ? frac.y : 1.0F - frac.y) *
                          (step.z != 0 ? frac.z : 1.0F - frac.z);

        const vec3i at = voxel_pos * world_units_per_voxel_;
        const auto it  = chunks_.find(world_to_chunk_coord(at));
        if (it == chunks_.end()) {
            weight += w;
            sky += w * full;
            continue;
        }

        const vec3i local = world_to_local_coord(at) / world_units_per_voxel_;
        if (!it->second->get_voxel(local).is_empty()) {
            continue;
        }

        const auto& volume = *it->second->get_volume();
        const auto* sky_field   = volume.get_sky_light();
        const auto* block_field = volume.get_block_light();
        if (sky_field == nullptr) {
            continue;
        }

        weight += w;
        sky += w * static_cast<float32>(sky_field->level_at(local));
        if (block_field != nullptr) {
            block += w * static_cast<float32>(block_field->level_at(local));
        }
    }

    if (weight <= 0.0001F) {
        return {};
    }

    return {.sky = sky / (weight * full), .block = block / (weight * full)};
}

auto world_grid::set_voxel(
    vec3i world_pos, voxel v
) -> void {
    auto cc = world_to_chunk_coord(world_pos);
    auto it = chunks_.find(cc);
    if (it == chunks_.end()) {
        return;
    }
    const auto lc = world_to_local_coord(world_pos) / world_units_per_voxel_;
    it->second->set_voxel(lc, v);

    const vec3i at = (cc * chunk::size) + lc;
    set_cover_(v.is_empty() ? at : at - vec3i{0, 1, 0}, 0);

    mark_light_dirty_(cc, lc);
    note_occupancy_change_(cc);
    refresh_chunk(cc);

    for (const face_direction face : all_face_directions) {
        if (lc[axis_of(face)] == boundary_layer(face, chunk::size)) {
            refresh_chunk(cc + offset_of(face));
        }
    }
}

auto world_grid::cell_of(
    const vec3f& world_pos
) const -> vec3i {
    const auto vs = static_cast<float32>(world_units_per_voxel_);
    return {
        static_cast<int32>(std::floor(world_pos.x / vs)), static_cast<int32>(std::floor(world_pos.y / vs)),
        static_cast<int32>(std::floor(world_pos.z / vs))
    };
}

auto world_grid::chunk_holding_(
    vec3i at
) const -> const chunk* {
    const auto it = chunks_.find(world_to_chunk_coord(at * world_units_per_voxel_));
    return it != chunks_.end() ? it->second.get() : nullptr;
}

auto world_grid::chunk_holding_(
    vec3i at
) -> chunk* {
    const auto it = chunks_.find(world_to_chunk_coord(at * world_units_per_voxel_));
    return it != chunks_.end() ? it->second.get() : nullptr;
}

auto world_grid::cell_at(
    vec3i at
) const -> std::optional<cell> {
    const vec3i chunk_coord = world_to_chunk_coord(at * world_units_per_voxel_);
    if (!column_chunks_.contains(vec2i{chunk_coord.x, chunk_coord.z})) {
        return std::nullopt;
    }

    if (const chunk* holder = chunk_holding_(at); holder != nullptr) {
        const voxel own = holder->get_voxel(world_to_local_coord(at * world_units_per_voxel_) / world_units_per_voxel_);
        if (!own.is_empty()) {
            return cell{.kind = occupant_kind::terrain, .look = own};
        }
    }

    const vec3i support = at - vec3i{0, 1, 0};
    const chunk* ground = chunk_holding_(support);
    if (ground == nullptr) {
        return cell{};
    }
    const vec3i local = world_to_local_coord(support * world_units_per_voxel_) / world_units_per_voxel_;
    const uint8 form  = ground->get_volume()->cover().form_at(local);
    if (form == 0) {
        return cell{};
    }
    return cell{
        .kind = is_flower_form(form) ? occupant_kind::flower : occupant_kind::grass,
        .look = ground->get_voxel(local),
        .form = form,
    };
}

auto world_grid::plant_cover(
    vec3i at, uint8 form
) -> std::expected<void, std::string> {
    if (form == 0 || form > cover_form_count) {
        return std::unexpected{std::format("cover form {} is out of 1..{}", form, cover_form_count)};
    }
    const auto here = cell_at(at);
    if (!here) {
        return std::unexpected{"the column of the cell is not loaded"};
    }
    if (here->kind == occupant_kind::terrain) {
        return std::unexpected{"the cell is taken by terrain"};
    }
    const auto below = cell_at(at - vec3i{0, 1, 0});
    if (!below || below->kind != occupant_kind::terrain) {
        return std::unexpected{"there is no ground under the cell"};
    }
    set_cover_(at - vec3i{0, 1, 0}, form);
    return {};
}

auto world_grid::clear_cell(
    vec3i at
) -> void {
    const auto here = cell_at(at);
    if (!here) {
        return;
    }
    switch (here->kind) {
        case occupant_kind::terrain:
            set_voxel(at * world_units_per_voxel_, voxels::air);
            break;
        case occupant_kind::grass:
        case occupant_kind::flower:
            set_cover_(at - vec3i{0, 1, 0}, 0);
            break;
        case occupant_kind::empty:
            break;
    }
}

auto world_grid::set_cover_(
    vec3i support, uint8 form
) -> void {
    chunk* ground = chunk_holding_(support);
    if (ground == nullptr) {
        return;
    }
    const vec3i local = world_to_local_coord(support * world_units_per_voxel_) / world_units_per_voxel_;
    ground->get_volume()->cover().set(local, form);
}

auto world_grid::mark_light_dirty_(
    vec3i chunk_coord, vec3i local
) -> void {
    constexpr int32 reach = ecs::light_column::max_level;
    static_assert(reach * 2 < chunk::size, "an edit must not reach past the next column");

    const vec2i column{chunk_coord.x, chunk_coord.z};
    light_dirty_.insert(column);

    const auto side_of = [](int32 at) -> int32 {
        if (at + 1 <= reach) {
            return -1;
        }
        return (chunk::size - at) <= reach ? 1 : 0;
    };

    const int32 dx = side_of(local.x);
    const int32 dz = side_of(local.z);

    if (dx != 0) {
        light_dirty_.insert(column + vec2i{dx, 0});
    }
    if (dz != 0) {
        light_dirty_.insert(column + vec2i{0, dz});
    }
    if (dx != 0 && dz != 0) {
        light_dirty_.insert(column + vec2i{dx, dz});
    }
}

auto world_grid::take_light_dirty() -> std::vector<vec2i> {
    std::vector<vec2i> out(light_dirty_.begin(), light_dirty_.end());
    light_dirty_.clear();
    return out;
}

auto world_grid::remesh_drawn_chunk(
    vec3i chunk_coord
) -> void {
    const auto it = chunks_.find(chunk_coord);
    if (it == chunks_.end() || !it->second->is_drawn()) {
        return;
    }
    refresh_chunk(chunk_coord);
}

auto world_grid::refresh_chunk(
    vec3i chunk_coord
) -> void {
    const auto it = chunks_.find(chunk_coord);
    if (it == chunks_.end()) {
        return;
    }

    auto& c    = *it->second;
    auto& vol  = *c.get_volume();
    uint8 mask = 0;

    for (const vec3i step : all_shell_steps()) {
        const auto neighbor = chunks_.find(chunk_coord + step);
        if (neighbor == chunks_.end()) {
            continue;
        }
        vol.set_boundary_shell(step, neighbor->second->get_volume()->voxels());
        if (shell_span(step) == 1) {
            mask |= face_bit(shell_face(step));
        }
    }

    c.set_known_neighbors(mask);

    if (c.ensure_entity()) {
        ++drawn_chunks_;
    }

    if (c.get_entity().is_valid()) {
        world_->registry().request_change<model_component>(c.get_entity());
    }
}

auto world_grid::has_chunk(
    vec3i chunk_coord
) const -> bool {
    return chunks_.contains(chunk_coord);
}

auto world_grid::get_chunk(
    vec3i chunk_coord
) -> chunk* {
    auto it = chunks_.find(chunk_coord);
    return it != chunks_.end() ? it->second.get() : nullptr;
}

auto world_grid::find_chunk(
    vec3i chunk_coord
) const -> const chunk* {
    const auto it = chunks_.find(chunk_coord);
    return it != chunks_.end() ? it->second.get() : nullptr;
}

auto world_grid::get_surface_voxel_y(
    int32 voxel_x, int32 voxel_z
) const -> std::optional<int32> {
    constexpr int32 s = chunk::size;

    auto floor_div = [](int32 a, int32 b) -> int32 { return a >= 0 ? a / b : (a - b + 1) / b; };
    int32 cx       = floor_div(voxel_x, s);
    int32 cz       = floor_div(voxel_z, s);
    vec2i col_coord{cx, cz};

    auto col_it = column_chunks_.find(col_coord);
    if (col_it == column_chunks_.end() || col_it->second.empty()) {
        return std::nullopt;
    }

    const auto& y_levels = col_it->second;

    int32 local_x = ((voxel_x % s) + s) % s;
    int32 local_z = ((voxel_z % s) + s) % s;

    for (auto it = y_levels.rbegin(); it != y_levels.rend(); ++it) {
        int32 cy = *it;
        vec3i chunk_coord{cx, cy, cz};
        auto chunk_it = chunks_.find(chunk_coord);
        if (chunk_it == chunks_.end()) {
            continue;
        }

        for (int32 local_y = s - 1; local_y >= 0; --local_y) {
            if (!chunk_it->second->get_voxel(local_x, local_y, local_z).is_empty()) {
                return (cy * s) + local_y;
            }
        }
    }

    return std::nullopt;
}

auto world_grid::has_column(
    vec2i coord
) const -> bool {
    return column_chunks_.contains(coord);
}

auto world_grid::column_levels(
    vec2i coord
) const -> std::span<const int32> {
    const auto it = column_chunks_.find(coord);
    return it != column_chunks_.end() ? std::span<const int32>{it->second}
                                      : std::span<const int32>{};
}

auto world_grid::column_count() const -> uint32 {
    return static_cast<uint32>(column_chunks_.size());
}

auto world_grid::chunk_count() const -> uint32 {
    return static_cast<uint32>(chunks_.size());
}

auto world_grid::drawn_chunk_count() const -> uint32 {
    return drawn_chunks_;
}

auto world_grid::place_chunk(
    vec3i chunk_coord, std::shared_ptr<asset::chunk_volume> volume
) -> chunk* {
    auto [it, inserted] = chunks_.emplace(
        chunk_coord,
        std::make_unique<chunk>(*world_, chunk_coord, std::move(volume), world_units_per_voxel_)
    );

    if (inserted && it->second->is_drawn()) {
        ++drawn_chunks_;
    }
    note_occupancy_change_(chunk_coord);

    return it->second.get();
}

auto world_grid::register_column(
    vec2i coord, std::vector<int32> y_levels
) -> void {
    column_chunks_[coord] = std::move(y_levels);
}

auto world_grid::unload_column(
    vec2i coord
) -> void {
    auto col_it = column_chunks_.find(coord);
    if (col_it != column_chunks_.end()) {
        for (int32 y : col_it->second) {
            vec3i chunk_coord{coord.x, y, coord.y};
            const auto it = chunks_.find(chunk_coord);
            if (it == chunks_.end()) {
                continue;
            }
            if (it->second->is_drawn()) {
                --drawn_chunks_;
            }
            chunks_.erase(it);
            note_occupancy_change_(chunk_coord);
        }
        column_chunks_.erase(col_it);
    }
}

auto world_grid::occupancy_serial() const -> uint64 {
    return occupancy_first_serial_ + occupancy_log_.size();
}

auto world_grid::occupancy_changes_since(uint64 serial) const
    -> std::optional<std::span<const vec3i>> {
    if (serial < occupancy_first_serial_ || serial > occupancy_serial()) {
        return std::nullopt;
    }
    return std::span<const vec3i>{occupancy_log_}.subspan(
        static_cast<std::size_t>(serial - occupancy_first_serial_)
    );
}

auto world_grid::note_occupancy_change_(vec3i chunk_coord) -> void {
    constexpr std::size_t kept_changes = 4096;

    if (occupancy_log_.size() >= kept_changes * 2) {
        occupancy_log_.erase(
            occupancy_log_.begin(),
            occupancy_log_.begin() + static_cast<std::ptrdiff_t>(kept_changes)
        );
        occupancy_first_serial_ += kept_changes;
    }
    occupancy_log_.push_back(chunk_coord);
}

auto world_grid::world_units_per_voxel() const -> int32 {
    return world_units_per_voxel_;
}

auto world_grid::world_to_chunk_coord(
    vec3i world_pos
) const -> vec3i {
    const int32 s = chunk::size * world_units_per_voxel_;
    return {
        world_pos.x >= 0 ? world_pos.x / s : (world_pos.x - s + 1) / s,
        world_pos.y >= 0 ? world_pos.y / s : (world_pos.y - s + 1) / s,
        world_pos.z >= 0 ? world_pos.z / s : (world_pos.z - s + 1) / s
    };
}

auto world_grid::world_to_local_coord(
    vec3i world_pos
) const -> vec3i {
    const int32 s = chunk::size * world_units_per_voxel_;
    return {((world_pos.x % s) + s) % s, ((world_pos.y % s) + s) % s, ((world_pos.z % s) + s) % s};
}

auto world_grid::chunk_to_world_coord(
    vec3i chunk_coord
) const -> vec3i {
    const int32 s = chunk::size * world_units_per_voxel_;
    return {chunk_coord.x * s, chunk_coord.y * s, chunk_coord.z * s};
}

}  // namespace vw::ecs
