#include <catch2/catch_test_macros.hpp>

import std;
import vw.core;

using namespace vw;

// Emptiness is the raw value being zero, and the page table, the occupancy walk
// and the light flood all lean on that. Only category zero index zero is air; an
// index of zero in any other category is an ordinary voxel.
TEST_CASE("only air is empty", "[voxels]") {
    static_assert(voxels::air.is_empty());
    REQUIRE(voxel{}.is_empty());
    REQUIRE(voxel{} == voxels::air);
    REQUIRE(voxels::air.value == 0);

    REQUIRE_FALSE(voxels::world::grass[0].is_empty());
    REQUIRE_FALSE(voxels::palette::amber[3].is_empty());
    REQUIRE_FALSE(voxel::from_raw(0xFFFF).is_empty());
}

TEST_CASE("voxel equality", "[voxels]") {
    REQUIRE(voxels::world::grass[0] == voxels::world::grass[0]);
    REQUIRE_FALSE(voxels::world::grass[0] == voxels::world::grass[1]);
}

// A voxel carries a full identity, but a page stores only the index within its
// model's set -- one byte, which is what model::build_x_rows folds eight at a
// time out of a machine word.
TEST_CASE("a page entry is one byte, an identity two", "[voxels]") {
    static_assert(sizeof(voxel_index) == 1);
    static_assert(sizeof(voxel) == 2);
}

// Zero is empty in every set, and the catalog numbers from one so that a zero
// byte can only ever mean air.
TEST_CASE("index zero is empty in any set", "[voxels]") {
    REQUIRE(voxel_index{}.is_empty());
    REQUIRE(voxel_index{0}.is_empty());
    REQUIRE_FALSE(voxel_index{1}.is_empty());

    REQUIRE(voxels::world::grass[0].index() >= 1);
    REQUIRE(voxels::palette::blue[0].index() >= 1);
}

TEST_CASE("air draws nothing and gives nothing", "[voxels]") {
    const voxel_registry registry;

    const voxel_type& air = registry.get(voxels::air);
    REQUIRE(air.surface == voxel_surface::invisible);
    REQUIRE(air.material.emission == 0);
    REQUIRE(air.material.glow == 0);
}

TEST_CASE("an ordinary voxel is unlit and unglowing", "[voxels]") {
    const voxel_registry registry;

    const voxel_type& grass = registry.get(voxels::world::grass[0]);
    REQUIRE(grass.surface == voxel_surface::opaque);
    REQUIRE(grass.material.emission == 0);
    REQUIRE(grass.material.glow == 0);
}

// Two properties, not one. What a voxel gives its neighbours is a level the
// flood carries; what it draws itself with is a brightness the shader adds
// outside every occluder. Lava has both, and nothing in the engine makes one
// follow from the other.
TEST_CASE("an emitter carries a flood level and a glow apart", "[voxels]") {
    const voxel_registry registry;

    const voxel_type& lava = registry.get(voxels::world::lava);
    REQUIRE(lava.material.emission == 15);
    REQUIRE(lava.material.glow == 255);

    // A crystal that glows without lighting the room is a glow with no light.
    const voxel_type& crystal = registry.get(voxels::world::crystal[0]);
    REQUIRE(crystal.material.emission == 0);
    REQUIRE(crystal.material.glow > 0);
}

// A level over fifteen cannot be baked: the quad keeps four bits a corner and
// the flood steps down by one, so the two numbers have to agree on a ceiling.
TEST_CASE("no voxel emits past the nibble", "[voxels]") {
    const voxel_registry registry;

    for (const voxel_type& type : registry.all()) {
        REQUIRE(type.material.emission <= 15);
    }
}

// The whole point of separating a voxel's identity from its colour. Ice and
// crystal are the same three steps of the blue ramp and differ only in glow;
// under the old registry the second one registered would have been unreachable.
TEST_CASE("two voxels may wear one colour", "[voxels]") {
    const voxel_registry registry;

    const voxel_type& ice     = registry.get(voxels::world::ice[0]);
    const voxel_type& crystal = registry.get(voxels::world::crystal[0]);

    REQUIRE(ice.material.clr == crystal.material.clr);
    REQUIRE(ice.id != crystal.id);
    REQUIRE(ice.slot != crystal.slot);
    REQUIRE(ice.material.glow != crystal.material.glow);
}

