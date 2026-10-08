#include <catch2/catch_test_macros.hpp>

import std;

import vw.core;
import vw.asset;
import vw.world;

using namespace vw;

TEST_CASE("the emission levels mirror the material table", "[emission]") {
    const voxel_registry registry;
    const material_table materials{registry};
    const material_levels levels = materials.emission();

    REQUIRE(levels[0] == 0);
    REQUIRE(levels[materials.of(voxels::green[10]).value] == 0);
    REQUIRE(levels[materials.of(voxels::lamp_amber).value] == 14);
    REQUIRE(levels[materials.of(voxels::fire_red).value] == 15);

    for (std::size_t row = 0; row < materials.all().size(); ++row) {
        REQUIRE(levels[row] == materials.all()[row].emission);
    }
}

TEST_CASE("default levels emit nothing", "[emission]") {
    const material_levels levels{};

    REQUIRE(std::ranges::all_of(levels, [](uint8 level) { return level == 0; }));
}
