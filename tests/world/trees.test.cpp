#include <catch2/catch_test_macros.hpp>

import std;

import vw.core;
import vw.asset;
import vw.ecs;
import vw.world;

using namespace vw;
using namespace vw::ecs;

namespace {

constexpr matter bark{voxels::amber[4], materials::wood};
constexpr matter leaves{voxels::green[4], materials::leaves};

auto look_at(const plant_shape& shape, vec3i offset) -> matter {
    const auto found = std::ranges::find(shape.voxels, offset, &plant_voxel::offset);
    return found == shape.voxels.end() ? matter{} : found->look;
}

auto is_wood(matter held) -> bool {
    return !held.is_empty() && held.made_of == materials::wood;
}

auto is_leaf(matter held) -> bool {
    return !held.is_empty() && held.made_of == materials::leaves;
}

auto same_voxels(const plant_shape& a, const plant_shape& b) -> bool {
    return std::ranges::equal(a.voxels, b.voxels, [](const plant_voxel& l, const plant_voxel& r) {
        return l.offset == r.offset && l.look == r.look;
    });
}

constexpr int32 side = 64;

auto forest_params() -> perlin_terrain_generator::params {
    perlin_terrain_generator::params p{};
    p.caves  = false;
    p.island = false;

    hills_biome hills{};
    hills.woods.density = 1.0F;
    hills.woods.fill    = 1.0F;
    hills.cover.density = 1.0F;
    p.biomes            = {hills};
    return p;
}

class grown_columns {
public:
    explicit grown_columns(const perlin_terrain_generator::params& params) : gen_{identity_pool_, pages_, params} {}

    auto grow(int32 cx, int32 cz) -> void {
        gen_column column{cx, cz};
        terrain_context ctx{
            .cx           = cx,
            .cz           = cz,
            .create_chunk = [&column](int32 y) -> chunk_data& {
                return column.create_chunk(y, chunk_data{});
            },
        };
        gen_.generate(ctx);
        for (auto& [cy, data] : column.get_all_chunk_data()) {
            chunks_[{cx, cy, cz}] = data.volume;
        }
    }

    [[nodiscard]] auto voxel_at(vec3i at) const -> matter {
        const auto down = [](int32 v) -> int32 { return v >= 0 ? v / side : ((v - side) + 1) / side; };
        const vec3i coord{down(at.x), down(at.y), down(at.z)};
        const auto it = chunks_.find(coord);
        if (it == chunks_.end()) {
            return matter{};
        }
        const vec3i local = at - (coord * side);
        return it->second->voxels().get_matter(local.x, local.y, local.z);
    }

    [[nodiscard]] auto column_voxels(int32 cx, int32 cz) const -> std::vector<uint8> {
        std::vector<uint8> out;
        for (const auto& [coord, volume] : chunks_) {
            if (coord.x != cx || coord.z != cz) {
                continue;
            }
            out.push_back(static_cast<uint8>(coord.y & 0xFF));
            for (int32 z = 0; z < side; ++z) {
                for (int32 y = 0; y < side; ++y) {
                    for (int32 x = 0; x < side; ++x) {
                        const matter held = volume->voxels().get_matter(x, y, z);
                        out.push_back(held.color.value);
                        out.push_back(held.made_of.value);
                    }
                }
            }
        }
        return out;
    }

    template <typename F>
    auto for_each_voxel(F&& f) const -> void {
        for (const auto& [coord, volume] : chunks_) {
            for (int32 z = 0; z < side; ++z) {
                for (int32 y = 0; y < side; ++y) {
                    for (int32 x = 0; x < side; ++x) {
                        f((coord * side) + vec3i{x, y, z}, volume->voxels().get_matter(x, y, z));
                    }
                }
            }
        }
    }

    template <typename F>
    auto for_each_grass(F&& f) const -> void {
        for (const auto& [coord, volume] : chunks_) {
            volume->cover().for_each([&](const asset::cover_layer::entry& e) { f((coord * side) + e.support); });
        }
    }

private:
    asset::model_identity_pool identity_pool_;
    asset::page_pool pages_;
    perlin_terrain_generator gen_;
    std::map<vec3i, std::shared_ptr<asset::chunk_volume>, decltype([](vec3i a, vec3i b) {
                 return std::tie(a.x, a.y, a.z) < std::tie(b.x, b.y, b.z);
             })>
        chunks_;
};

}  // namespace

TEST_CASE("a seed grows the same tree every time and another seed another one", "[world][trees]") {
    const tree_species species{};

    const auto a = grow_tree(species, 42, 0, bark, leaves);
    const auto b = grow_tree(species, 42, 0, bark, leaves);
    const auto c = grow_tree(species, 43, 0, bark, leaves);

    REQUIRE(same_voxels(a, b));
    REQUIRE_FALSE(same_voxels(a, c));
}

