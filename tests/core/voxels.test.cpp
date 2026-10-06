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

TEST_CASE("air draws nothing and gives nothing", "[voxels]") {
    const voxel_registry registry;

    const voxel_type& air = registry.get(voxels::air);
    REQUIRE(air.surface == voxel_surface::invisible);
    REQUIRE(air.material.emission == 0);
    REQUIRE(air.material.glow == 0);
}

TEST_CASE("an ordinary colour is unlit and unglowing", "[voxels]") {
    const voxel_registry registry;

    const voxel_type& green = registry.get(voxels::green[4]);
    REQUIRE(green.surface == voxel_surface::opaque);
    REQUIRE(green.material.emission == 0);
    REQUIRE(green.material.glow == 0);
}

TEST_CASE("the three tiers differ only in emission and glow", "[voxels]") {
    const voxel_registry registry;

    const voxel_type& base = registry.get(voxels::red[8]);
    const voxel_type& glow = registry.get(voxels::glow_red);
    const voxel_type& lamp = registry.get(voxels::lamp_red);
    const voxel_type& fire = registry.get(voxels::fire_red);

    REQUIRE(base.material.clr == glow.material.clr);
    REQUIRE(base.material.clr == lamp.material.clr);
    REQUIRE(base.material.clr == fire.material.clr);

    REQUIRE(glow.material.emission == 0);
    REQUIRE(glow.material.glow > 0);

    REQUIRE(lamp.material.emission == 14);
    REQUIRE(fire.material.emission == 15);
    REQUIRE(fire.material.glow > lamp.material.glow);
}

TEST_CASE("no voxel emits past the nibble", "[voxels]") {
    const voxel_registry registry;

    for (const voxel_type& type : registry.all()) {
        REQUIRE(type.material.emission <= 15);
    }
}

TEST_CASE("two voxels may wear one colour", "[voxels]") {
    const voxel_registry registry;

    const voxel_type& base = registry.get(voxels::blue[8]);
    const voxel_type& glow = registry.get(voxels::glow_blue);

    REQUIRE(base.material.clr == glow.material.clr);
    REQUIRE(base.id != glow.id);
    REQUIRE(base.material.glow != glow.material.glow);
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

TEST_CASE("colours and tiers share one index space", "[voxels]") {
    REQUIRE(voxels::green[4] != voxels::blue[0]);
    REQUIRE(voxels::glow_red != voxels::red[8]);
    REQUIRE(voxels::lamp_red != voxels::glow_red);
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

TEST_CASE("the catalog holds every colour exactly once as a matte voxel", "[voxels]") {
    const voxel_registry registry;

    for (const color& clr : colors::all) {
        const auto matte = std::ranges::count_if(registry.all(), [&](const voxel_type& type) {
            return registry.known(type.id) && type.material.clr == clr &&
                   type.material.glow == 0 && type.material.emission == 0;
        });
        REQUIRE(matte == 1);
    }
}

TEST_CASE("a group hands out the voxels it spans", "[voxels]") {
    constexpr auto group = voxel_group{"cool", voxels::blue[0], 22};

    REQUIRE(group.at(0) == voxels::blue[0]);
    REQUIRE(group.at(5) == voxels::blue[5]);
    REQUIRE(group.at(11) == voxels::green[0]);
}
