#include <catch2/catch_test_macros.hpp>

import std;

import vw.core;
import vw.asset;

using namespace vw;

namespace {

constexpr int32 side = 8;

auto fill_block(asset::model& m, vec3i from, vec3i to) -> void {
    asset::model_writer writer{m};
    for (int32 x = from.x; x <= to.x; ++x) {
        for (int32 y = from.y; y <= to.y; ++y) {
            for (int32 z = from.z; z <= to.z; ++z) {
                writer.set(x, y, z, voxels::gray[4]);
            }
        }
    }
}

}  // namespace

TEST_CASE("occupied bounds cover the voxels and nothing else", "[trim]") {
    asset::model_registry registry;
    auto m = registry.create_unnamed(vec3i{side, side, side});
    fill_block(*m, {2, 3, 1}, {4, 3, 5});

    const auto bounds = asset::occupied_bounds(*m);

    REQUIRE(bounds.has_value());
    REQUIRE(bounds->min == vec3i{2, 3, 1});
    REQUIRE(bounds->max == vec3i{4, 3, 5});
    REQUIRE(bounds->size() == vec3i{3, 1, 5});
}

TEST_CASE("an empty model has no occupied bounds", "[trim]") {
    asset::model_registry registry;
    auto m = registry.create_unnamed(vec3i{side, side, side});

    REQUIRE(!asset::occupied_bounds(*m).has_value());
}

TEST_CASE("trim cuts the empty rim down to the voxels", "[trim]") {
    asset::model_registry registry;
    auto m = registry.create_unnamed(vec3i{side, side, side});
    fill_block(*m, {2, 2, 2}, {4, 4, 4});

    const auto cut = asset::trimmed(*m, registry);

    REQUIRE(cut != nullptr);
    REQUIRE(cut->size() == vec3i{3, 3, 3});
    REQUIRE(cut->get_voxel(0, 0, 0) == voxels::gray[4]);
    REQUIRE(cut->get_voxel(2, 2, 2) == voxels::gray[4]);
}

TEST_CASE("trim moves the pivot by what it cut", "[trim]") {
    asset::model_registry registry;
    auto m = registry.create_unnamed(vec3i{side, side, side});
    fill_block(*m, {2, 2, 2}, {4, 4, 4});
    m->set_pivot(vec3f{3.5f, 2.f, 4.f});

    const auto cut = asset::trimmed(*m, registry);

    REQUIRE(cut != nullptr);
    REQUIRE(cut->pivot() == vec3f{1.5f, 0.f, 2.f});
}

TEST_CASE("trim keeps the voxels of the source", "[trim]") {
    asset::model_registry registry;
    auto m = registry.create_unnamed(vec3i{side, side, side});
    {
        asset::model_writer writer{*m};
        writer.set(1, 1, 1, voxels::gray[7]);
    }

    const auto cut = asset::trimmed(*m, registry);

    REQUIRE(cut != nullptr);
    REQUIRE(cut->get_voxel(0, 0, 0) == voxels::gray[7]);
    REQUIRE(cut->size() == vec3i{1, 1, 1});
}

TEST_CASE("trim refuses a model that already fits its voxels", "[trim]") {
    asset::model_registry registry;
    auto m = registry.create_unnamed(vec3i{side, side, side});
    fill_block(*m, {0, 0, 0}, {side - 1, side - 1, side - 1});

    REQUIRE(asset::trimmed(*m, registry) == nullptr);
}

TEST_CASE("trim refuses an empty model", "[trim]") {
    asset::model_registry registry;
    auto m = registry.create_unnamed(vec3i{side, side, side});

    REQUIRE(asset::trimmed(*m, registry) == nullptr);
}
