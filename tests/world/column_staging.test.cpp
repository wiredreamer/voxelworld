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

// A shallow world: these tests are about when columns are placed, and four
// chunks a column say that as well as nine. Not fewer, though -- the bottom
// chunk of the world has its floor open to nothing and is drawn, so a column
// needs one more below the surface to have anything buried in it at all.
auto shallow_params() -> perlin_terrain_generator::params {
    perlin_terrain_generator::params p{};
    p.world_bottom_y = -192;
    return p;
}

// Drives the grid system until the loader is idle and nothing more is being
// placed, watching every chunk from the moment it first appears.
class settled_grid {
public:
    explicit settled_grid(world& w) : world_{&w} {
        auto& models = w.resource<asset::model_registry>();
        auto& gs     = w.system<world_grid_system>();

        gs.set_grid(std::make_unique<world_grid>(w, 8));
        gs.set_loader(std::make_unique<chunk_loader>(std::make_unique<perlin_terrain_generator>(
            models.get_identity_pool(), models.get_page_pool(), shallow_params()
        )));

        viewer_ = w.create().with<transform_component>().with<world_view_component>().get_entity();
        gs.modify_view(viewer_).set_view_distance(view_distance);

        settle();
    }

    // Runs frames until nothing is in flight. Called once to build the world,
    // and again by the tests that edit it: an edit relights the columns it
    // touched, and that lands frames later on a worker of its own.
    auto settle() -> void {
        auto& gs = world_->system<world_grid_system>();

        // Quiet frames rather than one quiet frame: requests go out eight per
        // frame and columns are placed one per frame, so the loader is idle for
        // stretches long before the world is whole.
        int32 quiet = 0;

        for (int32 frame = 0; frame < max_frames && quiet < quiet_frames; ++frame) {
            world_->update(0.016F);
            observe_();

            // Three stages to wait on now, not one: a column that is generated
            // still has to be lit before it is placed, light runs on its own
            // workers, and an edited column is queued for relighting before any
            // of it is in flight.
            const auto& stats  = gs.get_stats();
            const bool waiting = stats.pending_count > 0 || stats.lighting_count > 0 ||
                                 stats.relight_backlog > 0;
            quiet = (waiting || placed_this_frame_ > 0) ? 0 : quiet + 1;

            // An empty update takes microseconds and generation takes
            // milliseconds, so a tight loop would run out of frames before the
            // first column ever arrived.
            if (waiting && placed_this_frame_ == 0) {
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
        }
    }

    [[nodiscard]] auto grid() const -> const world_grid& {
        return *world_->system<world_grid_system>().grid();
    }

    // How many chunks changed identity after they were placed. Every such
    // change is a chunk the mesher has to build a second time.
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
    entity viewer_;
    std::unordered_map<vec3i, asset::model_identity> first_seen_;
    int32 placed_this_frame_ = 0;
};

// The baked level at a world position, out of the field on the model the voxel
// belongs to. Nothing here reads the flood -- the point is what the world is
// carrying around, which is what the mesher will read.
auto light_at(world_grid& grid, vec3i world_pos) -> std::optional<int32> {
    auto* c = grid.get_chunk(grid.world_to_chunk_coord(world_pos));
    if (c == nullptr) {
        return std::nullopt;
    }

    const auto* light = c->get_volume()->get_sky_light();
    if (light == nullptr) {
        return std::nullopt;
    }

    return light->level_at(grid.world_to_local_coord(world_pos) / grid.voxel_scale());
}

// Somewhere in the middle of a column with solid rock straight down from the
// surface for as far as asked. Caves are everywhere in this terrain, so a fixed
// spot would be luck: a shaft that breaks into one is a hole into somewhere
// already lit, and says nothing about relighting.
//
// The middle of the column, so the shaft stays more than fifteen voxels from
// every side and the columns around it are left out of it.
auto solid_shaft_site(world_grid& grid, vec2i column, int32 depth) -> std::optional<vec3i> {
    const auto levels = grid.column_levels(column);
    if (levels.empty()) {
        return std::nullopt;
    }

    const int32 scale = grid.voxel_scale();
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
    world w;
    const settled_grid settled{w};

    REQUIRE(settled.seen_count() > 0);

    // Chunks used to be invalidated once per neighbour that turned up after
    // them, which is what made the mesher run over each chunk three and a half
    // times. Placement now waits for the neighbours instead.
    REQUIRE(settled.reissued() == 0);
}

TEST_CASE("a placed chunk knows every neighbour it has", "[world][grid]") {
    world w;
    const settled_grid settled{w};

    static constexpr vec3i offsets[6] = {
        {1, 0, 0}, {-1, 0, 0}, {0, 1, 0}, {0, -1, 0}, {0, 0, 1}, {0, 0, -1},
    };

    auto& grid = *w.system<world_grid_system>().grid();

    std::size_t checked = 0;
    std::size_t missing = 0;

    // The chunk is asked, not its model: the planes are dropped once nothing
    // else will read them, and the chunk keeps the record of having had them.
    grid.for_each_chunk([&](vec3i coord, const chunk& c) {
        for (int32 fd = 0; fd < 6; ++fd) {
            if (!grid.has_chunk(coord + offsets[fd])) {
                continue;
            }
            ++checked;
            if ((c.known_neighbors() & (1U << fd)) == 0) {
                ++missing;
            }
        }
    });

    REQUIRE(checked > 0);
    REQUIRE(missing == 0);
}

TEST_CASE("buried rock costs no entity", "[world][grid]") {
    world w;
    const settled_grid settled{w};

    auto& grid = *w.system<world_grid_system>().grid();

    // Face order as the grid exchanges boundaries: +X, -X, +Y, -Y, +Z, -Z. The
    // plane of the neighbour that faces this chunk is the far side of that
    // offset, which is why the layers below run the axis backwards.
    static constexpr vec3i offsets[6] = {
        {1, 0, 0}, {-1, 0, 0}, {0, 1, 0}, {0, -1, 0}, {0, 0, 1}, {0, 0, -1},
    };

    constexpr int32 s = chunk::size;

    // Read the seam out of the neighbour's voxels rather than the bit plane the
    // grid handed it: the point is to check that cached answer, not repeat it.
    const auto seam_is_solid = [&](const chunk& neighbor, int32 fd) -> bool {
        for (int32 a = 0; a < s; ++a) {
            for (int32 b = 0; b < s; ++b) {
                const vec3i local = [&] -> vec3i {
                    switch (fd) {
                        case 0: return {0, a, b};
                        case 1: return {s - 1, a, b};
                        case 2: return {a, 0, b};
                        case 3: return {a, s - 1, b};
                        case 4: return {a, b, 0};
                        default: return {a, b, s - 1};
                    }
                }();

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

        // Air has no faces of its own whatever stands around it.
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

        for (int32 fd = 0; fd < 6; ++fd) {
            INFO("face " << fd);

            // A chunk on the edge of the drawn area has neighbours in the
            // apron: generated, boundaries handed over, never placed. Their
            // voxels are out of reach here, so all this can say is that the
            // face was answered by something rather than left open to the sky.
            auto* neighbor = grid.get_chunk(coord + offsets[fd]);
            if (neighbor == nullptr) {
                REQUIRE((c.known_neighbors() & (1U << fd)) != 0);
                continue;
            }

            REQUIRE(seam_is_solid(*neighbor, fd));
            ++verified;
        }
    });

    // Four chunks a column, one of them the floor of the world and one the
    // surface: something in between has to be buried.
    REQUIRE(skipped > 0);
    REQUIRE(verified > 0);
    REQUIRE(grid.drawn_chunk_count() == grid.chunk_count() - skipped);
}

TEST_CASE("a chunk with no entity still holds its voxels", "[world][grid]") {
    world w;
    const settled_grid settled{w};

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
    world w;
    const settled_grid settled{w};

    auto& grid = *w.system<world_grid_system>().grid();

    // A chunk buried in rock, so both it and the neighbour start with nothing
    // to draw and no planes held.
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

    // The far skin of the chunk, which is half of the seam with the neighbour.
    constexpr int32 last = chunk::size - 1;
    const vec3i local{last, 20, 30};
    const auto scale = grid.voxel_scale();
    const auto world_pos =
        grid.chunk_to_world_coord(*target) + (local * scale);

    grid.set_voxel(world_pos, voxels::air);

    REQUIRE(grid.get_voxel(world_pos).is_empty());

    // Both sides are back in the scene with their planes, and the neighbour's
    // view of this chunk has the hole in it: face 1 is -X, and the plane it
    // reads is addressed (y, z).
    REQUIRE(digger->is_drawn());
    REQUIRE(digger->known_neighbors() != 0);
    REQUIRE(east->is_drawn());
    REQUIRE(east->get_volume()->has_boundary_slice(1));
    REQUIRE_FALSE(east->get_volume()->is_boundary_solid(1, 0, local.y, local.z));

    // ...and the rock beside the hole is still rock.
    REQUIRE(east->get_volume()->is_boundary_solid(1, 0, local.y + 1, local.z));
}

// The light stage, from the outside. A column is generated, then lit on a
// worker once its eight neighbours exist, and only then placed -- so a chunk
// that is in the grid at all has its light, and no chunk is ever meshed
// unlit and meshed again.
TEST_CASE("a placed chunk arrives with its sky light", "[world][grid]") {
    world w;
    const settled_grid settled{w};

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

    // And it is really light, not a field of zeroes: rock is dark, the top of
    // the world is 15, and the ground between them is what the pages are for.
    REQUIRE(dark > 0);
    REQUIRE(saw_open_sky);
    REQUIRE(paged > 0);
}

// An edit has to move the light, and a spade is the case that proves it.
//
// Sinking a shaft from open sky lights the whole of it, twenty voxels down.
// No patch around the edit could do that: the bottom of the shaft is far past
// the fifteen steps light carries, and is bright only because nothing opaque
// stands above it in its own column. That is rule one, it is a property of the
// column as a whole, and the only thing that re-applies it is flooding the
// column again.
TEST_CASE("digging to the sky relights the shaft", "[world][grid]") {
    world w;
    settled_grid settled{w};

    auto& gs         = w.system<world_grid_system>();
    auto& grid       = *gs.grid();
    const int32 scale = grid.voxel_scale();

    constexpr int32 depth = 20;

    const auto surface = solid_shaft_site(grid, vec2i{0, 0}, depth);
    REQUIRE(surface.has_value());

    std::vector<vec3i> shaft;
    for (int32 i = 0; i < depth; ++i) {
        shaft.push_back(vec3i{surface->x, surface->y - (i * scale), surface->z});
    }

    // Rock, and dark: the flood leaves a solid voxel at zero.
    for (vec3i at : shaft) {
        REQUIRE_FALSE(grid.get_voxel(at).is_empty());
        REQUIRE(light_at(grid, at) == 0);
    }

    const auto columns_before = gs.get_stats().relit_columns;

    for (vec3i at : shaft) {
        grid.set_voxel(at, voxels::air);
    }

    // The frame the spade lands on has the geometry and not the light: the
    // flood runs on a worker like every other one.
    REQUIRE(grid.get_voxel(shaft.back()).is_empty());
    REQUIRE(light_at(grid, shaft.back()) == 0);

    settled.settle();

    for (vec3i at : shaft) {
        INFO("world y " << at.y);
        REQUIRE(light_at(grid, at) == ecs::light_column::max_level);
    }

    // And only the column that was dug. The shaft is in the middle of it, more
    // than fifteen voxels from every side, so nothing outside had light to
    // gain and nothing outside was asked.
    REQUIRE(gs.get_stats().relit_columns == columns_before + 1);
}

// The other half of the bargain. A relight bakes the whole column because sky
// light after an edit can move anywhere below it -- but almost never does.
//
// Cutting a pocket out of rock deep underground changes no light at all: rock
// is dark and so is a sealed pocket. The fields say so, and because they say so
// not one chunk is meshed again. Without that comparison a mine would remesh
// nine chunks a stroke for nothing.
TEST_CASE("digging in the dark relights nothing", "[world][grid]") {
    world w;
    settled_grid settled{w};

    auto& gs   = w.system<world_grid_system>();
    auto& grid = *gs.grid();

    // Solid all through, so its field is one dark level and the middle of it is
    // thirty-two voxels from any face -- twice what light can carry.
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
        grid.chunk_to_world_coord(*target) + (vec3i{32, 32, 32} * grid.voxel_scale());

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
    world w;
    const settled_grid settled{w};

    const auto& grid = settled.grid();

    const auto& stats = w.system<world_grid_system>().get_stats();
    INFO(
        "columns " << grid.column_count() << ", chunks " << grid.chunk_count() << ", staged "
                   << stats.staged_count << ", active " << stats.active_count << ", pending "
                   << stats.pending_count
    );

    // Everything the viewer can see is there...
    for (int32 x = -view_distance; x <= view_distance; ++x) {
        for (int32 z = -view_distance; z <= view_distance; ++z) {
            INFO("column " << x << "," << z);
            REQUIRE(grid.has_column(vec2i{x, z}));
        }
    }

    // ...and the ring that exists only to give it its boundaries is not.
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