TEST_CASE("a tree stands on a solid trunk from its sunk root up to the fork", "[world][trees]") {
    tree_species species{};

    for (uint64 seed = 1; seed <= 40; ++seed) {
        INFO("seed " << seed);
        const auto shape = grow_tree(species, seed, 0, bark, leaves);
        const vec3i size = shape.max - shape.min;

        REQUIRE(shape.min.y == -tree_root_depth);
        REQUIRE(size.x <= 62);
        REQUIRE(size.z <= 62);
        REQUIRE(shape.max.y >= species.min_height - 1);
        REQUIRE(shape.max.y <= species.max_height + 2);

        const int32 fork =
            static_cast<int32>(std::lround(static_cast<float32>(species.min_height) * species.fork_share));
        for (int32 y = -tree_root_depth; y < fork; ++y) {
            REQUIRE(is_wood(look_at(shape, {0, y, 0})));
        }

        int32 wood_count = 0;
        int32 leaf_count = 0;
        for (const plant_voxel& v : shape.voxels) {
            REQUIRE((is_wood(v.look) || is_leaf(v.look)));
            REQUIRE(voxels::amber.contains(v.look.color) == is_wood(v.look));
            REQUIRE(voxels::green.contains(v.look.color) == is_leaf(v.look));
            if (is_wood(v.look)) {
                ++wood_count;
            } else {
                ++leaf_count;
            }
        }
        REQUIRE(leaf_count > wood_count);
    }
}

TEST_CASE("a tall tree starts on a thicker trunk", "[world][trees]") {
    const tree_species tall{.min_height = 18, .max_height = 18};
    const tree_species short_one{.min_height = 11, .max_height = 11};

    const auto big   = grow_tree(tall, 7, 0, bark, leaves);
    const auto small = grow_tree(short_one, 7, 0, bark, leaves);

    const auto width_at = [](const plant_shape& shape, int32 y) -> int32 {
        int32 w = 0;
        while (is_wood(look_at(shape, {w, y, 0}))) {
            ++w;
        }
        return w;
    };

    REQUIRE(width_at(big, 0) == 3);
    REQUIRE(width_at(small, 0) == 2);
    REQUIRE(width_at(big, 9) < 3);
}

TEST_CASE("a turned tree is the same tree on its side of the root", "[world][trees]") {
    const tree_species species{};

    const auto straight = grow_tree(species, 11, 0, bark, leaves);
    const auto turned   = grow_tree(species, 11, 1, bark, leaves);

    REQUIRE(straight.voxels.size() == turned.voxels.size());
    for (const plant_voxel& v : straight.voxels) {
        REQUIRE(look_at(turned, {-v.offset.z, v.offset.y, v.offset.x}) == v.look);
    }
}

TEST_CASE("a forest grows bark and leaves into the chunks above its ground", "[world][trees]") {
    grown_columns columns{forest_params()};
    columns.grow(0, 0);

    int32 wood_count = 0;
    int32 leaf_count = 0;
    columns.for_each_voxel([&](vec3i, matter v) {
        wood_count += is_wood(v) ? 1 : 0;
        leaf_count += is_leaf(v) ? 1 : 0;
    });
    REQUIRE(wood_count > 100);
    REQUIRE(leaf_count > wood_count);
}

TEST_CASE("a column grows the same trees whichever neighbour came first", "[world][trees]") {
    grown_columns east_first{forest_params()};
    east_first.grow(1, 0);
    east_first.grow(0, 0);

    grown_columns west_first{forest_params()};
    west_first.grow(0, 0);
    west_first.grow(1, 0);

    grown_columns alone{forest_params()};
    alone.grow(1, 0);

    REQUIRE(east_first.column_voxels(0, 0) == west_first.column_voxels(0, 0));
    REQUIRE(east_first.column_voxels(1, 0) == west_first.column_voxels(1, 0));
    REQUIRE(alone.column_voxels(1, 0) == west_first.column_voxels(1, 0));
}

TEST_CASE("a crown crosses the column border whole", "[world][trees]") {
    grown_columns columns{forest_params()};
    columns.grow(0, 0);
    columns.grow(1, 0);

    int32 crossing = 0;
    for (int32 y = -64; y < 192; ++y) {
        for (int32 z = 0; z < side; ++z) {
            const matter west = columns.voxel_at({side - 1, y, z});
            const matter east = columns.voxel_at({side, y, z});
            crossing += (is_leaf(west) && is_leaf(east)) ? 1 : 0;
        }
    }
    REQUIRE(crossing > 0);
}

TEST_CASE("grass never grows inside a plant", "[world][trees]") {
    grown_columns columns{forest_params()};
    columns.grow(0, 0);

    int32 grass = 0;
    columns.for_each_grass([&](vec3i support) {
        ++grass;
        REQUIRE(columns.voxel_at(support + vec3i{0, 1, 0}).is_empty());
    });
    REQUIRE(grass > 0);
}
