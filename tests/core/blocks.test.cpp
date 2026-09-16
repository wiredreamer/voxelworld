#include <catch2/catch_test_macros.hpp>

import std;
import vw.core;

using namespace vw;

TEST_CASE("air draws nothing and gives nothing", "[blocks]") {
    const block_registry registry;

    const block_type& air = registry.get(blocks::air);
    REQUIRE(air.surface == block_surface::invisible);
    REQUIRE(air.material.emission == 0);
    REQUIRE(air.material.glow == 0);
}

TEST_CASE("an ordinary block is unlit and unglowing", "[blocks]") {
    const block_registry registry;

    const block_type& grass = registry.get(blocks::terrain::grass[0]);
    REQUIRE(grass.surface == block_surface::opaque);
    REQUIRE(grass.material.emission == 0);
    REQUIRE(grass.material.glow == 0);
}

// Two properties, not one. What a block gives its neighbours is a level the
// flood carries; what it draws itself with is a brightness the shader adds
// outside every occluder. Lava has both, and nothing in the engine makes one
// follow from the other.
TEST_CASE("an emitter carries a flood level and a glow apart", "[blocks]") {
    const block_registry registry;

    const block_type& lava = registry.get(blocks::terrain::lava);
    REQUIRE(lava.material.emission == 15);
    REQUIRE(lava.material.glow == 255);

    // A crystal that glows without lighting the room is a glow with no light.
    const block_type& crystal = registry.get(blocks::terrain::crystal[0]);
    REQUIRE(crystal.material.emission == 0);
    REQUIRE(crystal.material.glow > 0);
}

// A level over fifteen cannot be baked: the quad keeps four bits a corner and
// the flood steps down by one, so the two numbers have to agree on a ceiling.
TEST_CASE("no block emits past the nibble", "[blocks]") {
    const block_registry registry;

    for (const block_type& block : registry.all()) {
        REQUIRE(block.material.emission <= 15);
    }
}

// The whole point of separating a block's identity from its colour. Ice and
// crystal are the same three steps of the blue ramp and differ only in glow;
// under the old registry the second one registered would have been unreachable.
TEST_CASE("two blocks may wear one colour", "[blocks]") {
    const block_registry registry;

    const block_type& ice     = registry.get(blocks::terrain::ice[0]);
    const block_type& crystal = registry.get(blocks::terrain::crystal[0]);

    REQUIRE(ice.material.clr == crystal.material.clr);
    REQUIRE(ice.id != crystal.id);
    REQUIRE(ice.slot != crystal.slot);
    REQUIRE(ice.material.glow != crystal.material.glow);
}

TEST_CASE("names are unique and resolve back to their block", "[blocks]") {
    const block_registry registry;

    std::vector<std::string_view> seen;
    for (const block_type& block : registry.all()) {
        if (block.id == blocks::air) {
            continue;
        }

        REQUIRE(registry.find(block.name) == block.id);
        REQUIRE(std::ranges::find(seen, block.name) == seen.end());
        seen.push_back(block.name);
    }
}

TEST_CASE("find returns nothing for a name outside the catalog", "[blocks]") {
    const block_registry registry;

    REQUIRE_FALSE(registry.find("terrain.unobtainium").has_value());
}

// Slots are the quad's ten bits. They are handed out densely so the palette on
// the device is exactly as long as the catalog.
TEST_CASE("slots are dense and within the quad's ten bits", "[blocks]") {
    const block_registry registry;

    REQUIRE(registry.all().size() <= block_slot_capacity);

    uint16 expected = 0;
    for (const block_type& block : registry.all()) {
        REQUIRE(block.slot.value == expected);
        ++expected;
    }
}

// Slot zero is the loud stand-in, not air: a block missing from the catalog has
// to be visible, and invisibility would hide the typo that produced it.
TEST_CASE("a block outside the catalog reads as the missing slot", "[blocks]") {
    const block_registry registry;

    REQUIRE(registry.slot_of(block_id{block_category{200}, 7}) == missing_block_slot);
    REQUIRE(registry.get(block_id{block_category{200}, 7}).name == "missing");
    REQUIRE(registry.slot_of(blocks::terrain::grass[0]) != missing_block_slot);
}

TEST_CASE("a category is independent of every other", "[blocks]") {
    REQUIRE(blocks::terrain::grass[0].category() == blocks::terrain::category);
    REQUIRE(blocks::palette::blue[0].category() == blocks::palette::category);
    REQUIRE(blocks::terrain::category != blocks::palette::category);

    // Same index in two categories, two different blocks.
    REQUIRE(blocks::terrain::grass[0].index() == blocks::palette::blue[0].index());
    REQUIRE(blocks::terrain::grass[0] != blocks::palette::blue[0]);
}

TEST_CASE("a material's variants sit next to each other", "[blocks]") {
    constexpr block_span grass = blocks::terrain::grass;

    REQUIRE(grass.count == 3);
    REQUIRE(grass[0].index() + 1 == grass[1].index());
    REQUIRE(grass[1].index() + 1 == grass[2].index());

    REQUIRE(grass.contains(grass[0]));
    REQUIRE(grass.contains(grass[2]));
    REQUIRE_FALSE(grass.contains(blocks::terrain::dirt[0]));
    REQUIRE_FALSE(grass.contains(blocks::palette::blue[0]));
}

