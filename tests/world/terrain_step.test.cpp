#include <catch2/catch_test_macros.hpp>

import std;

import vw.core;
import vw.asset;
import vw.ecs;
import vw.world;

using namespace vw;
using namespace vw::ecs;

namespace {

constexpr int32 cells_per_chunk = 64;

auto floor_div(int32 a, int32 b) -> int32 {
    return a >= 0 ? a / b : (a - b + 1) / b;
}

class stepped_column {
public:
    stepped_column(perlin_terrain_generator& gen, int32 cx, int32 cz, int32 voxels_per_cell)
        : voxels_per_cell_{voxels_per_cell} {
        ecs::gen_column column{cx, cz};

        terrain_context ctx{
            .cx              = cx,
            .cz              = cz,
            .voxels_per_cell = voxels_per_cell,
            .create_chunk    = [&column](int32 y) -> chunk_data& {
                return column.create_chunk(y, chunk_data{});
            },
        };
        gen.generate(ctx);

        for (auto& [cy, data] : column.get_all_chunk_data()) {
            models_[cy] = data.volume->shared_voxels();
            min_cell_   = std::min(min_cell_, cy * cells_per_chunk);
            max_cell_   = std::max(max_cell_, (cy * cells_per_chunk) + cells_per_chunk - 1);
        }
    }

    [[nodiscard]] auto chunk_count() const -> std::size_t {
        return models_.size();
    }

    [[nodiscard]] auto voxels_per_cell() const -> int32 {
        return voxels_per_cell_;
    }

    [[nodiscard]] auto scale_of_models() const -> int32 {
        return models_.empty() ? 0 : models_.begin()->second->world_units_per_voxel();
    }

    [[nodiscard]] auto at(int32 x, int32 cell_y, int32 z) const -> voxel {
        const int32 cy = floor_div(cell_y, cells_per_chunk);

        const auto it = models_.find(cy);
        if (it == models_.end()) {
            return voxels::air;
        }
        return it->second->get_voxel(x, cell_y - (cy * cells_per_chunk), z);
    }

    [[nodiscard]] auto surface_cell_of(int32 x, int32 z) const -> std::optional<int32> {
        for (int32 cell_y = max_cell_; cell_y >= min_cell_; --cell_y) {
            if (at(x, cell_y, z) != voxels::air) {
                return cell_y;
            }
        }
        return std::nullopt;
    }

private:
    int32 voxels_per_cell_ = 1;
    int32 min_cell_        = std::numeric_limits<int32>::max();
    int32 max_cell_        = std::numeric_limits<int32>::lowest();
    std::map<int32, std::shared_ptr<asset::model>> models_;
};

}  // namespace

TEST_CASE("a coarser step keeps the surface within the soil it can shift", "[terrain][lod]") {
    asset::model_identity_pool identity_pool;
    asset::page_pool pages;

    perlin_terrain_generator::params params{};
    params.plants = false;
    perlin_terrain_generator gen{identity_pool, pages, params};

    const stepped_column fine{gen, 0, 0, 1};

    for (const int32 step : {2, 4}) {
        const stepped_column coarse{gen, 0, 0, step};

        const int32 shared = cells_per_chunk / step;
        int32 compared     = 0;

        for (int32 x = 0; x < shared; ++x) {
            for (int32 z = 0; z < shared; ++z) {
                const auto fine_top   = fine.surface_cell_of(x * step, z * step);
                const auto coarse_top = coarse.surface_cell_of(x, z);

                REQUIRE(fine_top.has_value());
                REQUIRE(coarse_top.has_value());

                const int32 drift = std::abs((*coarse_top * step) - *fine_top);
                REQUIRE(drift <= step);

                ++compared;
            }
        }

        REQUIRE(compared == shared * shared);
    }
}

TEST_CASE("a coarser step carries its scale into the models", "[terrain][lod]") {
    asset::model_identity_pool identity_pool;
    asset::page_pool pages;

    const perlin_terrain_generator::params params{};
    perlin_terrain_generator gen{identity_pool, pages, params};

    for (const int32 step : {1, 2, 4}) {
        const stepped_column column{gen, 0, 0, step};

        REQUIRE(column.scale_of_models() == params.world_units_per_voxel * step);
    }
}

TEST_CASE("a coarser step spends fewer chunks on the same world", "[terrain][lod]") {
    asset::model_identity_pool identity_pool;
    asset::page_pool pages;

    perlin_terrain_generator gen{identity_pool, pages, perlin_terrain_generator::params{}};

    const stepped_column fine{gen, 0, 0, 1};
    const stepped_column coarse{gen, 0, 0, 2};

    REQUIRE(coarse.chunk_count() < fine.chunk_count());
}
