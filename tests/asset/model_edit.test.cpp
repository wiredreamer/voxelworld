#include <catch2/catch_test_macros.hpp>

import std;

import vw.core;
import vw.asset;

using namespace vw;

namespace {

constexpr int32 side = 64;

}  // namespace

TEST_CASE("a writer raises the generation once for the whole scope", "[model]") {
    asset::model_identity_pool ids;
    asset::page_pool pages;

    asset::model m{ids, pages, side, side, side};
    const uint32 before = m.get_identity().generation;

    {
        asset::model_writer writer{m};
        for (int32 x = 0; x < 8; ++x) {
            writer.set(x, 0, 0, voxels::gray[4]);
        }
    }

    REQUIRE(m.get_identity().generation == before + 1);
    REQUIRE(m.get_voxel(7, 0, 0) == voxels::gray[4]);
}

TEST_CASE("a writer that wrote nothing leaves the generation alone", "[model]") {
    asset::model_identity_pool ids;
    asset::page_pool pages;

    asset::model m{ids, pages, side, side, side};
    const uint32 before = m.get_identity().generation;

    {
        asset::model_writer writer{m};
    }

    REQUIRE(m.get_identity().generation == before);
}

TEST_CASE("a page fill costs one page entry, not a page of voxels", "[model]") {
    asset::model_identity_pool ids;
    asset::page_pool pages;

    asset::model m{ids, pages, side, side, side};

    {
        asset::model_writer writer{m};
        writer.fill_page(0, 0, 0, voxels::gray[4]);
    }

    REQUIRE(m.get_page_mode(0, 0, 0) == asset::page_mode::uniform);
    REQUIRE(m.get_voxel(5, 5, 5) == voxels::gray[4]);
    REQUIRE(m.is_empty(8, 0, 0));
}

TEST_CASE("a batch applies in order and raises the generation once", "[model]") {
    asset::model_identity_pool ids;
    asset::page_pool pages;

    asset::model m{ids, pages, side, side, side};
    const uint32 before = m.get_identity().generation;

    asset::voxel_batch batch;
    batch.fill_page(vec3i{1, 0, 0}, voxels::gray[4])
        .set(vec3i{8, 0, 0}, voxels::amber[2])
        .set(vec3i{8, 0, 0}, voxels::gray[2]);

    REQUIRE(batch.size() == 3);

    batch.apply_to(m);

    REQUIRE(m.get_identity().generation == before + 1);
    REQUIRE(m.get_voxel(8, 0, 0) == voxels::gray[2]);
    REQUIRE(m.get_voxel(9, 0, 0) == voxels::gray[4]);
}

TEST_CASE("a batch can be applied to more than one model", "[model]") {
    asset::model_identity_pool ids;
    asset::page_pool pages;

    asset::model first{ids, pages, side, side, side};
    asset::model second{ids, pages, side, side, side};

    asset::voxel_batch batch;
    batch.set(vec3i{2, 2, 2}, voxels::lamp_amber);

    batch.apply_to(first);
    batch.apply_to(second);

    REQUIRE(first.get_voxel(2, 2, 2) == voxels::lamp_amber);
    REQUIRE(second.get_voxel(2, 2, 2) == voxels::lamp_amber);

    batch.clear();
    REQUIRE(batch.empty());
}
