#include <catch2/catch_test_macros.hpp>

import std;

import vw.core;
import vw.world;

using namespace vw;

namespace {

constexpr int32 side = 64;

auto solid_cube(asset::model_identity_pool& ids, asset::page_pool& pages)
    -> std::unique_ptr<asset::model> {
    auto m = std::make_unique<asset::model>(ids, pages, blocks::terrain::category, side, side, side);
    m->fill(voxel{blocks::terrain::stone[0]});
    return m;
}

}  // namespace

TEST_CASE("the page table says what a volume is made of", "[model]") {
    asset::model_identity_pool ids;
    asset::page_pool pages;

    asset::model m{ids, pages, blocks::terrain::category, side, side, side};
    REQUIRE(m.scan_fill() == asset::model_fill::air);

    m.fill(voxel{blocks::terrain::stone[0]});
    REQUIRE(m.scan_fill() == asset::model_fill::solid);

    // One voxel of air turns its page sparse, and a sparse page is a mix
    // whatever it holds -- the cheap answer is deliberately the careful one.
    m.set_voxel(3, 3, 3, empty_voxel);
    REQUIRE(m.scan_fill() == asset::model_fill::mixed);

    m.set_voxel(3, 3, 3, voxel{blocks::terrain::stone[0]});
    REQUIRE(m.compact_pages() == 1);
    REQUIRE(m.scan_fill() == asset::model_fill::solid);
}

TEST_CASE("six solid neighbours leave nothing to draw", "[model]") {
    asset::model_identity_pool ids;
    asset::page_pool pages;

    auto voxels = std::make_shared<asset::model>(ids, pages, blocks::terrain::category, side, side, side);
    voxels->fill(voxel{blocks::terrain::stone[0]});
    asset::chunk_volume center{voxels};

    std::array<std::unique_ptr<asset::model>, 6> neighbors;
    for (auto& n : neighbors) {
        n = solid_cube(ids, pages);
    }

    REQUIRE_FALSE(center.boundaries_are_solid());

    // Five sides walled in and the sixth open to the sky is a face to draw.
    for (int32 fd = 0; fd < 5; ++fd) {
        center.set_boundary_slice(fd, *neighbors[fd]);
    }
    REQUIRE_FALSE(center.boundaries_are_solid());

    center.set_boundary_slice(5, *neighbors[5]);
    REQUIRE(center.boundaries_are_solid());

    // Face 0 is +X, so the plane facing this chunk is the neighbour's own -X
    // side. A single voxel of air in it is a quad the chunk owes.
    neighbors[0]->set_voxel(0, 10, 10, empty_voxel);
    center.set_boundary_slice(0, *neighbors[0]);
    REQUIRE_FALSE(center.boundaries_are_solid());

    // Air one voxel deeper is behind the seam and hides nothing.
    neighbors[1]->set_voxel(side - 2, 10, 10, empty_voxel);
    center.set_boundary_slice(1, *neighbors[1]);
    center.set_boundary_slice(0, *solid_cube(ids, pages));
    REQUIRE(center.boundaries_are_solid());

    center.release_boundary();
    REQUIRE_FALSE(center.boundaries_are_solid());
    REQUIRE_FALSE(center.has_boundary_slice(0));
}

TEST_CASE("a face plane comes out of the page table", "[model]") {
    asset::model_identity_pool ids;
    asset::page_pool pages;

    asset::model m{ids, pages, blocks::terrain::category, side, side, side};
    asset::face_occupancy face;

    REQUIRE(m.extract_face(0, face));
    REQUIRE(std::ranges::all_of(face.rows, [](uint64 row) -> bool { return row == 0; }));

    m.fill(voxel{blocks::terrain::stone[0]});
    REQUIRE(m.extract_face(0, face));
    REQUIRE(std::ranges::all_of(face.rows, [](uint64 row) -> bool { return row == ~uint64{0}; }));

    // Face 0 is +X, the plane at x = 63, addressed (a = y, b = z). Air one
    // voxel short of it belongs to no face at all.
    m.set_voxel(side - 1, 5, 9, empty_voxel);
    m.set_voxel(side - 2, 7, 9, empty_voxel);

    REQUIRE(m.extract_face(0, face));
    REQUIRE_FALSE(face.test(5, 9));
    REQUIRE(face.test(7, 9));

    // The other five sides of the same chunk are untouched.
    for (int32 fd = 1; fd < 6; ++fd) {
        INFO("face " << fd);
        REQUIRE(m.extract_face(fd, face));
        REQUIRE(std::ranges::all_of(face.rows, [](uint64 row) -> bool {
            return row == ~uint64{0};
        }));
    }

    // Anything that is not a chunk has no neighbours to hand a plane to.
    asset::model small{ids, pages, blocks::terrain::category, 32, 32, 32};
    REQUIRE_FALSE(small.extract_face(0, face));
}
