#include <catch2/catch_test_macros.hpp>

import std;

import vw.core;
import vw.asset;
import vw.ecs;
import vw.world;

using namespace vw;
using namespace vw::ecs;

namespace {

constexpr int32 view_distance = 2;

auto shallow_params() -> perlin_terrain_generator::params {
    perlin_terrain_generator::params p{};
    p.world_bottom_y = -192;
    p.island         = false;
    hills_biome hills{};
    p.biomes         = {hills};
    return p;
}

class settled_grid {
public:
    settled_grid(world& w, job_system& jobs) : world_{&w}, jobs_{&jobs} {
        install(shallow_params());

        viewer_ = w.create().with<transform_component>().with<world_view_component>().get_entity();
        w.system<world_grid_system>().modify_view(viewer_).set_view_distance(view_distance);

        settle();
    }

    auto install(const perlin_terrain_generator::params& params) -> void {
        auto& models = world_->resource<asset::model_registry>();
        auto& gs     = world_->system<world_grid_system>();

        gs.set_grid(std::make_unique<world_grid>(*world_, 8));
        gs.set_loader(
            std::make_unique<chunk_loader>(
                std::make_unique<perlin_terrain_generator>(
                    models.get_identity_pool(), models.get_page_pool(), params
                ),
                *jobs_
            ),
            *jobs_
        );
        first_seen_.clear();
    }

