#include <catch2/catch_test_macros.hpp>

import std;

import vw.core;
import vw.asset;
import vw.world;

using namespace vw;

// The table is what the flood asks "does this voxel emit" without dragging the
// registry into itself. It has to agree with the registry on every entry,
// because a voxel whose emission is written down in one place and read from the
// other would light the world differently from how it was authored.
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

// A voxel the catalog never registered has to read as dark rather than as
// whatever byte happened to sit at its index.
TEST_CASE("the emission table is dark outside the catalog", "[emission]") {
    const voxel_registry registry;
    const asset::emission_table table = asset::build_emission_table(registry);

    REQUIRE(table.get(voxel{voxel_category{200}, 7}) == 0);
    REQUIRE(table.get(voxel{voxels::world::category, 250}) == 0);
}

// A default-built table lights nothing, and the flood leans on that: the
// convenience constructors hand one over so a sky-only column needs no registry
// at all.
TEST_CASE("a default table emits nothing", "[emission]") {
    const asset::emission_table table{};

    REQUIRE(table.get(voxels::air) == 0);
    REQUIRE(table.get(voxels::world::lava) == 0);
    REQUIRE(table.get(voxel{voxel_category{99}, 7}) == 0);
}
