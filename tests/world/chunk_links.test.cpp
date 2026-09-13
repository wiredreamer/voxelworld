#include <catch2/catch_test_macros.hpp>

import std;

import vw.core;
import vw.asset;
import vw.ecs;
import vw.world;

using namespace vw;
using namespace vw::ecs;

namespace {

constexpr int32 side = asset::chunk_occupancy::side;

enum face : int32 {
    neg_x = 0,
    pos_x = 1,
    neg_y = 2,
    pos_y = 3,
    neg_z = 4,
    pos_z = 5,
};

// Occupancy is what the mesher reads, so the tests build it directly rather
// than going through a model: the shapes here are easier to state as bits.
auto solid_chunk() -> asset::chunk_occupancy {
    asset::chunk_occupancy occupancy;
    for (int32 y = 0; y < side; ++y) {
        for (int32 z = 0; z < side; ++z) {
            occupancy.set_row(y, z, ~uint64{0});
            occupancy.set_zrow(y, z, ~uint64{0});
        }
    }
    return occupancy;
}

auto clear_voxel(asset::chunk_occupancy& occupancy, int32 x, int32 y, int32 z) -> void {
    occupancy.rows[(y * side) + z] &= ~(uint64{1} << x);
    occupancy.zrows[(y * side) + x] &= ~(uint64{1} << z);
}

// A straight bore of the given radius along one axis, through the middle.
auto bore(asset::chunk_occupancy& occupancy, int32 axis, int32 radius) -> void {
    constexpr int32 mid = side / 2;

    for (int32 t = 0; t < side; ++t) {
        for (int32 a = -radius; a <= radius; ++a) {
            for (int32 b = -radius; b <= radius; ++b) {
                if (axis == 0) {
                    clear_voxel(occupancy, t, mid + a, mid + b);
                } else if (axis == 1) {
                    clear_voxel(occupancy, mid + a, t, mid + b);
                } else {
                    clear_voxel(occupancy, mid + a, mid + b, t);
                }
            }
        }
    }
}

// The tests build shapes that run the whole width of the chunk, so the cell
// they check is a corner one: its outward faces are the chunk's own.
auto corner_cell(const asset::chunk_links& links) -> const asset::cell_links& {
    return links.cells[asset::chunk_links::cell_index(0, 0, 0)];
}

// Which faces the cell is open on at all, across every pocket.
auto open_faces(const asset::cell_links& links) -> uint8 {
    uint8 faces = 0;
    for (const auto& pocket : links.pockets) {
        for (int32 face = 0; face < asset::chunk_pocket::face_count; ++face) {
            if (pocket.touches(face)) {
                faces |= static_cast<uint8>(1U << face);
            }
        }
    }
    return faces;
}

// True when one single pocket reaches both faces: sight can cross the chunk.
auto connects(const asset::cell_links& links, int32 a, int32 b) -> bool {
    return std::ranges::any_of(links.pockets, [a, b](const asset::chunk_pocket& pocket) -> bool {
        return pocket.touches(a) && pocket.touches(b);
    });
}

}  // namespace

TEST_CASE("solid rock connects nothing", "[world][links]") {
    const auto links = asset::build_chunk_links(solid_chunk());

    REQUIRE(links.is_sealed());
}

TEST_CASE("empty space is one pocket open on every face", "[world][links]") {
    const asset::chunk_occupancy empty;
    const auto links = asset::build_chunk_links(empty);

    for (const auto& cell : links.cells) {
        REQUIRE(cell.pockets.size() == 1);
        REQUIRE(open_faces(cell) == 0b111111);
        for (int32 face = 0; face < asset::chunk_pocket::face_count; ++face) {
            REQUIRE(cell.pockets[0].faces[face] == ~uint64{0});
        }
    }
}

TEST_CASE("a bore joins the two faces it runs between", "[world][links]") {
    struct probe {
        int32 axis;
        int32 low;
        int32 high;
    };

    for (const auto [axis, low, high] : {
             probe{0, neg_x, pos_x},
             probe{1, neg_y, pos_y},
             probe{2, neg_z, pos_z},
         }) {
        auto occupancy = solid_chunk();
        bore(occupancy, axis, 1);

        const auto links = asset::build_chunk_links(occupancy);

        // The bore runs through the middle of the chunk, so it passes through
        // the cells on the far side of each axis from the corner.
        const int32 far = asset::chunk_links::cells_per_side - 1;
        const auto& cell = links.cells[
            axis == 0   ? asset::chunk_links::cell_index(0, far, far)
            : axis == 1 ? asset::chunk_links::cell_index(far, 0, far)
                        : asset::chunk_links::cell_index(far, far, 0)];

        INFO("axis " << axis << " pockets " << cell.pockets.size());
        REQUIRE(cell.pockets.size() == 1);
        REQUIRE(connects(cell, low, high));

        // A patch of the face, not the whole of it.
        REQUIRE(std::popcount(cell.pockets[0].faces[low]) <= 4);
    }
}

