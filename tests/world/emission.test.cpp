#include <catch2/catch_test_macros.hpp>

import std;

import vw.core;
import vw.asset;
import vw.world;

using namespace vw;

TEST_CASE("the emission table mirrors the registry", "[emission]") {
    const voxel_registry registry;
    const asset::emission_table table = asset::build_emission_table(registry);

    REQUIRE(table[voxels::air.value] == 0);
    REQUIRE(table[voxels::green[5].value] == 0);
    REQUIRE(table[voxels::lamp_amber.value] == 14);
    REQUIRE(table[voxels::fire_red.value] == 15);

    for (const voxel_type& type : registry.all()) {
        REQUIRE(table[type.id.value] == type.material.emission);
    }
}

TEST_CASE("the emission table is dark outside the catalog", "[emission]") {
    const voxel_registry registry;
    const asset::emission_table table = asset::build_emission_table(registry);

    REQUIRE(table[200] == 0);
    REQUIRE(table[250] == 0);
}

TEST_CASE("a default table emits nothing", "[emission]") {
    const asset::emission_table table{};

    REQUIRE(table[voxels::air.value] == 0);
    REQUIRE(table[voxels::fire_red.value] == 0);
    REQUIRE(table[99] == 0);
}
