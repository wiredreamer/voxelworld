#include <catch2/catch_test_macros.hpp>

import std;

import vw.core;
import vw.asset;
import vw.ecs;
import vw.world;

using namespace vw;
using namespace vw::ecs;

namespace {

constexpr int32 side   = 64;
constexpr int32 ground = 10;

class meadow {
public:
    meadow() : grid_{world_} {
        auto model = world_.resource<asset::model_registry>().create_unnamed(side, side, side);
        for (int32 y = 0; y <= ground; ++y) {
            for (int32 z = 0; z < side; ++z) {
                for (int32 x = 0; x < side; ++x) {
                    model->set_voxel(x, y, z, y == ground ? voxels::green[4] : voxels::brown[4]);
                }
            }
        }
        volume_ = std::make_shared<asset::chunk_volume>(model);
        static_cast<void>(grid_.place_chunk({0, 0, 0}, volume_));
        grid_.register_column({0, 0}, {0});
    }

    [[nodiscard]] auto grid() -> world_grid& {
        return grid_;
    }

    [[nodiscard]] auto volume() const -> const asset::chunk_volume& {
        return *volume_;
    }

    [[nodiscard]] auto units_at(vec3i at) const -> vec3i {
        return at * grid_.world_units_per_voxel();
    }

private:
    ecs::world world_;
    world_grid grid_;
    std::shared_ptr<asset::chunk_volume> volume_;
};

class generated_column {
public:
    generated_column(const perlin_terrain_generator::params& params, int32 cx, int32 cz)
        : gen_{identity_pool_, pages_, params} {
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
            chunks_[cy] = data.volume;
        }
    }

    [[nodiscard]] auto voxel_at(int32 x, int32 wy, int32 z) const -> voxel {
        const int32 cy = wy >= 0 ? wy / side : ((wy - side) + 1) / side;
        const auto it  = chunks_.find(cy);
        return it == chunks_.end() ? voxels::air : it->second->voxels().get_voxel(x, wy - (cy * side), z);
    }

    template <typename F>
    auto for_each_grass(F&& f) const -> void {
        for (const auto& [cy, volume] : chunks_) {
            volume->cover().for_each([&](const asset::cover_layer::entry& e) {
                f(vec3i{e.support.x, (cy * side) + e.support.y, e.support.z}, e.form);
            });
        }
    }

    [[nodiscard]] auto grass_count() const -> int32 {
        int32 count = 0;
        for_each_grass([&count](vec3i, uint8) { ++count; });
        return count;
    }

private:
    asset::model_identity_pool identity_pool_;
    asset::page_pool pages_;
    perlin_terrain_generator gen_;
    std::map<int32, std::shared_ptr<asset::chunk_volume>> chunks_;
};

auto flat_params(float32 density) -> perlin_terrain_generator::params {
    perlin_terrain_generator::params p{};
    p.caves  = false;
    p.plants = false;
    p.island = false;

    plains_biome plains{};
    plains.cover.density = density;
    p.biomes             = {plains};
    return p;
}

}  // namespace

TEST_CASE("a cover layer keeps one form per support and forgets a zero", "[world][cover]") {
    asset::cover_layer layer{std::vector<asset::cover_layer::entry>{
        {.support = {5, 2, 7}, .form = 3},
        {.support = {1, 0, 0}, .form = 1},
        {.support = {5, 2, 7}, .form = 9},
        {.support = {2, 2, 2}, .form = 0},
    }};

    REQUIRE(layer.size() == 2);
    REQUIRE(layer.form_at({5, 2, 7}) != 0);
    REQUIRE(layer.form_at({1, 0, 0}) == 1);
    REQUIRE(layer.form_at({2, 2, 2}) == 0);

    const uint64 before = layer.revision();
    REQUIRE_FALSE(layer.set({1, 0, 0}, 1));
    REQUIRE(layer.revision() == before);

    REQUIRE(layer.set({1, 0, 0}, 4));
    REQUIRE(layer.form_at({1, 0, 0}) == 4);
    REQUIRE(layer.revision() > before);

    REQUIRE(layer.set({1, 0, 0}, 0));
    REQUIRE(layer.form_at({1, 0, 0}) == 0);
    REQUIRE(layer.size() == 1);
    REQUIRE_FALSE(layer.set({-1, 0, 0}, 2));
}

