#include <catch2/catch_test_macros.hpp>

import std;

import vw.core;
import vw.asset;
import vw.ecs;
import vw.world;

using namespace vw;
using namespace vw::ecs;

namespace {

constexpr int32 side = 64;

auto ground_chunk(world& w) -> std::shared_ptr<asset::chunk_volume> {
    auto model = w.resource<asset::model_registry>().create_unnamed(side, side, side);
    for (int32 z = 0; z < side; ++z) {
        for (int32 x = 0; x < side; ++x) {
            model->set_voxel(x, 0, z, voxels::brown[4]);
        }
    }
    return std::make_shared<asset::chunk_volume>(model);
}

}  // namespace

TEST_CASE("the grid names every chunk whose voxels came, went or changed", "[world][grid]") {
    world w;
    world_grid grid{w};

    const uint64 start = grid.occupancy_serial();

    static_cast<void>(grid.place_chunk({0, 0, 0}, ground_chunk(w)));
    static_cast<void>(grid.place_chunk({1, 0, 0}, ground_chunk(w)));
    grid.register_column({0, 0}, {0});
    grid.register_column({1, 0}, {0});

    const auto placed = grid.occupancy_changes_since(start);
    REQUIRE(placed);
    REQUIRE(placed->size() == 2);
    CHECK((*placed)[0] == vec3i{0, 0, 0});
    CHECK((*placed)[1] == vec3i{1, 0, 0});

    const uint64 settled = grid.occupancy_serial();
    REQUIRE(grid.occupancy_changes_since(settled)->empty());

    grid.set_voxel({5, 3, 5}, voxels::gray[8]);
    grid.unload_column({1, 0});

    const auto later = grid.occupancy_changes_since(settled);
    REQUIRE(later);
    REQUIRE(later->size() == 2);
    CHECK((*later)[0] == vec3i{0, 0, 0});
    CHECK((*later)[1] == vec3i{1, 0, 0});
}

TEST_CASE("a reader that fell too far behind is told to start over", "[world][grid]") {
    world w;
    world_grid grid{w};

    static_cast<void>(grid.place_chunk({0, 0, 0}, ground_chunk(w)));
    grid.register_column({0, 0}, {0});

    const uint64 start = grid.occupancy_serial();
    for (int32 i = 0; i < 9000; ++i) {
        grid.set_voxel({5, 3, 5}, (i % 2) == 0 ? voxels::gray[8] : voxels::air);
    }

    CHECK_FALSE(grid.occupancy_changes_since(start));
    CHECK(grid.occupancy_changes_since(grid.occupancy_serial()));
}

TEST_CASE("serials of two grids never answer for each other", "[world][grid]") {
    world w;
    world_grid first{w};
    world_grid second{w};

    static_cast<void>(first.place_chunk({0, 0, 0}, ground_chunk(w)));

    CHECK_FALSE(second.occupancy_changes_since(first.occupancy_serial()));
    CHECK_FALSE(first.occupancy_changes_since(second.occupancy_serial()));
}
