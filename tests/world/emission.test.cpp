#include <catch2/catch_test_macros.hpp>

import std;

import vw.core;
import vw.asset;
import vw.world;

using namespace vw;

TEST_CASE("the emission levels mirror the material table", "[emission]") {
    const material_table table;
    const material_levels levels = table.emission();

    REQUIRE(levels[materials::inert.value] == 0);
    REQUIRE(levels[materials::leaves.value] == 0);
    REQUIRE(levels[materials::lamp.value] == 14);
    REQUIRE(levels[materials::fire.value] == 15);

    for (std::size_t row = 0; row < table.all().size(); ++row) {
        REQUIRE(levels[row] == table.all()[row].emission);
    }
}

TEST_CASE("default levels emit nothing", "[emission]") {
    const material_levels levels{};

    REQUIRE(std::ranges::all_of(levels, [](uint8 level) { return level == 0; }));
}