TEST_CASE("two layers never share a revision", "[world][cover]") {
    const asset::cover_layer first;
    const asset::cover_layer second;
    REQUIRE(first.revision() != second.revision());
}

TEST_CASE("a cell answers who holds it", "[world][cover]") {
    meadow m;
    auto& grid = m.grid();

    REQUIRE(grid.cell_at({3, ground, 3}) == cell{.kind = occupant_kind::terrain, .look = voxels::green[4]});
    REQUIRE(grid.cell_at({3, ground + 1, 3}) == cell{});
    REQUIRE_FALSE(grid.cell_at({side + 3, ground + 1, 3}).has_value());

    REQUIRE(grid.plant_cover({3, ground + 1, 3}, 2).has_value());

    const auto grass = grid.cell_at({3, ground + 1, 3});
    REQUIRE(grass == cell{.kind = occupant_kind::grass, .look = voxels::green[4], .form = 2});
    REQUIRE(grass->traits() == cell_traits{});
    REQUIRE(traits_of(occupant_kind::terrain).solid);
    REQUIRE(grid.cell_at({3, ground + 2, 3}) == cell{});
}

TEST_CASE("grass is planted only on ground, into an empty cell", "[world][cover]") {
    meadow m;
    auto& grid = m.grid();

    REQUIRE_FALSE(grid.plant_cover({3, ground, 3}, 1).has_value());
    REQUIRE_FALSE(grid.plant_cover({3, ground + 2, 3}, 1).has_value());
    REQUIRE_FALSE(grid.plant_cover({side + 3, ground + 1, 3}, 1).has_value());
    REQUIRE_FALSE(grid.plant_cover({3, ground + 1, 3}, 0).has_value());
    REQUIRE_FALSE(grid.plant_cover({3, ground + 1, 3}, cover_form_count + 1).has_value());
    REQUIRE(m.volume().cover().empty());
}

TEST_CASE("a block put into grass pushes it out, digging under it takes it away", "[world][cover]") {
    meadow m;
    auto& grid = m.grid();

    REQUIRE(grid.plant_cover({3, ground + 1, 3}, 1).has_value());
    REQUIRE(grid.plant_cover({8, ground + 1, 8}, 1).has_value());

    grid.set_voxel(m.units_at({3, ground + 1, 3}), voxels::gray[10]);
    REQUIRE(grid.cell_at({3, ground + 1, 3})->kind == occupant_kind::terrain);
    REQUIRE(grid.cell_at({3, ground + 2, 3}) == cell{});

    grid.set_voxel(m.units_at({8, ground, 8}), voxels::air);
    REQUIRE(grid.cell_at({8, ground + 1, 8}) == cell{});
    REQUIRE(m.volume().cover().empty());
}

TEST_CASE("clearing a cell removes whoever holds it", "[world][cover]") {
    meadow m;
    auto& grid = m.grid();

    REQUIRE(grid.plant_cover({3, ground + 1, 3}, 1).has_value());
    grid.clear_cell({3, ground + 1, 3});
    REQUIRE(grid.cell_at({3, ground + 1, 3}) == cell{});
    REQUIRE(grid.cell_at({3, ground, 3})->kind == occupant_kind::terrain);

    REQUIRE(grid.plant_cover({4, ground + 1, 4}, 1).has_value());
    grid.clear_cell({4, ground, 4});
    REQUIRE(grid.cell_at({4, ground, 4}) == cell{});
    REQUIRE(grid.cell_at({4, ground + 1, 4}) == cell{});
}

TEST_CASE("planting grass does not touch the terrain mesh", "[world][cover]") {
    meadow m;
    const auto identity = m.volume().voxels().get_identity();

    REQUIRE(m.grid().plant_cover({3, ground + 1, 3}, 1).has_value());
    m.grid().clear_cell({3, ground + 1, 3});

    REQUIRE(m.volume().voxels().get_identity() == identity);
}

TEST_CASE("generated grass stands on the grass layer, one cell above the surface", "[world][cover]") {
    const generated_column column{flat_params(0.5F), 0, 0};

    REQUIRE(column.grass_count() > 0);
    column.for_each_grass([&column](vec3i support, uint8 form) {
        INFO("grass at " << support.x << "," << support.y << "," << support.z);
        REQUIRE(form >= 1);
        REQUIRE(form <= cover_form_count);
        REQUIRE(voxels::green.contains(column.voxel_at(support.x, support.y, support.z)));
        REQUIRE(column.voxel_at(support.x, support.y + 1, support.z) == voxels::air);
    });
}