TEST_CASE("names are unique and resolve back to their voxel", "[voxels]") {
    const voxel_registry registry;

    std::vector<std::string_view> seen;
    for (const voxel_type& type : registry.all()) {
        if (type.id == voxels::air) {
            continue;
        }

        REQUIRE(registry.find(type.name) == type.id);
        REQUIRE(std::ranges::find(seen, type.name) == seen.end());
        seen.push_back(type.name);
    }
}

TEST_CASE("find returns nothing for a name outside the catalog", "[voxels]") {
    const voxel_registry registry;

    REQUIRE_FALSE(registry.find("world.unobtainium").has_value());
}

// Slots are the quad's ten bits. They are handed out densely so the palette on
// the device is exactly as long as the catalog.
TEST_CASE("slots are dense and within the quad's ten bits", "[voxels]") {
    const voxel_registry registry;

    REQUIRE(registry.all().size() <= voxel_slot_capacity);

    uint16 expected = 0;
    for (const voxel_type& type : registry.all()) {
        REQUIRE(type.slot.value == expected);
        ++expected;
    }
}

// Slot zero is the loud stand-in, not air: a voxel missing from the catalog has
// to be visible, and invisibility would hide the typo that produced it.
TEST_CASE("a voxel outside the catalog reads as the missing slot", "[voxels]") {
    const voxel_registry registry;

    REQUIRE(registry.slot_of(voxel{voxel_category{200}, 7}) == missing_voxel_slot);
    REQUIRE(registry.get(voxel{voxel_category{200}, 7}).name == "missing");
    REQUIRE(registry.slot_of(voxels::world::grass[0]) != missing_voxel_slot);
}

TEST_CASE("a category is independent of every other", "[voxels]") {
    REQUIRE(voxels::world::grass[0].category() == voxels::world::category);
    REQUIRE(voxels::palette::blue[0].category() == voxels::palette::category);
    REQUIRE(voxels::world::category != voxels::palette::category);

    // Same index in two categories, two different voxels.
    REQUIRE(voxels::world::grass[0].index() == voxels::palette::blue[0].index());
    REQUIRE(voxels::world::grass[0] != voxels::palette::blue[0]);
}

TEST_CASE("a material's variants sit next to each other", "[voxels]") {
    constexpr voxel_span grass = voxels::world::grass;

    REQUIRE(grass.count == 3);
    REQUIRE(grass[0].index() + 1 == grass[1].index());
    REQUIRE(grass[1].index() + 1 == grass[2].index());

    REQUIRE(grass.contains(grass[0]));
    REQUIRE(grass.contains(grass[2]));
    REQUIRE_FALSE(grass.contains(voxels::world::dirt[0]));
    REQUIRE_FALSE(grass.contains(voxels::palette::blue[0]));
}

// The generator picks a variant out of a noise value, so the value is whatever
// the noise happened to be and the span has to fold it itself.
TEST_CASE("picking a variant wraps on the count", "[voxels]") {
    constexpr voxel_span grass = voxels::world::grass;

    REQUIRE(grass.pick(0) == grass[0]);
    REQUIRE(grass.pick(3) == grass[0]);
    REQUIRE(grass.pick(4) == grass[1]);
    REQUIRE(grass.pick(1'000'001) == grass[2]);
}

TEST_CASE("a voxel table answers with its default outside a live category", "[voxels]") {
    voxel_table<uint8> table{7};

    REQUIRE(table.get(voxel{voxel_category{9}, 4}) == 7);

    table.set(voxel{voxel_category{9}, 4}, 12);
    REQUIRE(table.get(voxel{voxel_category{9}, 4}) == 12);

    // The row is per category, so a neighbour in the same one keeps the default
    // and a different category is untouched.
    REQUIRE(table.get(voxel{voxel_category{9}, 5}) == 7);
    REQUIRE(table.get(voxel{voxel_category{10}, 4}) == 7);
}

TEST_CASE("an extension recolours a catalog voxel in place", "[voxels]") {
    const voxel_registry base;

    const auto recoloured = std::array{
        voxel_desc{voxels::world::grass[0], "world.grass_0", voxel_material{colors::red_3}}
    };
    const voxel_registry extended{recoloured};

    REQUIRE(extended.all().size() == base.all().size());
    REQUIRE(extended.slot_of(voxels::world::grass[0]) ==
            base.slot_of(voxels::world::grass[0]));
    REQUIRE(extended.get(voxels::world::grass[0]).material.clr == colors::red_3);
}