    auto settle() -> void {
        auto& gs = world_->system<world_grid_system>();

        int32 quiet = 0;

        for (int32 frame = 0; frame < max_frames && quiet < quiet_frames; ++frame) {
            world_->update(0.016F);
            observe_();

            const auto& stats  = gs.get_stats();
            const bool waiting = stats.pending_count > 0 || stats.lighting_count > 0 ||
                                 stats.relight_backlog > 0;
            quiet = (waiting || placed_this_frame_ > 0) ? 0 : quiet + 1;

            if (waiting && placed_this_frame_ == 0) {
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
        }
    }

    [[nodiscard]] auto grid() const -> const world_grid& {
        return *world_->system<world_grid_system>().grid();
    }

    [[nodiscard]] auto reissued() const -> std::size_t {
        std::size_t count = 0;

        world_->system<world_grid_system>().grid()->for_each_chunk(
            [&](vec3i coord, const chunk& c) {
                const auto it = first_seen_.find(coord);
                if (it != first_seen_.end() && !(it->second == c.get_model()->get_identity())) {
                    ++count;
                }
            }
        );

        return count;
    }

    [[nodiscard]] auto seen_count() const -> std::size_t {
        return first_seen_.size();
    }

private:
    static constexpr int32 max_frames  = 4000;
    static constexpr int32 quiet_frames = 120;

    auto observe_() -> void {
        placed_this_frame_ = 0;

        world_->system<world_grid_system>().grid()->for_each_chunk(
            [&](vec3i coord, const chunk& c) {
                if (first_seen_.try_emplace(coord, c.get_model()->get_identity()).second) {
                    ++placed_this_frame_;
                }
            }
        );
    }

    world* world_;
    job_system* jobs_;
    entity viewer_;
    std::unordered_map<vec3i, asset::model_identity> first_seen_;
    int32 placed_this_frame_ = 0;
};

auto light_at(world_grid& grid, vec3i world_pos) -> std::optional<int32> {
    auto* c = grid.get_chunk(grid.world_to_chunk_coord(world_pos));
    if (c == nullptr) {
        return std::nullopt;
    }

    const auto* light = c->get_volume()->get_sky_light();
    if (light == nullptr) {
        return std::nullopt;
    }

    return light->level_at(grid.world_to_local_coord(world_pos) / grid.world_units_per_voxel());
}

auto solid_shaft_site(world_grid& grid, vec2i column, int32 depth) -> std::optional<vec3i> {
    const auto levels = grid.column_levels(column);
    if (levels.empty()) {
        return std::nullopt;
    }

    const int32 scale = grid.world_units_per_voxel();
    const int32 span  = chunk::size * scale;
    const int32 top   = ((levels.back() + 1) * span) - scale;
    const int32 floor = levels.front() * span;

    for (int32 lz = 16; lz < 48; ++lz) {
        for (int32 lx = 16; lx < 48; ++lx) {
            const int32 wx = (column.x * span) + (lx * scale);
            const int32 wz = (column.y * span) + (lz * scale);

            int32 y = top;
            while (y >= floor && grid.get_voxel(vec3i{wx, y, wz}).is_empty()) {
                y -= scale;
            }

            const int32 deepest = y - ((depth - 1) * scale);
            if (y < floor || deepest < floor) {
                continue;
            }

            bool all_rock = true;
            for (int32 at = y; at >= deepest && all_rock; at -= scale) {
                all_rock = !grid.get_voxel(vec3i{wx, at, wz}).is_empty();
            }

            if (all_rock) {
                return vec3i{wx, y, wz};
            }
        }
    }

    return std::nullopt;
}

}  // namespace

TEST_CASE("a placed chunk is never reissued", "[world][grid]") {
    job_system jobs;
    world w;
    const settled_grid settled{w, jobs};

    REQUIRE(settled.seen_count() > 0);

    REQUIRE(settled.reissued() == 0);
}

TEST_CASE("a new generator fills the view without the viewer moving", "[world][grid]") {
    job_system jobs;
    world w;
    settled_grid settled{w, jobs};
    REQUIRE(settled.seen_count() > 0);

    auto reseeded = shallow_params();
    reseeded.seed = 7;
    settled.install(reseeded);
    settled.settle();

    REQUIRE(settled.seen_count() > 0);
    REQUIRE(settled.grid().has_column({0, 0}));
}

TEST_CASE("a placed chunk knows every neighbour it has", "[world][grid]") {
    job_system jobs;
    world w;
    const settled_grid settled{w, jobs};

    auto& grid = *w.system<world_grid_system>().grid();

    std::size_t checked = 0;
    std::size_t missing = 0;

    grid.for_each_chunk([&](vec3i coord, const chunk& c) {
        for (const face_direction face : all_face_directions) {
            if (!grid.has_chunk(coord + offset_of(face))) {
                continue;
            }
            ++checked;
            if ((c.known_neighbors() & face_bit(face)) == 0) {
                ++missing;
            }
        }
    });

    REQUIRE(checked > 0);
    REQUIRE(missing == 0);
}

TEST_CASE("buried rock costs no entity", "[world][grid]") {
    job_system jobs;
    world w;
    const settled_grid settled{w, jobs};

    auto& grid = *w.system<world_grid_system>().grid();

    constexpr int32 s = chunk::size;

    const auto seam_is_solid = [&](const chunk& neighbor, face_direction toward) -> bool {
        const face_direction facing_back = opposite(toward);
        const int32 seam                 = boundary_layer(facing_back, s);

        for (int32 a = 0; a < s; ++a) {
            for (int32 b = 0; b < s; ++b) {
                const vec3i local = lift_off_face_plane(facing_back, vec2i{a, b}, seam);

                if (neighbor.get_voxel(local).is_empty()) {
                    return false;
                }
            }
        }
        return true;
    };

    std::size_t skipped  = 0;
    std::size_t verified = 0;

    grid.for_each_chunk([&](vec3i coord, const chunk& c) {
        if (c.is_drawn()) {
            return;
        }
        ++skipped;

        INFO("chunk " << coord.x << "," << coord.y << "," << coord.z);

        if (!c.is_solid()) {
            bool all_air = true;
            for (int32 x = 0; x < s && all_air; ++x) {
                for (int32 y = 0; y < s && all_air; ++y) {
                    for (int32 z = 0; z < s && all_air; ++z) {
                        all_air = c.is_empty(x, y, z);
                    }
                }
            }
            REQUIRE(all_air);
            return;
        }

        for (const face_direction face : all_face_directions) {
            INFO("face " << static_cast<int32>(face));

            auto* neighbor = grid.get_chunk(coord + offset_of(face));
            if (neighbor == nullptr) {
                REQUIRE((c.known_neighbors() & face_bit(face)) != 0);
                continue;
            }

            REQUIRE(seam_is_solid(*neighbor, face));
            ++verified;
        }
    });

    REQUIRE(skipped > 0);
    REQUIRE(verified > 0);
    REQUIRE(grid.drawn_chunk_count() == grid.chunk_count() - skipped);
}

TEST_CASE("a chunk with no entity still holds its voxels", "[world][grid]") {
    job_system jobs;
    world w;
    const settled_grid settled{w, jobs};

    auto& grid = *w.system<world_grid_system>().grid();

    std::size_t checked = 0;

    grid.for_each_chunk([&](vec3i coord, const chunk& c) {
        if (c.is_drawn() || !c.is_solid()) {
            return;
        }
        ++checked;

        INFO("chunk " << coord.x << "," << coord.y << "," << coord.z);
        REQUIRE_FALSE(c.is_empty(0, 0, 0));
        REQUIRE_FALSE(c.is_empty(chunk::size - 1, chunk::size - 1, chunk::size - 1));
    });

    REQUIRE(checked > 0);
}

TEST_CASE("digging a seam tells both sides", "[world][grid]") {
    job_system jobs;
    world w;
    const settled_grid settled{w, jobs};

    auto& grid = *w.system<world_grid_system>().grid();

    std::optional<vec3i> target;
    grid.for_each_chunk([&](vec3i coord, const chunk& c) {
        if (target || c.is_drawn() || !c.is_solid()) {
            return;
        }
        if (grid.has_chunk(coord + vec3i{1, 0, 0})) {
            target = coord;
        }
    });

    REQUIRE(target.has_value());

    auto* digger = grid.get_chunk(*target);
    auto* east   = grid.get_chunk(*target + vec3i{1, 0, 0});
    REQUIRE(east != nullptr);
    REQUIRE_FALSE(digger->is_drawn());

    constexpr int32 last = chunk::size - 1;
    const vec3i local{last, 20, 30};
    const auto scale = grid.world_units_per_voxel();
    const auto world_pos =
        grid.chunk_to_world_coord(*target) + (local * scale);

    grid.set_voxel(world_pos, voxels::air);

    REQUIRE(grid.get_voxel(world_pos).is_empty());

    REQUIRE(digger->is_drawn());
    REQUIRE(digger->known_neighbors() != 0);
    REQUIRE(east->is_drawn());
    REQUIRE(east->get_volume()->has_boundary_slice(face_direction::neg_x));
    REQUIRE_FALSE(
        east->get_volume()->is_boundary_solid(face_direction::neg_x, 0, local.y, local.z)
    );

    REQUIRE(east->get_volume()->is_boundary_solid(face_direction::neg_x, 0, local.y + 1, local.z));
}

TEST_CASE("a placed chunk arrives with its sky light", "[world][grid]") {
    job_system jobs;
    world w;
    const settled_grid settled{w, jobs};

    std::size_t chunks     = 0;
    std::size_t with_light = 0;
    std::size_t paged      = 0;
    std::size_t dark       = 0;
    bool saw_open_sky      = false;

    settled.grid().for_each_chunk([&](vec3i, const chunk& c) -> void {
        ++chunks;

        const auto* light = c.get_volume()->get_sky_light();
        if (light == nullptr) {
            return;
        }
        ++with_light;

        if (light->is_uniform()) {
            dark += light->uniform_level() == 0 ? 1 : 0;
            return;
        }

        ++paged;

        for (int32 z = 0; z < asset::light_field::side && !saw_open_sky; ++z) {
            for (int32 x = 0; x < asset::light_field::side; ++x) {
                if (light->level_at(x, asset::light_field::side - 1, z) ==
                    ecs::light_column::max_level) {
                    saw_open_sky = true;
                    break;
                }
            }
        }
    });

    INFO(
        "chunks " << chunks << ", with light " << with_light << ", paged " << paged << ", dark "
                  << dark
    );

    REQUIRE(chunks > 0);
    REQUIRE(with_light == chunks);

    REQUIRE(dark > 0);
    REQUIRE(saw_open_sky);
    REQUIRE(paged > 0);
}

TEST_CASE("digging to the sky relights the shaft", "[world][grid]") {
    job_system jobs;
    world w;
    settled_grid settled{w, jobs};

    auto& gs         = w.system<world_grid_system>();
    auto& grid       = *gs.grid();
    const int32 scale = grid.world_units_per_voxel();

    constexpr int32 depth = 20;

    const auto surface = solid_shaft_site(grid, vec2i{0, 0}, depth);
    REQUIRE(surface.has_value());

    std::vector<vec3i> shaft;
    for (int32 i = 0; i < depth; ++i) {
        shaft.push_back(vec3i{surface->x, surface->y - (i * scale), surface->z});
    }

    for (vec3i at : shaft) {
        REQUIRE_FALSE(grid.get_voxel(at).is_empty());
        REQUIRE(light_at(grid, at) == 0);
    }

    const auto columns_before = gs.get_stats().relit_columns;

    for (vec3i at : shaft) {
        grid.set_voxel(at, voxels::air);
    }

    REQUIRE(grid.get_voxel(shaft.back()).is_empty());
    REQUIRE(light_at(grid, shaft.back()) == 0);

    settled.settle();

    for (vec3i at : shaft) {
        INFO("world y " << at.y);
        REQUIRE(light_at(grid, at) == ecs::light_column::max_level);
    }

    REQUIRE(gs.get_stats().relit_columns == columns_before + 1);
}

TEST_CASE("digging in the dark relights nothing", "[world][grid]") {
    job_system jobs;
    world w;
    settled_grid settled{w, jobs};

    auto& gs   = w.system<world_grid_system>();
    auto& grid = *gs.grid();

    std::optional<vec3i> target;
    grid.for_each_chunk([&](vec3i coord, const chunk& c) -> void {
        if (target.has_value() || !c.is_solid()) {
            return;
        }

        const auto* light = c.get_volume()->get_sky_light();
        if (light != nullptr && light->is_uniform() && light->uniform_level() == 0) {
            target = coord;
        }
    });

    REQUIRE(target.has_value());

    const auto& stats         = gs.get_stats();
    const auto chunks_before  = stats.relit_chunks;
    const auto columns_before = stats.relit_columns;

    const vec3i at =
        grid.chunk_to_world_coord(*target) + (vec3i{32, 32, 32} * grid.world_units_per_voxel());

    REQUIRE_FALSE(grid.get_voxel(at).is_empty());
    grid.set_voxel(at, voxels::air);

    settled.settle();

    INFO(
        "relit " << (stats.relit_columns - columns_before) << " columns, "
                 << (stats.relit_chunks - chunks_before) << " chunks"
    );

    REQUIRE(stats.relit_columns > columns_before);
    REQUIRE(stats.relit_chunks == chunks_before);
}

TEST_CASE("the apron is generated but not placed", "[world][grid]") {
    job_system jobs;
    world w;
    const settled_grid settled{w, jobs};

    const auto& grid = settled.grid();

    const auto& stats = w.system<world_grid_system>().get_stats();
    INFO(
        "columns " << grid.column_count() << ", chunks " << grid.chunk_count() << ", staged "
                   << stats.staged_count << ", active " << stats.active_count << ", pending "
                   << stats.pending_count
    );

    for (int32 x = -view_distance; x <= view_distance; ++x) {
        for (int32 z = -view_distance; z <= view_distance; ++z) {
            INFO("column " << x << "," << z);
            REQUIRE(grid.has_column(vec2i{x, z}));
        }
    }

    std::size_t apron_placed = 0;
    for (int32 x = -view_distance - 1; x <= view_distance + 1; ++x) {
        for (int32 z = -view_distance - 1; z <= view_distance + 1; ++z) {
            const bool on_ring =
                std::abs(x) == view_distance + 1 || std::abs(z) == view_distance + 1;
            if (on_ring && grid.has_column(vec2i{x, z})) {
                ++apron_placed;
            }
        }
    }

    REQUIRE(apron_placed == 0);
}

TEST_CASE(
    "a level change hands the chunk its neighbour slices back", "[world][grid][lod]"
) {
    job_system jobs;
    world w;

    settled_grid streamed{w, jobs};

    auto& grid = *w.system<world_grid_system>().grid();
    auto& lod  = w.system<lod_system>();

    vec3i picked{};
    entity owner;

    grid.for_each_chunk([&](vec3i coord, const chunk& c) {
        if (owner.is_valid() || !c.get_entity().is_valid()) {
            return;
        }
        const bool under_viewer = coord.x == 0 && coord.z == 0;
        if (under_viewer) {
            return;
        }
        bool surrounded = true;
        for (const face_direction face : all_face_directions) {
            surrounded = surrounded && grid.has_chunk(coord + offset_of(face));
        }
        if (surrounded) {
            picked = coord;
            owner  = c.get_entity();
        }
    });

    REQUIRE(owner.is_valid());

    auto& volume = *grid.get_chunk(picked)->get_volume();

    volume.release_boundary();
    for (const face_direction face : all_face_directions) {
        REQUIRE_FALSE(volume.has_boundary_slice(face));
    }

    lod.set_default_base_distance(1.0F);
    w.update(0.016F);

    REQUIRE(w.get<model_component>(owner).get_lod_level() > 0);

    std::size_t back = 0;
    for (const face_direction face : all_face_directions) {
        back += volume.has_boundary_slice(face) ? 1U : 0U;
    }

    REQUIRE(back == all_face_directions.size());
}

TEST_CASE("open sky reaches the mesher as an air slice", "[world][grid]") {
    job_system jobs;
    world w;

    settled_grid streamed{w, jobs};

    auto& grid = *w.system<world_grid_system>().grid();

    std::size_t tops    = 0;
    std::size_t sky_set = 0;

    grid.for_each_chunk([&](vec3i coord, const chunk& c) {
        const auto levels = grid.column_levels(vec2i{coord.x, coord.z});
        if (levels.empty() || coord.y != levels.back()) {
            return;
        }

        ++tops;

        const auto& vol = *c.get_volume();
        if (vol.has_boundary_slice(face_direction::pos_y)) {
            ++sky_set;
            for (int32 a = 0; a < 64; ++a) {
                REQUIRE_FALSE(vol.is_boundary_solid(face_direction::pos_y, a, 0, a));
            }
        }
    });

    REQUIRE(tops > 0);
    REQUIRE(sky_set == tops);
}

TEST_CASE("a settled world leaves no sideways hole in any chunk", "[world][grid]") {
    job_system jobs;
    world w;

    settled_grid streamed{w, jobs};

    auto& grid = *w.system<world_grid_system>().grid();

    std::size_t checked = 0;
    std::size_t holes   = 0;
    std::string detail;

    grid.for_each_chunk([&](vec3i coord, const chunk& c) {
        if (!c.is_drawn()) {
            return;
        }
        for (const face_direction face : all_face_directions) {
            if (axis_of(face) == 1) {
                continue;
            }
            const auto at = coord + offset_of(face);
            if (!grid.has_chunk(at)) {
                continue;
            }
            ++checked;
            if (!c.get_volume()->has_boundary_slice(face)) {
                ++holes;
                const auto self_levels = grid.column_levels(vec2i{coord.x, coord.z});
                const auto near_levels = grid.column_levels(vec2i{at.x, at.z});
                detail += std::format(
                    "[{},{},{} face {} self {}..{} neighbour {}..{}] ", coord.x, coord.y,
                    coord.z, static_cast<int32>(face),
                    self_levels.empty() ? 0 : self_levels.front(),
                    self_levels.empty() ? 0 : self_levels.back(),
                    near_levels.empty() ? 0 : near_levels.front(),
                    near_levels.empty() ? 0 : near_levels.back()
                );
            }
        }
    });

    INFO(detail);
    REQUIRE(checked > 0);
    REQUIRE(holes == 0);
}
