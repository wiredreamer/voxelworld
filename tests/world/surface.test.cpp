#include <catch2/catch_test_macros.hpp>

import std;

import vw.core;
import vw.asset;
import vw.ecs;
import vw.world;

using namespace vw;
using namespace vw::ecs;

namespace {

class sampled_column {
public:
    static constexpr int32 chunk = 64;

    sampled_column(perlin_terrain_generator& gen, int32 cx, int32 cz) {
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
            models_[cy] = data.volume->shared_voxels();
            min_y_      = std::min(min_y_, cy * chunk);
            max_y_      = std::max(max_y_, (cy * chunk) + chunk - 1);
        }
    }

    [[nodiscard]] auto bottom() const -> int32 {
        return min_y_;
    }

    [[nodiscard]] auto top() const -> int32 {
        return max_y_;
    }

    [[nodiscard]] auto id_at(int32 x, int32 wy, int32 z) const -> voxel {
        const int32 cy = wy >= 0 ? wy / chunk : ((wy - chunk) + 1) / chunk;

        const auto it = models_.find(cy);
        if (it == models_.end()) {
            return voxels::air;
        }
        return it->second->get_voxel(x, wy - (cy * chunk), z);
    }

    [[nodiscard]] auto surface_of(int32 x, int32 z) const -> int32 {
        for (int32 wy = max_y_; wy >= min_y_; --wy) {
            if (id_at(x, wy, z) != voxels::air) {
                return wy;
            }
        }
        return min_y_ - 1;
    }

private:
    std::unordered_map<int32, std::shared_ptr<asset::model>> models_;
    int32 min_y_ = std::numeric_limits<int32>::max();
    int32 max_y_ = std::numeric_limits<int32>::lowest();
};

auto settled_params() -> perlin_terrain_generator::params {
    perlin_terrain_generator::params p{};
    p.caves = false;
    return p;
}

auto column_has_turf(const sampled_column& column, int32 x, int32 z) -> bool {
    return column.id_at(x, column.surface_of(x, z), z) == voxels::world::grass[0];
}

}  // namespace

TEST_CASE("ground is layers, not paint", "[world][surface]") {
    const auto p = settled_params();

    asset::model_identity_pool identity_pool;
    asset::page_pool pages;
    perlin_terrain_generator gen{identity_pool, pages, p};

    const sampled_column column{gen, 0, 0};

    REQUIRE(column.bottom() == p.world_bottom_y);

    int32 with_soil = 0;
    int32 bare_rock = 0;

    for (int32 x = 0; x < sampled_column::chunk; ++x) {
        for (int32 z = 0; z < sampled_column::chunk; ++z) {
            const int32 surface = column.surface_of(x, z);
            INFO("column " << x << "," << z << " surface at " << surface);
            REQUIRE(surface >= p.world_bottom_y);

            REQUIRE(column.id_at(x, surface + 1, z) == voxels::air);

            bool unbroken = true;
            for (int32 wy = p.world_bottom_y; wy <= surface; ++wy) {
                unbroken = unbroken && (column.id_at(x, wy, z) != voxels::air);
            }
            REQUIRE(unbroken);

            const auto crown = column.id_at(x, surface, z);

            if (crown == voxels::world::grass[0]) {
                ++with_soil;

                REQUIRE(column.id_at(x, surface - 1, z) != voxels::world::grass[0]);
                const auto under = column.id_at(x, surface - 1, z);
                REQUIRE((under == voxels::world::dirt[0] || under == voxels::world::stone[0]));
                continue;
            }

            REQUIRE((crown == voxels::world::stone[1] || crown == voxels::world::snow[1]));
            if (crown == voxels::world::snow[1]) {
                REQUIRE(surface > p.snow_line);
            }
            ++bare_rock;

            REQUIRE(column.id_at(x, surface - 1, z) != voxels::world::dirt[0]);
        }
    }

    INFO("soil columns " << with_soil << ", bare rock " << bare_rock);
    REQUIRE(with_soil > 0);
    REQUIRE(bare_rock > 0);
}

TEST_CASE("rock changes with absolute depth", "[world][surface]") {
    const auto p = settled_params();

    asset::model_identity_pool identity_pool;
    asset::page_pool pages;
    perlin_terrain_generator gen{identity_pool, pages, p};

    const sampled_column column{gen, 3, 5};

    struct probe {
        int32 wy;
        voxel expected;
    };

    for (const auto& [wy, expected] : {
             probe{p.world_bottom_y, voxels::world::bedrock},
             probe{p.world_bottom_y + p.bedrock_thickness - 1, voxels::world::bedrock},
             probe{p.world_bottom_y + p.bedrock_thickness, voxels::world::stone_deep[0]},
             probe{p.rock_bottom_y - 1, voxels::world::stone_deep[0]},
             probe{p.rock_bottom_y, voxels::world::stone_deep[1]},
             probe{p.rock_deep_y - 1, voxels::world::stone_deep[1]},
             probe{p.rock_deep_y, voxels::world::stone_deep[2]},
         }) {
        INFO("at height " << wy);
        REQUIRE(column.id_at(0, wy, 0) == expected);
        REQUIRE(column.id_at(37, wy, 21) == expected);
    }
}

TEST_CASE("soil settles by slope, not by a mountain rule", "[world][surface]") {
    auto p = settled_params();

    asset::model_identity_pool identity_pool;
    asset::page_pool pages;

    p.soil_slope_limit  = 1.0e6F;
    p.soil_altitude_end = 1000;

    perlin_terrain_generator gentle{identity_pool, pages, p};
    const sampled_column soft{gentle, 7, 2};

    perlin_terrain_generator strict{identity_pool, pages, settled_params()};
    const sampled_column hard{strict, 7, 2};

    int32 soft_soil = 0;
    int32 hard_soil = 0;

    for (int32 x = 0; x < sampled_column::chunk; ++x) {
        for (int32 z = 0; z < sampled_column::chunk; ++z) {
            soft_soil += column_has_turf(soft, x, z) ? 1 : 0;
            hard_soil += column_has_turf(hard, x, z) ? 1 : 0;
        }
    }

    INFO("turf with the limit lifted " << soft_soil << ", with it in place " << hard_soil);
    REQUIRE(soft_soil > hard_soil);
}