TEST_CASE("crossing bores connect all four faces they reach", "[world][links]") {
    auto occupancy = solid_chunk();
    bore(occupancy, 0, 1);
    bore(occupancy, 2, 1);

    const auto links = asset::build_chunk_links(occupancy);

    // Both bores meet in the middle of the chunk, which is the corner where
    // all eight cells touch: that cell sees them as one pocket crossing it in
    // two directions, and nothing in it reaches a Y face.
    const int32 far  = asset::chunk_links::cells_per_side - 1;
    const auto& cell = links.cells[asset::chunk_links::cell_index(far, far, far)];

    REQUIRE(cell.pockets.size() == 1);
    REQUIRE(connects(cell, neg_x, neg_z));
    REQUIRE_FALSE(connects(cell, neg_y, pos_y));
    REQUIRE_FALSE(connects(cell, neg_x, pos_y));
}

TEST_CASE("bores that miss each other stay separate", "[world][links]") {
    auto occupancy = solid_chunk();

    // One along X near the bottom, one along Z near the top: they cross in
    // plan but not in space. Both are kept clear of the boundaries between
    // cells, where a bore would touch the face it runs along.
    for (int32 x = 0; x < side; ++x) {
        clear_voxel(occupancy, x, 10, 20);
    }
    for (int32 z = 0; z < side; ++z) {
        clear_voxel(occupancy, 44, 50, z);
    }

    const auto links = asset::build_chunk_links(occupancy);

    // The two bores run through different cells, and neither cell has a pocket
    // reaching both an X and a Z face.
    for (const auto& cell : links.cells) {
        REQUIRE_FALSE(connects(cell, neg_x, pos_z));
        REQUIRE_FALSE(connects(cell, neg_z, pos_x));
    }
}

TEST_CASE("a sealed bubble connects nothing", "[world][links]") {
    auto occupancy = solid_chunk();

    for (int32 x = 20; x < 30; ++x) {
        for (int32 y = 20; y < 30; ++y) {
            for (int32 z = 20; z < 30; ++z) {
                clear_voxel(occupancy, x, y, z);
            }
        }
    }

    const auto links = asset::build_chunk_links(occupancy);

    REQUIRE(links.is_sealed());
}

TEST_CASE("a pocket that only opens on one face crosses nothing", "[world][links]") {
    auto occupancy = solid_chunk();

    // Reaches the -X face and stops well short of every other one.
    for (int32 x = 0; x < 20; ++x) {
        clear_voxel(occupancy, x, 20, 20);
    }

    const auto links = asset::build_chunk_links(occupancy);

    // It starts on the -X face of the chunk and dies inside the first cell, so
    // that cell is open on -X and nothing else, and no other cell is open at
    // all.
    const auto& cell = corner_cell(links);
    REQUIRE(cell.pockets.size() == 1);
    REQUIRE(open_faces(cell) == (1U << neg_x));

    for (int32 i = 1; i < asset::chunk_links::cell_count; ++i) {
        REQUIRE(links.cells[i].is_sealed());
    }
}

TEST_CASE("openings are placed on the face, not just counted", "[world][links]") {
    auto occupancy = solid_chunk();

    // Two bores along X, far apart on the face: one near a corner, one near the
    // opposite one.
    for (int32 x = 0; x < side; ++x) {
        clear_voxel(occupancy, x, 4, 4);
        clear_voxel(occupancy, x, 60, 60);
    }

    const auto links = asset::build_chunk_links(occupancy);

    // The two bores land in different cells of the -X face of the chunk.
    const int32 far   = asset::chunk_links::cells_per_side - 1;
    const auto& lower = links.cells[asset::chunk_links::cell_index(0, 0, 0)];
    const auto& upper = links.cells[asset::chunk_links::cell_index(0, far, far)];

    REQUIRE(lower.pockets.size() == 1);
    REQUIRE(upper.pockets.size() == 1);

    const uint64 first  = lower.pockets[0].faces[neg_x];
    const uint64 second = upper.pockets[0].faces[neg_x];

    REQUIRE(std::popcount(first) == 1);
    REQUIRE(std::popcount(second) == 1);

    // A cell on the -X side whose +X face opens only where the first bore does
    // meets that pocket and not the other.
    asset::chunk_pocket neighbour;
    neighbour.faces[pos_x] = first;

    REQUIRE(lower.pockets[0].meets(neighbour, neg_x));
    REQUIRE_FALSE(upper.pockets[0].meets(neighbour, neg_x));
}
