#include <catch2/catch_test_macros.hpp>

import std;

import vw.core;
import vw.asset;

using namespace vw;

namespace {

constexpr int32 side = 64;

auto solid_cube(asset::model_identity_pool& ids, asset::page_pool& pages)
    -> std::unique_ptr<asset::model> {
    auto m = std::make_unique<asset::model>(ids, pages, side, side, side);
    m->fill(voxels::gray[4]);
    return m;
}

}  // namespace

TEST_CASE("the page table says what a volume is made of", "[model]") {
    asset::model_identity_pool ids;
    asset::page_pool pages;

    asset::model m{ids, pages, side, side, side};
    REQUIRE(m.scan_fill() == asset::model_fill::air);

    m.fill(voxels::gray[4]);
    REQUIRE(m.scan_fill() == asset::model_fill::solid);

    m.set_voxel(3, 3, 3, voxels::air);
    REQUIRE(m.scan_fill() == asset::model_fill::mixed);

    m.set_voxel(3, 3, 3, voxels::gray[4]);
    REQUIRE(m.compact_pages() == 1);
    REQUIRE(m.scan_fill() == asset::model_fill::solid);
}

TEST_CASE("six solid neighbours leave nothing to draw", "[model]") {
    asset::model_identity_pool ids;
    asset::page_pool pages;

    auto voxels = std::make_shared<asset::model>(ids, pages, side, side, side);
    voxels->fill(voxels::gray[4]);
    asset::chunk_volume center{voxels};

    per_face<std::unique_ptr<asset::model>> neighbors;
    for (auto& n : neighbors) {
        n = solid_cube(ids, pages);
    }

    REQUIRE_FALSE(center.boundaries_are_solid());

    constexpr auto last_face = face_direction::neg_z;

    for (const face_direction face : all_face_directions) {
        if (face != last_face) {
            center.set_boundary_slice(face, *neighbors[face]);
        }
    }
    REQUIRE_FALSE(center.boundaries_are_solid());

    center.set_boundary_slice(last_face, *neighbors[last_face]);
    REQUIRE(center.boundaries_are_solid());

    neighbors[face_direction::pos_x]->set_voxel(0, 10, 10, voxels::air);
    center.set_boundary_slice(face_direction::pos_x, *neighbors[face_direction::pos_x]);
    REQUIRE_FALSE(center.boundaries_are_solid());

    neighbors[face_direction::neg_x]->set_voxel(side - 2, 10, 10, voxels::air);
    center.set_boundary_slice(face_direction::neg_x, *neighbors[face_direction::neg_x]);
    center.set_boundary_slice(face_direction::pos_x, *solid_cube(ids, pages));
    REQUIRE(center.boundaries_are_solid());

    center.release_boundary();
    REQUIRE_FALSE(center.boundaries_are_solid());
    REQUIRE_FALSE(center.has_boundary_slice(face_direction::pos_x));
}

TEST_CASE("a face plane comes out of the page table", "[model]") {
    asset::model_identity_pool ids;
    asset::page_pool pages;

    asset::model m{ids, pages, side, side, side};
    asset::face_occupancy face;

    REQUIRE(m.extract_face(face_direction::pos_x, face));
    REQUIRE(std::ranges::all_of(face.rows, [](uint64 row) -> bool { return row == 0; }));

    m.fill(voxels::gray[4]);
    REQUIRE(m.extract_face(face_direction::pos_x, face));
    REQUIRE(std::ranges::all_of(face.rows, [](uint64 row) -> bool { return row == ~uint64{0}; }));

    m.set_voxel(side - 1, 5, 9, voxels::air);
    m.set_voxel(side - 2, 7, 9, voxels::air);

    REQUIRE(m.extract_face(face_direction::pos_x, face));
    REQUIRE_FALSE(face.test(5, 9));
    REQUIRE(face.test(7, 9));

    for (const face_direction direction : all_face_directions) {
        if (direction == face_direction::pos_x) {
            continue;
        }
        INFO("face " << static_cast<int32>(direction));
        REQUIRE(m.extract_face(direction, face));
        REQUIRE(std::ranges::all_of(face.rows, [](uint64 row) -> bool {
            return row == ~uint64{0};
        }));
    }

    asset::model small{ids, pages, 32, 32, 32};
    REQUIRE_FALSE(small.extract_face(face_direction::pos_x, face));
}
