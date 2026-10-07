#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

import std;
import vw.core;

using namespace vw;
using Catch::Approx;
using spatial::occupancy_clipmap_layout;

namespace {

class cell_world {
public:
    auto fill(vec3i voxel) -> void {
        solid_.insert(key_of(voxel));
    }

    auto know_only(int32 level) -> void {
        only_level_ = level;
    }

    auto forget(vec3i chunk) -> void {
        unknown_.insert(key_of(chunk));
    }

    [[nodiscard]] auto brick_at(vec3i voxel, int32 level) const -> std::optional<uint8> {
        if (unknown_.contains(key_of(spatial::occupancy_chunk_of(voxel)))) {
            return std::nullopt;
        }
        if (only_level_ >= 0 && level != only_level_) {
            return std::nullopt;
        }

        const vec3i cell{voxel.x >> level, voxel.y >> level, voxel.z >> level};
        const vec3i brick{cell.x >> 1, cell.y >> 1, cell.z >> 1};

        uint8 bits = 0;
        for (int32 corner = 0; corner < 8; ++corner) {
            const vec3i member{
                (brick.x * 2) + (corner & 1), (brick.y * 2) + ((corner >> 1) & 1),
                (brick.z * 2) + ((corner >> 2) & 1)
            };
            if (cell_holds_(member, level)) {
                bits |= static_cast<uint8>(1U << spatial::occupancy_brick_bit(member));
            }
        }
        return bits;
    }

private:
    [[nodiscard]] static auto key_of(vec3i at) -> uint64 {
        const auto part = [](int32 v) { return static_cast<uint64>(v + 100000) & 0x1FFFFF; };
        return part(at.x) | (part(at.y) << 21) | (part(at.z) << 42);
    }

    [[nodiscard]] auto cell_holds_(vec3i cell, int32 level) const -> bool {
        const int32 span = 1 << level;
        for (int32 z = 0; z < span; ++z) {
            for (int32 y = 0; y < span; ++y) {
                for (int32 x = 0; x < span; ++x) {
                    const vec3i voxel{
                        (cell.x * span) + x, (cell.y * span) + y, (cell.z * span) + z
                    };
                    if (solid_.contains(key_of(voxel))) {
                        return true;
                    }
                }
            }
        }
        return false;
    }

    std::unordered_set<uint64> solid_;
    std::unordered_set<uint64> unknown_;
    int32 only_level_ = -1;
};

auto march(const cell_world& cells, vec3f origin, vec3f direction, float32 reach = 512.0F) {
    return spatial::march_occupancy(
        vec3i{0, 0, 0}, origin, direction, reach, 0,
        [&](vec3i voxel, int32 level) { return cells.brick_at(voxel, level); }
    );
}

}  // namespace

TEST_CASE("the slots of a window tile the texture without overlap", "[occupancy]") {
    for (int32 level = 0; level < occupancy_clipmap_layout::level_count; ++level) {
        const int32 side   = occupancy_clipmap_layout::window_chunks(level);
        const vec3i origin = spatial::occupancy_window_origin({-1000, 37, 4000}, level);

        std::vector<bool> taken(static_cast<std::size_t>(side * side * side), false);
        for (int32 z = 0; z < side; ++z) {
            for (int32 y = 0; y < side; ++y) {
                for (int32 x = 0; x < side; ++x) {
                    const vec3i chunk{origin.x + x, origin.y + y, origin.z + z};
                    REQUIRE(spatial::occupancy_window_holds(chunk, origin, level));

                    const auto index = static_cast<std::size_t>(spatial::occupancy_slot_index(
                        spatial::occupancy_slot_of(chunk, level), level
                    ));
                    REQUIRE_FALSE(taken[index]);
                    taken[index] = true;
                }
            }
        }
    }
}

TEST_CASE("the window keeps the centre at least three and a half chunks inside", "[occupancy]") {
    for (const int32 at : {-130, -64, -33, -32, -1, 0, 31, 32, 63, 64, 500}) {
        const vec3i origin = spatial::occupancy_window_origin({at, at, at}, 0);
        const int32 low    = origin.x * occupancy_clipmap_layout::chunk_voxels;
        const int32 high   = low + (occupancy_clipmap_layout::window_chunks(0) *
                                    occupancy_clipmap_layout::chunk_voxels);

        CHECK(at - low >= 224);
        CHECK(high - at > 224);
    }
}

TEST_CASE("a ray stops at the first solid voxel on its way", "[occupancy]") {
    cell_world cells;
    cells.fill({20, 5, 5});
    cells.fill({30, 5, 5});

    const auto hit = march(cells, {2.5F, 5.5F, 5.5F}, {1.0F, 0.0F, 0.0F});

    REQUIRE(hit);
    CHECK(hit->voxel == vec3i{20, 5, 5});
    CHECK(hit->entry_axis == 0);
    CHECK(hit->distance == Approx(17.5F).margin(0.01));
}

