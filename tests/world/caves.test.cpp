#include <catch2/catch_test_macros.hpp>

import std;

import vw.core;
import vw.asset;
import vw.ecs;
import vw.world;

using namespace vw;
using namespace vw::ecs;

namespace {

// The world the generator produced, flattened into one array so the cave
// structure can be walked without going through chunk boundaries.
class sampled_region {
public:
    static constexpr int32 chunk = 64;

    sampled_region(
        int32 columns_x, int32 columns_z, perlin_terrain_generator& gen, int32 origin_x = 0,
        int32 origin_z = 0
    )
        : columns_x_{columns_x}, columns_z_{columns_z} {
        std::vector<std::pair<vec3i, std::shared_ptr<asset::model>>> chunks;

        for (int32 ix = 0; ix < columns_x_; ++ix) {
            for (int32 iz = 0; iz < columns_z_; ++iz) {
                const int32 cx = origin_x + ix;
                const int32 cz = origin_z + iz;

                ecs::gen_column column{cx, cz};
                terrain_context ctx{
                    .cx           = cx,
                    .cz           = cz,
                    .create_chunk = [&column](int32 y) -> chunk_data& {
                        return column.create_chunk(y, chunk_data{});
                    },
                };
                gen.generate(ctx);

                for (auto& [cy, data] : column.get_all_chunk_data()) {
                    min_y_ = std::min(min_y_, cy);
                    max_y_ = std::max(max_y_, cy);
                    chunks.emplace_back(
                        vec3i{ix, cy, iz}, data.volume->shared_voxels());
                }
            }
        }

        columns_y_ = (max_y_ - min_y_) + 1;
        solid_.assign(static_cast<std::size_t>(size_x()) * size_y() * size_z(), false);

        for (const auto& [coord, model] : chunks) {
            for (int32 x = 0; x < chunk; ++x) {
                for (int32 y = 0; y < chunk; ++y) {
                    for (int32 z = 0; z < chunk; ++z) {
                        if (model->is_empty(x, y, z)) {
                            continue;
                        }
                        solid_[at((coord.x * chunk) + x, ((coord.y - min_y_) * chunk) + y,
                                  (coord.z * chunk) + z)] = true;
                    }
                }
            }
        }
    }

    [[nodiscard]] auto size_x() const -> int32 { return columns_x_ * chunk; }
    [[nodiscard]] auto size_y() const -> int32 { return columns_y_ * chunk; }
    [[nodiscard]] auto size_z() const -> int32 { return columns_z_ * chunk; }

    [[nodiscard]] auto is_solid(int32 x, int32 y, int32 z) const -> bool {
        if (x < 0 || y < 0 || z < 0 || x >= size_x() || y >= size_y() || z >= size_z()) {
            return true;  // outside the sampled region counts as rock
        }
        return solid_[at(x, y, z)];
    }

    // Rock above and rock below. Requiring only rock above counts the empty
    // space under the world floor as cave, which is most of what is down there
    // and swamps every other number.
    [[nodiscard]] auto is_underground(int32 x, int32 y, int32 z) const -> bool {
        bool above = false;
        for (int32 up = y + 1; up < size_y(); ++up) {
            if (is_solid(x, up, z)) {
                above = true;
                break;
            }
        }
        if (!above) {
            return false;
        }

        for (int32 down = y - 1; down >= 0; --down) {
            if (is_solid(x, down, z)) {
                return true;
            }
        }
        return false;
    }

    [[nodiscard]] auto at(int32 x, int32 y, int32 z) const -> std::size_t {
        return (static_cast<std::size_t>(y) * size_z() + z) * size_x() + x;
    }

private:
    int32 columns_x_;
    int32 columns_z_;
    int32 columns_y_ = 1;
    int32 min_y_     = std::numeric_limits<int32>::max();
    int32 max_y_     = std::numeric_limits<int32>::lowest();
    std::vector<bool> solid_;
};

// Caves are scenery and somewhere to put ore, not a road network. What is worth
// asserting is therefore the opposite of what it was for tunnels: a body has to
// fit, the systems have to differ in size, some have to open to the sky -- and
// they must NOT all join into one underground.
struct cave_stats {
    std::size_t underground_total = 0;
    std::size_t underground_air   = 0;

