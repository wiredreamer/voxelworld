#include <catch2/catch_test_macros.hpp>

import std;

import vw.core;
import vw.asset;
import vw.ecs;
import vw.world;

using namespace vw;
using namespace vw::ecs;

namespace {

enum face : int32 {
    neg_x = 0,
    pos_x = 1,
    neg_y = 2,
    pos_y = 3,
    neg_z = 4,
    pos_z = 5,
};

auto sealed() -> asset::cell_links {
    return asset::cell_links{};
}

auto pocket_of(std::initializer_list<int32> faces) -> asset::chunk_pocket {
    asset::chunk_pocket pocket;
    for (const int32 face : faces) {
        pocket.faces[face] = ~uint64{0};
    }
    return pocket;
}

auto wide_open() -> asset::cell_links {
    asset::cell_links links;
    links.pockets.assign(1, asset::chunk_pocket::wide_open());
    return links;
}

auto joins(std::initializer_list<std::pair<int32, int32>> pairs) -> asset::cell_links {
    asset::cell_links links;
    for (const auto& [a, b] : pairs) {
        links.pockets.push_back(pocket_of({a, b}));
    }
    return links;
}

class fake_grid {
public:
    auto set(vec3i coord, asset::cell_links links) -> void {
        chunks_[coord] = links;
    }

    auto fill(int32 extent, asset::cell_links links) -> void {
        for (int32 x = -extent; x <= extent; ++x) {
            for (int32 y = -extent; y <= extent; ++y) {
                for (int32 z = -extent; z <= extent; ++z) {
                    chunks_[vec3i{x, y, z}] = links;
                }
            }
        }
    }

    [[nodiscard]] auto links_at(vec3i coord) const -> const asset::cell_links* {
        const auto it = chunks_.find(coord);
        return it == chunks_.end() ? nullptr : &it->second;
    }

private:
    std::unordered_map<vec3i, asset::cell_links> chunks_;
};

auto walk(const fake_grid& grid, vec3i origin, int32 radius) -> std::set<std::tuple<int32, int32, int32>> {
    std::set<std::tuple<int32, int32, int32>> seen;
    walk_visible_chunks(
        origin, radius, [&grid](vec3i c) { return grid.links_at(c); },
        [&seen](vec3i c) { seen.insert({c.x, c.y, c.z}); }
    );
    return seen;
}

}  // namespace

TEST_CASE("the walk starts at the viewer's own chunk", "[world][walk]") {
    fake_grid grid;
    grid.set({0, 0, 0}, sealed());

    const auto seen = walk(grid, {0, 0, 0}, 0);

    REQUIRE(seen.contains({0, 0, 0}));
    REQUIRE(seen.size() == 1);
}

TEST_CASE("empty space is walked to the radius", "[world][walk]") {
    const fake_grid grid;

    const auto seen = walk(grid, {0, 0, 0}, 1);

    REQUIRE(seen.size() == 27);
    REQUIRE(seen.contains({1, 1, 1}));
    REQUIRE(seen.contains({-1, -1, -1}));
}

TEST_CASE("solid chunks stop the walk", "[world][walk]") {
    fake_grid grid;
    grid.fill(4, wide_open());

    for (int32 y = -4; y <= 4; ++y) {
        for (int32 z = -4; z <= 4; ++z) {
            grid.set({1, y, z}, sealed());
        }
    }

    const auto seen = walk(grid, {0, 0, 0}, 2);

    REQUIRE(seen.contains({1, 0, 0}));

    REQUIRE_FALSE(seen.contains({2, 0, 0}));
}

TEST_CASE("a tunnel through rock is followed", "[world][walk]") {
    fake_grid grid;
    grid.fill(4, sealed());
    grid.set({0, 0, 0}, wide_open());

    grid.set({1, 0, 0}, joins({{neg_x, pos_x}}));
    grid.set({2, 0, 0}, wide_open());

    const auto seen = walk(grid, {0, 0, 0}, 3);

    REQUIRE(seen.contains({1, 0, 0}));
    REQUIRE(seen.contains({2, 0, 0}));

    REQUIRE_FALSE(seen.contains({1, 1, 0}));
    REQUIRE_FALSE(seen.contains({1, 0, 1}));
}

TEST_CASE("a bend in the tunnel is followed round the corner", "[world][walk]") {
    fake_grid grid;
    grid.fill(4, sealed());

    grid.set({0, 0, 0}, wide_open());
    grid.set({1, 0, 0}, joins({{neg_x, pos_y}}));
    grid.set({1, 1, 0}, joins({{neg_y, pos_x}}));

    grid.set({2, 1, 0}, joins({{neg_x, pos_x}}));

    const auto seen = walk(grid, {0, 0, 0}, 3);

    REQUIRE(seen.contains({1, 0, 0}));
    REQUIRE(seen.contains({1, 1, 0}));
    REQUIRE(seen.contains({2, 1, 0}));
    REQUIRE(seen.contains({3, 1, 0}));

    REQUIRE_FALSE(seen.contains({2, 0, 0}));
}

TEST_CASE("a cave under a closed surface is not visible from above", "[world][walk]") {
    fake_grid grid;
    grid.fill(6, wide_open());

    for (int32 x = -6; x <= 6; ++x) {
        for (int32 z = -6; z <= 6; ++z) {
            grid.set({x, 0, z}, joins({{neg_x, pos_x}, {neg_z, pos_z}}));
        }
    }

    const auto seen = walk(grid, {0, 2, 0}, 4);

    REQUIRE(seen.contains({0, 0, 0}));

    std::size_t underground = 0;
    for (const auto& [x, y, z] : seen) {
        if (y < 0) {
            ++underground;
        }
    }
    REQUIRE(underground == 0);
}

TEST_CASE("an opening in the surface reveals the cave under it", "[world][walk]") {
    fake_grid grid;
    grid.fill(6, wide_open());

    for (int32 x = -6; x <= 6; ++x) {
        for (int32 z = -6; z <= 6; ++z) {
            grid.set({x, 0, z}, joins({{neg_x, pos_x}, {neg_z, pos_z}}));
        }
    }

    grid.set({1, 0, 1}, joins({{neg_y, pos_y}}));

    const auto seen = walk(grid, {0, 2, 0}, 4);

    REQUIRE(seen.contains({1, -1, 1}));
    REQUIRE(seen.contains({0, -1, 1}));
}

TEST_CASE("the radius bounds the walk", "[world][walk]") {
    const fake_grid grid;

    const auto seen = walk(grid, {5, 5, 5}, 2);

    REQUIRE(seen.size() == 125);
    for (const auto& [x, y, z] : seen) {
        REQUIRE(std::abs(x - 5) <= 2);
        REQUIRE(std::abs(y - 5) <= 2);
        REQUIRE(std::abs(z - 5) <= 2);
    }
}