TEST_CASE("grass density goes from bare ground to a full lawn", "[world][cover]") {
    REQUIRE(generated_column{flat_params(0.0F), 0, 0}.grass_count() == 0);
    REQUIRE(generated_column{flat_params(1.0F), 0, 0}.grass_count() == side * side);

    const int32 half = generated_column{flat_params(0.5F), 0, 0}.grass_count();
    REQUIRE(half > (side * side) / 10);
    REQUIRE(half < (side * side) * 9 / 10);
}

TEST_CASE("the same seed grows the same grass", "[world][cover]") {
    const generated_column first{flat_params(0.5F), 1, -2};
    const generated_column second{flat_params(0.5F), 1, -2};

    std::vector<std::pair<vec3i, uint8>> a;
    std::vector<std::pair<vec3i, uint8>> b;
    first.for_each_grass([&a](vec3i at, uint8 form) { a.emplace_back(at, form); });
    second.for_each_grass([&b](vec3i at, uint8 form) { b.emplace_back(at, form); });

    REQUIRE_FALSE(a.empty());
    REQUIRE(a == b);
}

TEST_CASE("no grass grows on the stone of steep hills", "[world][cover]") {
    perlin_terrain_generator::params p{};
    p.caves  = false;
    p.plants = false;
    p.island = false;

    hills_biome hills{};
    hills.cover.density   = 1.0F;
    hills.height          = 120.0F;
    hills.shape.frequency = 0.02F;
    p.biomes              = {hills};

    const generated_column column{p, 0, 0};
    REQUIRE(column.grass_count() > 0);
    REQUIRE(column.grass_count() < side * side);
    column.for_each_grass([&column](vec3i support, uint8) {
        REQUIRE(voxels::green.contains(column.voxel_at(support.x, support.y, support.z)));
    });
}

TEST_CASE("grass grows taller deeper inside its patch", "[world][cover]") {
    const generated_column column{flat_params(0.7F), 0, 0};

    std::array<int32, grass_height_classes> seen{};
    column.for_each_grass([&seen](vec3i, uint8 form) { ++seen[grass_height_class_of(form)]; });

    for (uint8 height_class = 0; height_class < grass_height_classes; ++height_class) {
        INFO("class " << static_cast<int32>(height_class));
        REQUIRE(seen[height_class] > 0);
    }
}

TEST_CASE("flowers grow inside the grass only as often as the biome asks", "[world][cover]") {
    auto params                                      = flat_params(1.0F);
    std::get<plains_biome>(params.biomes[0]).cover.flower_share = 0.0F;
    int32 flowers = 0;
    generated_column{params, 0, 0}.for_each_grass([&flowers](vec3i, uint8 form) {
        flowers += is_flower_form(form) ? 1 : 0;
    });
    REQUIRE(flowers == 0);

    std::get<plains_biome>(params.biomes[0]).cover.flower_share = 1.0F;
    int32 grass = 0;
    generated_column{params, 0, 0}.for_each_grass([&grass](vec3i, uint8 form) {
        grass += is_flower_form(form) ? 0 : 1;
    });
    REQUIRE(grass == 0);

    std::get<plains_biome>(params.biomes[0]).cover.flower_share = 0.06F;
    int32 some = 0;
    int32 all  = 0;
    generated_column{params, 0, 0}.for_each_grass([&](vec3i, uint8 form) {
        some += is_flower_form(form) ? 1 : 0;
        ++all;
    });
    REQUIRE(some > 0);
    REQUIRE(some < all / 5);
}

TEST_CASE("a planted flower is a cell of its own kind", "[world][cover]") {
    meadow m;
    REQUIRE(m.grid().plant_cover({3, ground + 1, 3}, flower_form_of(2, 1)).has_value());

    const auto flower = m.grid().cell_at({3, ground + 1, 3});
    REQUIRE(flower->kind == occupant_kind::flower);
    REQUIRE(flower->traits() == cell_traits{});

    m.grid().clear_cell({3, ground + 1, 3});
    REQUIRE(m.grid().cell_at({3, ground + 1, 3}) == cell{});
}
