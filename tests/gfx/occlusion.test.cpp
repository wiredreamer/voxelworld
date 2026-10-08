#include <catch2/catch_test_macros.hpp>

import std;

import vw.core;
import vw.asset;
import vw.gfx;

using namespace vw;

TEST_CASE("the occluder pyramid halves down to one texel and packs its levels back to back", "[occlusion]") {
    const gfx::occluder_pyramid pyramid = gfx::pyramid_over(96, 54, 8);

    REQUIRE(pyramid.corner_count() == 97 * 55);
    REQUIRE(pyramid.levels[0] == gfx::occluder_level{.offset = 97 * 55 * 8, .width = 96, .height = 54});

    int32 next = pyramid.levels[0].offset;
    for (uint32 level = 0; level < pyramid.level_count; ++level) {
        const gfx::occluder_level& held = pyramid.levels[level];
        REQUIRE(held.offset == next);
        next += held.width * held.height;

        if (level > 0) {
            const gfx::occluder_level& finer = pyramid.levels[level - 1];
            REQUIRE(held.width == (finer.width + 1) / 2);
            REQUIRE(held.height == (finer.height + 1) / 2);
        }
    }

    const gfx::occluder_level& top = pyramid.levels[pyramid.level_count - 1];
    REQUIRE(top.width == 1);
    REQUIRE(top.height == 1);
    REQUIRE(pyramid.depth_count == static_cast<uint32>(next));
    REQUIRE(pyramid.level_count == 8);
}

TEST_CASE("an odd pyramid still ends in a single texel", "[occlusion]") {
    const gfx::occluder_pyramid pyramid = gfx::pyramid_over(5, 3, 1);

    REQUIRE(pyramid.level_count == 4);
    REQUIRE(pyramid.levels[1] == gfx::occluder_level{.offset = 24 + 15, .width = 3, .height = 2});
    REQUIRE(pyramid.levels[3].width == 1);
    REQUIRE(pyramid.levels[3].height == 1);
}

TEST_CASE("a mesh knows the box its quads reach and nothing beyond it", "[occlusion][mesh]") {
    asset::model_identity_pool ids;
    asset::page_pool pages;

    asset::model m{ids, pages, 64, 64, 64};
    m.set_voxel(10, 20, 30, voxels::gray[10]);
    m.set_voxel(11, 20, 30, voxels::gray[10]);
    m.set_voxel(40, 22, 33, voxels::gray[4]);

    const gfx::mesh simple = gfx::simple_mesh_generator::generate_mesh_data({.voxels = m});
    REQUIRE(simple.reach_min == vec3i{10, 20, 30});
    REQUIRE(simple.reach_max == vec3i{41, 23, 34});
    REQUIRE_FALSE(simple.sways);

    gfx::mesh_generation_storage storage;
    const gfx::mesh greedy = gfx::greedy_mesh_generator::generate_mesh_data(storage, {.voxels = m});
    REQUIRE(greedy.reach_min == simple.reach_min);
    REQUIRE(greedy.reach_max == simple.reach_max);

    const gfx::mesh coarse =
        gfx::greedy_mesh_generator::generate_mesh_data(storage, {.voxels = m}, {.lod_step = 4});
    REQUIRE(coarse.reach_min.x <= 8);
    REQUIRE(coarse.reach_max.x >= 44);
    REQUIRE(coarse.reach_min.y <= 20);
    REQUIRE(coarse.reach_max.y >= 23);
}

TEST_CASE("a mesh with foliage says so and an empty one reaches nowhere", "[occlusion][mesh]") {
    asset::model_identity_pool ids;
    asset::page_pool pages;

    asset::model leafy{ids, pages, 64, 64, 64};
    leafy.set_voxel(5, 5, 5, voxels::green[3], materials::leaves);
    REQUIRE(gfx::simple_mesh_generator::generate_mesh_data({.voxels = leafy}).sways);

    asset::model empty{ids, pages, 64, 64, 64};
    const gfx::mesh none = gfx::simple_mesh_generator::generate_mesh_data({.voxels = empty});
    REQUIRE(none.quads.empty());
    REQUIRE(none.reach_min == vec3i{});
    REQUIRE(none.reach_max == vec3i{});
}
