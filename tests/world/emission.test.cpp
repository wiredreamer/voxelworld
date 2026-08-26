#include <catch2/catch_test_macros.hpp>

import std;

import vw.core;
import vw.world;

using namespace vw;

// The table is what the flood asks "does this block emit" without dragging the
// registry into itself. It has to agree with the registry on every entry,
// because a block whose emission is written down in one place and read from the
// other would light the world differently from how it was authored.
TEST_CASE("the emission table mirrors the registry", "[emission]") {
    const block_registry registry;
    const asset::emission_table table = asset::build_emission_table(registry);

    REQUIRE(table.get(blocks::air) == 0);
    REQUIRE(table.get(blocks::terrain::grass_dry[2]) == 0);
    REQUIRE(table.get(blocks::terrain::glowstone) == 14);
    REQUIRE(table.get(blocks::terrain::lava) == 15);

    for (const block_type& block : registry.all()) {
        REQUIRE(table.get(block.id) == block.material.emission);
    }
}

// A block the catalog never registered has to read as dark rather than as
// whatever byte happened to sit at its index.
TEST_CASE("the emission table is dark outside the catalog", "[emission]") {
    const block_registry registry;
    const asset::emission_table table = asset::build_emission_table(registry);

    REQUIRE(table.get(block_id{block_category{200}, 7}) == 0);
    REQUIRE(table.get(block_id{blocks::terrain::category, 250}) == 0);
}

// A default-built table lights nothing, and the flood leans on that: the
// convenience constructors hand one over so a sky-only column needs no registry
// at all.
TEST_CASE("a default table emits nothing", "[emission]") {
    const asset::emission_table table{};

    REQUIRE(table.get(blocks::air) == 0);
    REQUIRE(table.get(blocks::terrain::lava) == 0);
    REQUIRE(table.get(block_id{block_category{99}, 7}) == 0);
}
