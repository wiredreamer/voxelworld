#include <catch2/catch_test_macros.hpp>

import std;

import vw.core;
import vw.asset;

using namespace vw;

namespace {

constexpr int32 side = 64;

constexpr material amber_lamp{5};
constexpr material blue_lamp{9};

auto emission_of_lamps() -> material_levels {
    material_levels emission{};
    emission[amber_lamp.value] = 14;
    emission[blue_lamp.value]  = 9;
    return emission;
}

auto sorted(std::vector<asset::emitting_voxel> found) -> std::vector<asset::emitting_voxel> {
    std::ranges::sort(found, {}, [](const asset::emitting_voxel& at) {
        return std::tuple{at.z, at.y, at.x};
    });
    return found;
}

}  // namespace

TEST_CASE("a volume without lamps names no emitters", "[model][light]") {
    asset::model_identity_pool ids;
    asset::page_pool pages;

    asset::model m{ids, pages, side, side, side};
    m.fill(voxels::gray[8]);
    m.set_voxel(5, 5, 5, voxels::air);

    std::vector<asset::emitting_voxel> found;
    m.collect_emitters(emission_of_lamps(), found);

    REQUIRE(found.empty());
}

TEST_CASE("every lamp is named once with the level of its type", "[model][light]") {
    asset::model_identity_pool ids;
    asset::page_pool pages;

    asset::model m{ids, pages, side, side, side};
    m.set_voxel(1, 2, 3, voxels::gray[8]);
    m.set_voxel(9, 2, 3, voxels::amber[8], amber_lamp);
    m.set_voxel(63, 63, 63, voxels::blue[8], blue_lamp);
    m.set_voxel(40, 17, 8, voxels::amber[8], amber_lamp);
    m.set_voxel(41, 17, 8, voxels::gray[3]);

    std::vector<asset::emitting_voxel> found;
    m.collect_emitters(emission_of_lamps(), found);

    const std::vector<asset::emitting_voxel> expected{
        {.x = 9, .y = 2, .z = 3, .level = 14},
        {.x = 40, .y = 17, .z = 8, .level = 14},
        {.x = 63, .y = 63, .z = 63, .level = 9},
    };
    REQUIRE(sorted(found) == sorted(expected));
}

TEST_CASE("a page filled with one lamp type names all of its voxels", "[model][light]") {
    asset::model_identity_pool ids;
    asset::page_pool pages;

    asset::model m{ids, pages, side, side, side};
    for (int32 z = 8; z < 16; ++z) {
        for (int32 y = 0; y < 8; ++y) {
            for (int32 x = 16; x < 24; ++x) {
                m.set_voxel(x, y, z, voxels::blue[8], blue_lamp);
            }
        }
    }
    static_cast<void>(m.compact_pages());

    std::vector<asset::emitting_voxel> found;
    m.collect_emitters(emission_of_lamps(), found);

    REQUIRE(found.size() == 512);
    REQUIRE(std::ranges::all_of(found, [](const asset::emitting_voxel& at) {
        return at.level == 9 && at.x >= 16 && at.x < 24 && at.y < 8 && at.z >= 8 && at.z < 16;
    }));
}