    // Air a character fits in. The arena body is 12x28x12 units at voxel_scale
    // 8, so it needs a 2x4x2 box of clear voxels.
    std::size_t walkable = 0;

    // Openings wide enough to read as a chamber rather than a crawl: a clear
    // 5-cube around the voxel.
    std::size_t roomy = 0;

    // Connected systems of walkable space, and how the biggest compares.
    std::size_t systems         = 0;
    std::size_t largest_system  = 0;
    std::size_t median_system   = 0;

    // Underground air a flood fill from the sky can get to.
    std::size_t reachable = 0;

    // Faces between air and rock: what the mesher has to turn into quads.
    std::size_t faces = 0;
};

auto fits_body(const sampled_region& r, int32 x, int32 y, int32 z) -> bool {
    for (int32 dx = 0; dx < 2; ++dx) {
        for (int32 dy = 0; dy < 4; ++dy) {
            for (int32 dz = 0; dz < 2; ++dz) {
                if (r.is_solid(x + dx, y + dy, z + dz)) {
                    return false;
                }
            }
        }
    }
    return true;
}

auto clear_cube(const sampled_region& r, int32 x, int32 y, int32 z, int32 radius) -> bool {
    for (int32 dx = -radius; dx <= radius; ++dx) {
        for (int32 dy = -radius; dy <= radius; ++dy) {
            for (int32 dz = -radius; dz <= radius; ++dz) {
                if (r.is_solid(x + dx, y + dy, z + dz)) {
                    return false;
                }
            }
        }
    }
    return true;
}

auto measure(const sampled_region& r) -> cave_stats {
    cave_stats stats;

    const std::size_t volume =
        static_cast<std::size_t>(r.size_x()) * r.size_y() * r.size_z();

    std::vector<uint8> walkable(volume, 0);

    for (int32 y = 0; y < r.size_y(); ++y) {
        for (int32 z = 0; z < r.size_z(); ++z) {
            for (int32 x = 0; x < r.size_x(); ++x) {
                if (!r.is_underground(x, y, z)) {
                    continue;
                }

                ++stats.underground_total;
                if (r.is_solid(x, y, z)) {
                    continue;
                }

                ++stats.underground_air;

                constexpr std::array<vec3i, 6> sides{
                    vec3i{1, 0, 0},  vec3i{-1, 0, 0}, vec3i{0, 1, 0},
                    vec3i{0, -1, 0}, vec3i{0, 0, 1},  vec3i{0, 0, -1},
                };
                for (const auto& d : sides) {
                    if (r.is_solid(x + d.x, y + d.y, z + d.z)) {
                        ++stats.faces;
                    }
                }

                if (clear_cube(r, x, y, z, 2)) {
                    ++stats.roomy;
                }
                if (fits_body(r, x, y, z)) {
                    ++stats.walkable;
                    walkable[r.at(x, y, z)] = 1;
                }
            }
        }
    }

    // Separate systems, measured over the space a body can occupy: two caves
    // joined by a crack a character cannot pass are still two caves.
    std::vector<uint8> seen(volume, 0);
    std::vector<vec3i> stack;
    std::vector<std::size_t> sizes;

    for (int32 y = 0; y < r.size_y(); ++y) {
        for (int32 z = 0; z < r.size_z(); ++z) {
            for (int32 x = 0; x < r.size_x(); ++x) {
                if (seen[r.at(x, y, z)] != 0 || walkable[r.at(x, y, z)] == 0) {
                    continue;
                }

                std::size_t size = 0;
                stack.push_back({x, y, z});
                seen[r.at(x, y, z)] = 1;

                while (!stack.empty()) {
                    const auto p = stack.back();
                    stack.pop_back();
                    ++size;

                    constexpr std::array<vec3i, 6> dirs{
                        vec3i{1, 0, 0},  vec3i{-1, 0, 0}, vec3i{0, 1, 0},
                        vec3i{0, -1, 0}, vec3i{0, 0, 1},  vec3i{0, 0, -1},
                    };
                    for (const auto& d : dirs) {
                        const vec3i n{p.x + d.x, p.y + d.y, p.z + d.z};
                        if (n.x < 0 || n.y < 0 || n.z < 0 || n.x >= r.size_x() ||
                            n.y >= r.size_y() || n.z >= r.size_z()) {
                            continue;
                        }
                        if (seen[r.at(n.x, n.y, n.z)] != 0 || walkable[r.at(n.x, n.y, n.z)] == 0) {
                            continue;
                        }
                        seen[r.at(n.x, n.y, n.z)] = 1;
                        stack.push_back(n);
                    }
                }

                sizes.push_back(size);
            }
        }
    }

    std::ranges::sort(sizes);
    stats.systems = sizes.size();
    if (!sizes.empty()) {
        stats.largest_system = sizes.back();
        stats.median_system  = sizes[sizes.size() / 2];
    }

    // What the sky can reach: a cave with no entrance is one nobody will stand
    // in, however good it looks.
    std::vector<uint8> reached(volume, 0);
    stack.clear();

    for (int32 x = 0; x < r.size_x(); ++x) {
        for (int32 z = 0; z < r.size_z(); ++z) {
            const int32 y = r.size_y() - 1;
            if (!r.is_solid(x, y, z) && reached[r.at(x, y, z)] == 0) {
                reached[r.at(x, y, z)] = 1;
                stack.push_back({x, y, z});
            }
        }
    }

    while (!stack.empty()) {
        const auto p = stack.back();
        stack.pop_back();

        if (r.is_underground(p.x, p.y, p.z)) {
            ++stats.reachable;
        }

        constexpr std::array<vec3i, 6> dirs{
            vec3i{1, 0, 0},  vec3i{-1, 0, 0}, vec3i{0, 1, 0},
            vec3i{0, -1, 0}, vec3i{0, 0, 1},  vec3i{0, 0, -1},
        };
        for (const auto& d : dirs) {
            const vec3i n{p.x + d.x, p.y + d.y, p.z + d.z};
            if (n.x < 0 || n.y < 0 || n.z < 0 || n.x >= r.size_x() || n.y >= r.size_y() ||
                n.z >= r.size_z()) {
                continue;
            }
            if (r.is_solid(n.x, n.y, n.z) || reached[r.at(n.x, n.y, n.z)] != 0) {
                continue;
            }
            reached[r.at(n.x, n.y, n.z)] = 1;
            stack.push_back(n);
        }
    }

    return stats;
}

auto report(std::string_view label, const cave_stats& stats) -> void {
    const auto total = static_cast<float64>(stats.underground_total);
    const auto air   = static_cast<float64>(stats.underground_air);

    std::println(
        "{}: air {:.4f}  walkable {:.2f}  roomy {:.2f}  systems {}  "
        "largest/walkable {:.2f}  largest/median {:.0f}  from sky {:.2f}  faces/kvox {:.1f}",
        label,
        total > 0 ? air / total : 0.0,
        air > 0 ? static_cast<float64>(stats.walkable) / air : 0.0,
        air > 0 ? static_cast<float64>(stats.roomy) / air : 0.0,
        stats.systems,
        stats.walkable > 0
            ? static_cast<float64>(stats.largest_system) / static_cast<float64>(stats.walkable)
            : 0.0,
        stats.median_system > 0
            ? static_cast<float64>(stats.largest_system) /
                  static_cast<float64>(stats.median_system)
            : 0.0,
        air > 0 ? static_cast<float64>(stats.reachable) / air : 0.0,
        total > 0 ? static_cast<float64>(stats.faces) * 1000.0 / total : 0.0
    );
}

}  // namespace