// The generator picks a variant out of a noise value, so the value is whatever
// the noise happened to be and the span has to fold it itself.
TEST_CASE("picking a variant wraps on the count", "[blocks]") {
    constexpr block_span grass = blocks::terrain::grass;

    REQUIRE(grass.pick(0) == grass[0]);
    REQUIRE(grass.pick(3) == grass[0]);
    REQUIRE(grass.pick(4) == grass[1]);
    REQUIRE(grass.pick(1'000'001) == grass[2]);
}

TEST_CASE("a block table answers with its default outside a live category", "[blocks]") {
    block_table<uint8> table{7};

    REQUIRE(table.get(block_id{block_category{9}, 4}) == 7);

    table.set(block_id{block_category{9}, 4}, 12);
    REQUIRE(table.get(block_id{block_category{9}, 4}) == 12);

    // The row is per category, so a neighbour in the same one keeps the default
    // and a different category is untouched.
    REQUIRE(table.get(block_id{block_category{9}, 5}) == 7);
    REQUIRE(table.get(block_id{block_category{10}, 4}) == 7);
}

TEST_CASE("an extension recolours a catalog block in place", "[blocks]") {
    const block_registry base;

    const auto recoloured = std::array{
        block_desc{blocks::terrain::grass[0], "terrain.grass_0", block_material{colors::red_3}}
    };
    const block_registry extended{recoloured};

    REQUIRE(extended.all().size() == base.all().size());
    REQUIRE(extended.slot_of(blocks::terrain::grass[0]) ==
            base.slot_of(blocks::terrain::grass[0]));
    REQUIRE(extended.get(blocks::terrain::grass[0]).material.clr == colors::red_3);
}

TEST_CASE("an extension adds a block in a category of its own", "[blocks]") {
    constexpr auto modded = block_category{7};
    const auto extra      = std::array{
        block_desc{block_id{modded, 3}, "mod.thing", block_material{colors::blue_5}}
    };

    const block_registry registry{extra};

    REQUIRE(registry.all().size() == block_registry{}.all().size() + 1);
    REQUIRE(registry.get(block_id{modded, 3}).name == "mod.thing");
    REQUIRE(registry.slot_of(block_id{modded, 3}) != missing_block_slot);
}

TEST_CASE("the registry names the sets it knows", "[blocks]") {
    const block_registry registry;

    REQUIRE(registry.sets().size() >= 2);

    const block_set* terrain = registry.set_of(blocks::terrain::category);
    REQUIRE(terrain != nullptr);
    REQUIRE(terrain->name == "terrain");
    REQUIRE_FALSE(terrain->groups.empty());

    REQUIRE(terrain->kind == block_set_kind::materials);

    const block_set* palette = registry.set_of(blocks::palette::category);
    REQUIRE(palette != nullptr);
    REQUIRE(palette->name == "palette");
    REQUIRE(palette->kind == block_set_kind::palette);
}

// Whoever opens a document picks the set by what it is for -- a structure is
// built out of matter, everything else is painted -- and the names of the sets
// are the catalog's business, not theirs.
TEST_CASE("the registry finds a set by its kind", "[blocks]") {
    const block_registry registry;

    REQUIRE(registry.first_set(block_set_kind::palette)->category == blocks::palette::category);
    REQUIRE(registry.first_set(block_set_kind::materials)->category == blocks::terrain::category);
}

// The palette is an alphabet of colours: a colour missing from it is a colour
// the artist cannot reach, and a colour in it twice is a choice with nothing to
// choose between -- which the mesher then refuses to merge into one quad.
TEST_CASE("the palette holds every colour exactly once", "[blocks]") {
    const block_registry registry;

    const block_set* palette = registry.first_set(block_set_kind::palette);
    REQUIRE(palette != nullptr);

    for (const color& clr : colors::all) {
        const auto matte = std::ranges::count_if(registry.all(), [&](const block_type& block) {
            return block.id.category() == palette->category && block.material.clr == clr &&
                   block.material.glow == 0;
        });
        REQUIRE(matte == 1);
    }
}

// An extension may add blocks in a category of its own without adding a set for
// it. Whoever shows them has to cope, so the answer is nothing -- not a made-up
// name.
TEST_CASE("a category outside the catalog has no set", "[blocks]") {
    const block_registry registry;

    REQUIRE(registry.set_of(block_category{200}) == nullptr);
}

// Groups are what the palette walks, so a block outside every group would never
// be drawn and a block inside two would be drawn twice. The catalog asserts this
// at compile time; here it is stated where it can be read.
TEST_CASE("the groups of a set cover it once each", "[blocks]") {
    const block_registry registry;

    for (const block_set& set : registry.sets()) {
        std::array<int32, 256> seen{};

        for (const block_group& group : set.groups) {
            for (uint8 offset = 0; offset < group.count; ++offset) {
                const block_id id = group.at(offset);
                REQUIRE(id.category() == set.category);
                ++seen[id.index()];
            }
        }

        REQUIRE(seen[0] == 0);

        for (const block_type& block : registry.all()) {
            if (block.id.category() != set.category || block.id == blocks::air) {
                continue;
            }
            REQUIRE(seen[block.id.index()] == 1);
        }
    }
}

TEST_CASE("a group hands out the blocks it spans", "[blocks]") {
    constexpr auto group = block_group{"cool", blocks::palette::blue[0], 12};

    REQUIRE(group.at(0) == blocks::palette::blue[0]);
    REQUIRE(group.at(5) == blocks::palette::blue[5]);
    REQUIRE(group.at(6) == blocks::palette::green[0]);
}
