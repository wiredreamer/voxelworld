module vw.world;

import std;
import vw.core;
import vw.asset;

namespace vw::ecs {

world_grid::world_grid(
    world& w, int32 world_units_per_voxel
)
    : world_(&w), world_units_per_voxel_(world_units_per_voxel) {}

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

    mark_light_dirty_(cc, lc);
    refresh_chunk(cc);

    for (const face_direction face : all_face_directions) {
        if (lc[axis_of(face)] == boundary_layer(face, chunk::size)) {
            refresh_chunk(cc + offset_of(face));
        }
    }
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

    for (const face_direction face : all_face_directions) {
        const auto neighbor = chunks_.find(chunk_coord + offset_of(face));
        if (neighbor == chunks_.end()) {
            continue;
        }
        vol.set_boundary_slice(face, neighbor->second->get_volume()->voxels());
        mask |= face_bit(face);
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
        }
        column_chunks_.erase(col_it);
    }
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
