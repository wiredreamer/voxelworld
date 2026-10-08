#include <catch2/catch_test_macros.hpp>

import std;
import vw.core;

using namespace vw;

TEST_CASE("only air is empty", "[voxels]") {
    static_assert(voxels::air.is_empty());
    REQUIRE(voxel{}.is_empty());
    REQUIRE(voxel{} == voxels::air);
    REQUIRE(voxels::air.value == 0);

    REQUIRE_FALSE(voxels::green[4].is_empty());
    REQUIRE_FALSE(voxels::amber[6].is_empty());
    REQUIRE_FALSE(voxel{255}.is_empty());
}

TEST_CASE("voxel equality", "[voxels]") {
    REQUIRE(voxels::green[4] == voxels::green[4]);
    REQUIRE_FALSE(voxels::green[4] == voxels::green[6]);
}

TEST_CASE("a voxel is one byte", "[voxels]") {
    static_assert(sizeof(voxel) == 1);
    static_assert(voxel_type_capacity == 256);
}

TEST_CASE("value zero is empty and nothing else is", "[voxels]") {
    REQUIRE(voxel{}.is_empty());
    REQUIRE(voxel{0}.is_empty());
    REQUIRE_FALSE(voxel{1}.is_empty());

    REQUIRE(voxels::green[4].value >= 1);
    REQUIRE(voxels::blue[0].value >= 1);
}

TEST_CASE("air draws nothing", "[voxels]") {
    const voxel_registry registry;

    REQUIRE(registry.get(voxels::air).surface == voxel_surface::invisible);
    REQUIRE(registry.get(voxels::green[4]).surface == voxel_surface::opaque);
}

TEST_CASE("names are unique and resolve back to their voxel", "[voxels]") {
    const voxel_registry registry;

    std::vector<std::string_view> seen;
    for (const voxel_type& type : registry.all()) {
        if (type.id == voxels::air || !registry.known(type.id)) {
            continue;
        }

        REQUIRE(registry.find(type.name) == type.id);
        REQUIRE(std::ranges::find(seen, type.name) == seen.end());
        seen.push_back(type.name);
    }
}

TEST_CASE("find returns nothing for a name outside the catalog", "[voxels]") {
    const voxel_registry registry;

    REQUIRE_FALSE(registry.find("unobtainium").has_value());
}

TEST_CASE("the registry addresses every byte", "[voxels]") {
    const voxel_registry registry;

    REQUIRE(registry.all().size() == voxel_type_capacity);

    uint32 expected = 0;
    for (const voxel_type& type : registry.all()) {
        REQUIRE(type.id.value == expected);
        ++expected;
    }
}

TEST_CASE("a voxel outside the catalog reads as missing", "[voxels]") {
    const voxel_registry registry;

    REQUIRE_FALSE(registry.known(voxel{200}));
    REQUIRE(registry.get(voxel{200}).name == "missing");
    REQUIRE(registry.known(voxels::green[4]));
}

TEST_CASE("the catalog ends where the colours end", "[voxels]") {
    const voxel_registry registry;

    REQUIRE(registry.known(voxels::black));
    for (uint32 value = voxels::black.value + 1U; value < voxel_type_capacity; ++value) {
        REQUIRE_FALSE(registry.known(voxel{static_cast<uint8>(value)}));
    }
}

TEST_CASE("a colour's shades sit next to each other", "[voxels]") {
    constexpr voxel_span green = voxels::green;

    REQUIRE(green.count == 11);
    REQUIRE(green[0].value + 1 == green[1].value);
    REQUIRE(green[1].value + 1 == green[2].value);

    REQUIRE(green.contains(green[0]));
    REQUIRE(green.contains(green[10]));
    REQUIRE_FALSE(green.contains(voxels::brown[0]));
    REQUIRE_FALSE(green.contains(voxels::blue[0]));
}

TEST_CASE("picking a shade wraps on the count", "[voxels]") {
    constexpr voxel_span blue = voxels::blue;

    REQUIRE(blue.pick(0) == blue[0]);
    REQUIRE(blue.pick(11) == blue[0]);
    REQUIRE(blue.pick(12) == blue[1]);
    REQUIRE(blue.pick(1'000'001) == blue[2]);
}

