#include <catch2/catch_test_macros.hpp>

import std;

import vw.core;
import vw.asset;
import vw.world;

using namespace vw;

TEST_CASE("the emission table mirrors the registry", "[emission]") {
    const voxel_registry registry;
    const asset::emission_table table = asset::build_emission_table(registry);

    REQUIRE(table.get(voxels::air) == 0);
    REQUIRE(table.get(voxels::world::grass_dry[2]) == 0);
    REQUIRE(table.get(voxels::world::glowstone) == 14);
    REQUIRE(table.get(voxels::world::lava) == 15);

    for (const voxel_type& type : registry.all()) {
        REQUIRE(table.get(type.id) == type.material.emission);
    }
}

TEST_CASE("the emission table is dark outside the catalog", "[emission]") {
    const voxel_registry registry;
    const asset::emission_table table = asset::build_emission_table(registry);

    REQUIRE(table.get(voxel{voxel_category{200}, 7}) == 0);
    REQUIRE(table.get(voxel{voxels::world::category, 250}) == 0);
}

TEST_CASE("a default table emits nothing", "[emission]") {
    const asset::emission_table table{};

    REQUIRE(table.get(voxels::air) == 0);
    REQUIRE(table.get(voxels::world::lava) == 0);
    REQUIRE(table.get(voxel{voxel_category{99}, 7}) == 0);
}