TEST_CASE("an extension adds a voxel in a category of its own", "[voxels]") {
    constexpr auto modded = voxel_category{7};
    const auto extra      = std::array{
        voxel_desc{voxel{modded, 3}, "mod.thing", voxel_material{colors::blue_5}}
    };

    const voxel_registry registry{extra};

    REQUIRE(registry.all().size() == voxel_registry{}.all().size() + 1);
    REQUIRE(registry.get(voxel{modded, 3}).name == "mod.thing");
    REQUIRE(registry.slot_of(voxel{modded, 3}) != missing_voxel_slot);
}

TEST_CASE("the registry names the sets it knows", "[voxels]") {
    const voxel_registry registry;

    REQUIRE(registry.sets().size() >= 2);

    const voxel_set* world = registry.set_of(voxels::world::category);
    REQUIRE(world != nullptr);
    REQUIRE(world->name == "world");
    REQUIRE_FALSE(world->groups.empty());

    REQUIRE(world->kind == voxel_set_kind::materials);

    const voxel_set* palette = registry.set_of(voxels::palette::category);
    REQUIRE(palette != nullptr);
    REQUIRE(palette->name == "palette");
    REQUIRE(palette->kind == voxel_set_kind::palette);
}

// Whoever opens a document picks the set by what it is for -- a structure is
// built out of matter, everything else is painted -- and the names of the sets
// are the catalog's business, not theirs.
TEST_CASE("the registry finds a set by its kind", "[voxels]") {
    const voxel_registry registry;

    REQUIRE(registry.first_set(voxel_set_kind::palette)->category == voxels::palette::category);
    REQUIRE(registry.first_set(voxel_set_kind::materials)->category == voxels::world::category);
}

// The palette is an alphabet of colours: a colour missing from it is a colour
// the artist cannot reach, and a colour in it twice is a choice with nothing to
// choose between -- which the mesher then refuses to merge into one quad.
TEST_CASE("the palette holds every colour exactly once", "[voxels]") {
    const voxel_registry registry;

    const voxel_set* palette = registry.first_set(voxel_set_kind::palette);
    REQUIRE(palette != nullptr);

    for (const color& clr : colors::all) {
        const auto matte = std::ranges::count_if(registry.all(), [&](const voxel_type& type) {
            return type.id.category() == palette->category && type.material.clr == clr &&
                   type.material.glow == 0;
        });
        REQUIRE(matte == 1);
    }
}

// An extension may add voxels in a category of its own without adding a set for
// it. Whoever shows them has to cope, so the answer is nothing -- not a made-up
// name.
TEST_CASE("a category outside the catalog has no set", "[voxels]") {
    const voxel_registry registry;

    REQUIRE(registry.set_of(voxel_category{200}) == nullptr);
}

// Groups are what the palette walks, so a voxel outside every group would never
// be drawn and a voxel inside two would be drawn twice. The catalog asserts this
// at compile time; here it is stated where it can be read.
TEST_CASE("the groups of a set cover it once each", "[voxels]") {
    const voxel_registry registry;

    for (const voxel_set& set : registry.sets()) {
        std::array<int32, 256> seen{};

        for (const voxel_group& group : set.groups) {
            for (uint8 offset = 0; offset < group.count; ++offset) {
                const voxel id = group.at(offset);
                REQUIRE(id.category() == set.category);
                ++seen[id.index()];
            }
        }

        REQUIRE(seen[0] == 0);

        for (const voxel_type& type : registry.all()) {
            if (type.id.category() != set.category || type.id == voxels::air) {
                continue;
            }
            REQUIRE(seen[type.id.index()] == 1);
        }
    }
}

TEST_CASE("a group hands out the voxels it spans", "[voxels]") {
    constexpr auto group = voxel_group{"cool", voxels::palette::blue[0], 12};

    REQUIRE(group.at(0) == voxels::palette::blue[0]);
    REQUIRE(group.at(5) == voxels::palette::blue[5]);
    REQUIRE(group.at(6) == voxels::palette::green[0]);
}
