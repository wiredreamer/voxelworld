#include <catch2/catch_test_macros.hpp>

import std;

import vw.core;
import vw.asset;

using namespace vw;

namespace {

constexpr int32 side = 64;

constexpr material wood{7};
constexpr material leaf{9};

}  // namespace

TEST_CASE("a model without materials answers inert everywhere", "[model][material]") {
    asset::model_identity_pool ids;
    asset::page_pool pages;

    asset::model m{ids, pages, side, side, side};
    m.set_voxel(3, 4, 5, voxels::gray[8], material{});

    REQUIRE(m.get_material(3, 4, 5) == material{});
    REQUIRE(m.get_material(40, 40, 40) == material{});
    REQUIRE(m.get_page_material(0, 0, 0) == material{});
}

TEST_CASE("a voxel keeps the material it was set with and loses it when cleared", "[model][material]") {
    asset::model_identity_pool ids;
    asset::page_pool pages;

    asset::model m{ids, pages, side, side, side};
    m.set_voxel(3, 4, 5, voxels::amber[4], wood);
    m.set_voxel(4, 4, 5, voxels::amber[4], leaf);
    m.set_voxel(60, 60, 60, voxels::green[3], leaf);

    REQUIRE(m.get_material(3, 4, 5) == wood);
    REQUIRE(m.get_material(4, 4, 5) == leaf);
    REQUIRE(m.get_material(5, 4, 5) == material{});
    REQUIRE(m.get_material(60, 60, 60) == leaf);
    REQUIRE_FALSE(m.get_page_material(0, 0, 0).has_value());

    m.set_voxel(4, 4, 5, voxels::air, leaf);
    REQUIRE(m.get_material(4, 4, 5) == material{});
}

TEST_CASE("a page of one material folds to a single value and unfolds on a different one", "[model][material]") {
    asset::model_identity_pool ids;
    asset::page_pool pages;

    asset::model m{ids, pages, side, side, side};
    for (int32 z = 8; z < 16; ++z) {
        for (int32 y = 0; y < 8; ++y) {
            for (int32 x = 16; x < 24; ++x) {
                m.set_voxel(x, y, z, voxels::green[3], leaf);
            }
        }
    }
    REQUIRE_FALSE(m.get_page_material(2, 0, 1).has_value());

    static_cast<void>(m.compact_pages());
    REQUIRE(m.get_page_material(2, 0, 1) == leaf);
    REQUIRE(m.get_material(20, 3, 12) == leaf);

    m.set_voxel(20, 3, 12, voxels::amber[4], wood);
    REQUIRE_FALSE(m.get_page_material(2, 0, 1).has_value());
    REQUIRE(m.get_material(20, 3, 12) == wood);
    REQUIRE(m.get_material(21, 3, 12) == leaf);
}

TEST_CASE("clearing the last material drops the layer", "[model][material]") {
    asset::model_identity_pool ids;
    asset::page_pool pages;

    asset::model m{ids, pages, side, side, side};
    m.set_voxel(3, 4, 5, voxels::amber[4], wood);
    REQUIRE_FALSE(m.get_page_material(0, 0, 0).has_value());

    m.set_voxel(3, 4, 5, voxels::air);
    static_cast<void>(m.compact_pages());

    REQUIRE(m.get_page_material(0, 0, 0) == material{});
    REQUIRE(m.get_page_material(7, 7, 7) == material{});
}

TEST_CASE("a clone carries the materials of its source", "[model][material]") {
    asset::model_identity_pool ids;
    asset::page_pool pages;

    asset::model source{ids, pages, side, side, side};
    source.set_voxel(3, 4, 5, voxels::amber[4], wood);
    source.set_voxel(33, 4, 5, voxels::green[3], leaf);

    asset::model copy{ids, pages, side, side, side};
    copy.clone_pages_from(source);

    REQUIRE(copy.get_material(3, 4, 5) == wood);
    REQUIRE(copy.get_material(33, 4, 5) == leaf);

    source.set_voxel(3, 4, 5, voxels::amber[4], leaf);
    REQUIRE(copy.get_material(3, 4, 5) == wood);
}

TEST_CASE("rows of a material set name exactly its voxels", "[model][material]") {
    asset::model_identity_pool ids;
    asset::page_pool pages;

    asset::model m{ids, pages, side, side, side};
    m.set_voxel(3, 4, 5, voxels::amber[4], wood);
    m.set_voxel(4, 4, 5, voxels::green[3], leaf);
    m.set_voxel(63, 63, 63, voxels::green[3], leaf);

    material_set wanted;
    wanted.set(leaf.value);

    asset::chunk_occupancy rows;
    REQUIRE(m.build_rows_of(rows, wanted));

    REQUIRE(rows.test(4, 4, 5));
    REQUIRE(rows.test(63, 63, 63));
    REQUIRE_FALSE(rows.test(3, 4, 5));
    REQUIRE(((rows.zrow(4, 4) >> 5) & 1U) == 1);
    REQUIRE(((rows.zrow(4, 3) >> 5) & 1U) == 0);

    asset::face_occupancy plane;
    asset::face_occupancy held;
    REQUIRE(m.extract_face(face_direction::pos_x, plane, wanted, held));
    REQUIRE(plane.test(63, 63));
    REQUIRE(held.test(63, 63));
}