TEST_CASE("an extension recolours a catalog voxel in place", "[voxels]") {
    const voxel_registry base;

    const auto recoloured =
        std::array{voxel_desc{voxels::green[4], "green_4", voxel_material{colors::red_6}}};
    const voxel_registry extended{recoloured};

    REQUIRE(extended.all().size() == base.all().size());
    REQUIRE(extended.get(voxels::green[4]).material.clr == colors::red_6);
}

TEST_CASE("an extension claims a value the catalog left free", "[voxels]") {
    constexpr auto modded = voxel{200};
    const auto extra =
        std::array{voxel_desc{modded, "mod.thing", voxel_material{colors::blue_10}}};

    const voxel_registry registry{extra};

    REQUIRE_FALSE(voxel_registry{}.known(modded));
    REQUIRE(registry.known(modded));
    REQUIRE(registry.get(modded).name == "mod.thing");
}

TEST_CASE("the groups cover the catalog once each", "[voxels]") {
    const voxel_registry registry;

    std::array<int32, voxel_type_capacity> seen{};

    for (const voxel_group& group : registry.groups()) {
        for (uint8 offset = 0; offset < group.count; ++offset) {
            ++seen[group.at(offset).value];
        }
    }

    REQUIRE(seen[0] == 0);

    for (const voxel_type& type : registry.all()) {
        if (type.id == voxels::air || !registry.known(type.id)) {
            continue;
        }
        REQUIRE(seen[type.id.value] == 1);
    }
}

TEST_CASE("the catalog holds every colour exactly once", "[voxels]") {
    const voxel_registry registry;

    for (const color& clr : colors::all) {
        const auto held = std::ranges::count_if(registry.all(), [&](const voxel_type& type) {
            return registry.known(type.id) && type.material.clr == clr;
        });
        REQUIRE(held == 1);
    }
}

TEST_CASE("a group hands out the voxels it spans", "[voxels]") {
    constexpr auto group = voxel_group{"cool", voxels::blue[0], 22};

    REQUIRE(group.at(0) == voxels::blue[0]);
    REQUIRE(group.at(5) == voxels::blue[5]);
    REQUIRE(group.at(11) == voxels::green[0]);
}

TEST_CASE("the material table names its rows and keeps the inert one first", "[voxels][material]") {
    const material_table table;

    REQUIRE(table.all().size() == static_cast<std::size_t>(material_capacity));
    REQUIRE(table.named().size() == default_material_catalog.size());

    REQUIRE(table.get(materials::inert) == material_type{.name = "inert"});
    REQUIRE(table.find("inert") == materials::inert);
    REQUIRE(table.find("wood") == materials::wood);
    REQUIRE(table.find("leaves") == materials::leaves);
    REQUIRE(table.find("glow") == materials::glow);
    REQUIRE(table.find("lamp") == materials::lamp);
    REQUIRE(table.find("fire") == materials::fire);
    REQUIRE_FALSE(table.find("granite").has_value());
    REQUIRE_FALSE(table.find("").has_value());
}

TEST_CASE("the three glowing materials differ in emission and glow", "[voxels][material]") {
    const material_table table;

    const material_type& glow = table.get(materials::glow);
    const material_type& lamp = table.get(materials::lamp);
    const material_type& fire = table.get(materials::fire);

    REQUIRE(glow.emission == 0);
    REQUIRE(glow.glow > 0);
    REQUIRE(lamp.emission == 14);
    REQUIRE(fire.emission == 15);
    REQUIRE(fire.glow > lamp.glow);

    for (const material_type& row : table.all()) {
        REQUIRE(row.emission <= 15);
    }
}

TEST_CASE("only leaves sway, and a row past the catalog is inert", "[voxels][material]") {
    const material_table table;

    const material_set swaying = table.swaying();
    REQUIRE(swaying.count() == 1);
    REQUIRE(swaying.test(materials::leaves.value));

    REQUIRE(table.get(material{200}) == material_type{});
    REQUIRE(table.emission()[200] == 0);
    REQUIRE(table.emission()[materials::lamp.value] == 14);
}

TEST_CASE("matter is empty by its colour and inert unless told otherwise", "[voxels][material]") {
    REQUIRE(matter{}.is_empty());
    REQUIRE(matter{voxels::air, materials::lamp}.is_empty());
    REQUIRE_FALSE(matter{voxels::green[4]}.is_empty());

    const matter plain = voxels::green[4];
    REQUIRE(plain.made_of == materials::inert);
    REQUIRE(plain != matter{voxels::green[4], materials::leaves});
}
