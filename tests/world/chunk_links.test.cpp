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

auto corner_cell(const asset::chunk_links& links) -> const asset::cell_links& {
    return links.cells[asset::chunk_links::cell_index(0, 0, 0)];
}

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

        const int32 far = asset::chunk_links::cells_per_side - 1;
        const auto& cell = links.cells[
            axis == 0   ? asset::chunk_links::cell_index(0, far, far)
            : axis == 1 ? asset::chunk_links::cell_index(far, 0, far)
                        : asset::chunk_links::cell_index(far, far, 0)];

        INFO("axis " << axis << " pockets " << cell.pockets.size());
        REQUIRE(cell.pockets.size() == 1);
        REQUIRE(connects(cell, low, high));

        REQUIRE(std::popcount(cell.pockets[0].faces[low]) <= 4);
    }
}

TEST_CASE("crossing bores connect all four faces they reach", "[world][links]") {
    auto occupancy = solid_chunk();
    bore(occupancy, 0, 1);
    bore(occupancy, 2, 1);

    const auto links = asset::build_chunk_links(occupancy);

    const int32 far  = asset::chunk_links::cells_per_side - 1;
    const auto& cell = links.cells[asset::chunk_links::cell_index(far, far, far)];

    REQUIRE(cell.pockets.size() == 1);
    REQUIRE(connects(cell, neg_x, neg_z));
    REQUIRE_FALSE(connects(cell, neg_y, pos_y));
    REQUIRE_FALSE(connects(cell, neg_x, pos_y));
}

TEST_CASE("bores that miss each other stay separate", "[world][links]") {
    auto occupancy = solid_chunk();

    for (int32 x = 0; x < side; ++x) {
        clear_voxel(occupancy, x, 10, 20);
    }
    for (int32 z = 0; z < side; ++z) {
        clear_voxel(occupancy, 44, 50, z);
    }

    const auto links = asset::build_chunk_links(occupancy);

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

    for (int32 x = 0; x < 20; ++x) {
        clear_voxel(occupancy, x, 20, 20);
    }

    const auto links = asset::build_chunk_links(occupancy);

    const auto& cell = corner_cell(links);
    REQUIRE(cell.pockets.size() == 1);
    REQUIRE(open_faces(cell) == (1U << neg_x));

    for (int32 i = 1; i < asset::chunk_links::cell_count; ++i) {
        REQUIRE(links.cells[i].is_sealed());
    }
}

TEST_CASE("openings are placed on the face, not just counted", "[world][links]") {
    auto occupancy = solid_chunk();

    for (int32 x = 0; x < side; ++x) {
        clear_voxel(occupancy, x, 4, 4);
        clear_voxel(occupancy, x, 60, 60);
    }

    const auto links = asset::build_chunk_links(occupancy);

    const int32 far   = asset::chunk_links::cells_per_side - 1;
    const auto& lower = links.cells[asset::chunk_links::cell_index(0, 0, 0)];
    const auto& upper = links.cells[asset::chunk_links::cell_index(0, far, far)];

    REQUIRE(lower.pockets.size() == 1);
    REQUIRE(upper.pockets.size() == 1);

    const uint64 first  = lower.pockets[0].faces[neg_x];
    const uint64 second = upper.pockets[0].faces[neg_x];

    REQUIRE(std::popcount(first) == 1);
    REQUIRE(std::popcount(second) == 1);

    asset::chunk_pocket neighbour;
    neighbour.faces[pos_x] = first;

    REQUIRE(lower.pockets[0].meets(neighbour, neg_x));
    REQUIRE_FALSE(upper.pockets[0].meets(neighbour, neg_x));
}