// Prints the whole table at once, so tuning does not need a rebuild per value.
// Not part of the suite: tagged [.sweep], run it by name.
TEST_CASE("cave shape sweep", "[world][caves][.sweep]") {
    // Sampled at several places: a region only holds a handful of systems, so
    // one patch of the map says as much about where it was cut as about the
    // settings.
    struct probe {
        const char* label;
        int32 origin_x;
        int32 origin_z;
    };

    for (const auto& [label, origin_x, origin_z] : {
             probe{"at   0, 0", 0, 0},
             probe{"at  16, 8", 16, 8},
             probe{"at 40,24 ", 40, 24},
             probe{"at 64,64 ", 64, 64},
         }) {
        asset::model_identity_pool identity_pool;
        asset::page_pool pages;

        perlin_terrain_generator gen{identity_pool, pages, perlin_terrain_generator::params{}};
        const sampled_region area{5, 5, gen, origin_x, origin_z};
        report(label, measure(area));
    }
}

TEST_CASE("caves are rooms, not a tunnel network", "[world][caves]") {
    asset::model_identity_pool identity_pool;
    asset::page_pool pages;

    perlin_terrain_generator gen{identity_pool, pages, perlin_terrain_generator::params{}};

    // Five columns is 320 voxels, enough to hold several regions of the cave
    // grid and so several independent systems.
    const sampled_region area{5, 5, gen};
    const auto stats = measure(area);

    const auto total = static_cast<float64>(stats.underground_total);
    const auto air   = static_cast<float64>(stats.underground_air);

    const auto fraction = total > 0 ? air / total : 0.0;
    const auto walkable = air > 0 ? static_cast<float64>(stats.walkable) / air : 0.0;
    const auto roomy    = air > 0 ? static_cast<float64>(stats.roomy) / air : 0.0;
    const auto biggest  = stats.walkable > 0
         ? static_cast<float64>(stats.largest_system) / static_cast<float64>(stats.walkable)
         : 0.0;

    INFO("air fraction: " << fraction);
    INFO("walkable share: " << walkable);
    INFO("roomy share: " << roomy);
    INFO("systems: " << stats.systems);
    INFO("largest system share: " << biggest);
    INFO("largest/median size: "
         << (stats.median_system > 0 ? static_cast<float64>(stats.largest_system) /
                                           static_cast<float64>(stats.median_system)
                                     : 0.0));
    REQUIRE(stats.underground_air > 0);

    // Hollow enough to find, far from a sponge. The floor is low because the
    // world is now a thousand voxels deep while these systems still hang off
    // the surface: the same caves in an eighth of the rock read as 0.4 %. Once
    // caves come from noise and fill the depth, this wants measuring per band
    // rather than over the whole column.
    REQUIRE(fraction > 0.0004);
    REQUIRE(fraction < 0.05);

    // A body fits in a good part of it. The share never approaches 1 even for a
    // perfectly round chamber, because this counts the corners a 2x4x2 body can
    // start from, and there are none against the wall.
    REQUIRE(walkable > 0.25);

    // Not all one calibre: some of the air is open enough to stand around in.
    REQUIRE(roomy > 0.05);

    // Several separate systems rather than one underground. Dungeons will be
    // the connected part of the world; caves are not. Measured across patches
    // of the map the biggest system holds between a quarter and a half of the
    // walkable space, so the bar sits above that spread and well below one.
    REQUIRE(stats.systems >= 4);
    REQUIRE(biggest < 0.7);

}

// Whether the biggest system under one patch of ground happens to have an
// opening is chance: a patch this size holds only a handful of fields. Ways in
// are a property of the world, so they are counted over two patches at once.
// Measured at 0.05 and 0.54 separately, 0.35 together.
TEST_CASE("some caves are open to the sky", "[world][caves]") {
    asset::model_identity_pool identity_pool;
    asset::page_pool pages;

    perlin_terrain_generator gen{identity_pool, pages, perlin_terrain_generator::params{}};

    std::size_t air       = 0;
    std::size_t reachable = 0;

    for (const auto& [origin_x, origin_z] : {std::pair{0, 0}, std::pair{64, 64}}) {
        const sampled_region area{5, 5, gen, origin_x, origin_z};
        const auto stats = measure(area);
        air += stats.underground_air;
        reachable += stats.reachable;
    }

    const auto from_sky =
        air > 0 ? static_cast<float64>(reachable) / static_cast<float64>(air) : 0.0;

    INFO("reachable from sky: " << from_sky);
    REQUIRE(from_sky > 0.15);
}