TEST_CASE("a diagonal ray is stopped by four voxels around one edge", "[occupancy]") {
    cell_world cells;
    cells.fill({10, 0, 11});
    cells.fill({11, 0, 10});
    cells.fill({11, 0, 11});
    cells.fill({10, 0, 10});

    const float32 inv = 1.0F / std::sqrt(2.0F);
    const auto hit    = march(cells, {0.5F, 0.5F, 0.5F}, {inv, 0.0F, inv});

    REQUIRE(hit);
    CHECK(hit->voxel == vec3i{10, 0, 10});
}

TEST_CASE("skipping an empty brick never skips a solid neighbour", "[occupancy]") {
    std::mt19937 rng{7};
    std::uniform_int_distribution<int32> place{-40, 40};
    std::uniform_real_distribution<float32> turn{-1.0F, 1.0F};

    cell_world cells;
    std::vector<vec3i> solids;
    for (int32 i = 0; i < 400; ++i) {
        const vec3i voxel{place(rng), place(rng), place(rng)};
        if (std::abs(voxel.x) < 3 && std::abs(voxel.y) < 3 && std::abs(voxel.z) < 3) {
            continue;
        }
        cells.fill(voxel);
        solids.push_back(voxel);
    }

    const vec3f origin{0.37F, 0.61F, 0.43F};

    for (int32 i = 0; i < 300; ++i) {
        vec3f direction{turn(rng), turn(rng), turn(rng)};
        const float32 length = std::sqrt(
            (direction.x * direction.x) + (direction.y * direction.y) +
            (direction.z * direction.z)
        );
        if (length < 0.1F) {
            continue;
        }
        direction = {direction.x / length, direction.y / length, direction.z / length};

        float32 nearest = 1.0e30F;
        for (const vec3i voxel : solids) {
            float32 enters = 0.0F;
            float32 leaves = 1.0e30F;
            for (std::size_t axis = 0; axis < 3; ++axis) {
                const float32 low  = static_cast<float32>(voxel[axis]) - origin[axis];
                const float32 high = low + 1.0F;
                if (direction[axis] == 0.0F) {
                    if (low > 0.0F || high < 0.0F) {
                        enters = 1.0e30F;
                    }
                    continue;
                }
                const float32 a = low / direction[axis];
                const float32 b = high / direction[axis];
                enters = std::max(enters, std::min(a, b));
                leaves = std::min(leaves, std::max(a, b));
            }
            if (enters <= leaves && enters < nearest) {
                nearest = enters;
            }
        }

        const auto hit = march(cells, origin, direction, 60.0F);

        if (nearest < 59.0F) {
            REQUIRE(hit);
            CHECK(hit->distance == Approx(nearest).margin(0.02));
        } else if (nearest > 61.0F) {
            CHECK_FALSE(hit);
        }
    }
}

TEST_CASE("open air is crossed in strides of the coarsest empty brick", "[occupancy]") {
    cell_world cells;
    cells.fill({400, 5, 5});

    const auto hit = march(cells, {2.5F, 5.5F, 5.5F}, {1.0F, 0.0F, 0.0F}, 600.0F);

    REQUIRE(hit);
    CHECK(hit->voxel == vec3i{400, 5, 5});
    CHECK(hit->level == 0);
    CHECK(hit->steps < 60);
}

TEST_CASE("a coarse level answers with its own cell size", "[occupancy]") {
    cell_world cells;
    cells.know_only(2);
    cells.fill({41, 1, 1});

    const auto hit = march(cells, {2.5F, 1.5F, 1.5F}, {1.0F, 0.0F, 0.0F});

    REQUIRE(hit);
    CHECK(hit->level == 2);
    CHECK(hit->distance == Approx(37.5F).margin(0.01));
}

TEST_CASE("a ray told to start at a coarse level never reads a finer one", "[occupancy]") {
    cell_world cells;
    cells.fill({41, 1, 1});

    int32 finest_read = 99;
    const auto hit    = spatial::march_occupancy(
        vec3i{0, 0, 0}, vec3f{2.5F, 1.5F, 1.5F}, vec3f{1.0F, 0.0F, 0.0F}, 512.0F, 2,
        [&](vec3i voxel, int32 level) {
            finest_read = std::min(finest_read, level);
            return cells.brick_at(voxel, level);
        }
    );

    REQUIRE(hit);
    CHECK(finest_read == 2);
    CHECK(hit->level == 2);
    CHECK(hit->distance == Approx(37.5F).margin(0.01));
}

TEST_CASE("a chunk nobody knows is crossed as empty", "[occupancy]") {
    cell_world cells;
    cells.fill({70, 5, 5});
    cells.fill({140, 5, 5});
    cells.forget({1, 0, 0});

    const auto hit = march(cells, {2.5F, 5.5F, 5.5F}, {1.0F, 0.0F, 0.0F});

    REQUIRE(hit);
    CHECK(hit->voxel == vec3i{140, 5, 5});
}
