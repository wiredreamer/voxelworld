#include <catch2/catch_test_macros.hpp>

import std;

import vw.core;
import vw.asset;

using namespace vw;

namespace {

constexpr vec3i block_size{6, 5, 4};

auto make_block(asset::model_registry& registry) -> std::shared_ptr<asset::model> {
    auto m = registry.create_unnamed(block_size);
    {
        asset::model_writer writer{*m};
        writer.set(1, 1, 1, voxels::gray[4]);
        writer.set(2, 1, 1, voxels::gray[7]);
        writer.set(2, 2, 1, voxels::gray[2]);
        writer.set(5, 4, 3, voxels::gray[9]);
    }
    m->set_pivot(vec3f{3.f, 2.5f, 2.f});
    return m;
}

auto solid_count(const asset::model& m) -> int32 {
    int32 count     = 0;
    const auto size = m.size();
    for (int32 x = 0; x < size.x; ++x) {
        for (int32 y = 0; y < size.y; ++y) {
            for (int32 z = 0; z < size.z; ++z) {
                count += m.is_empty(x, y, z) ? 0 : 1;
            }
        }
    }
    return count;
}

}  // namespace

TEST_CASE("a position is inside a volume only within its size", "[rewrite]") {
    CHECK(asset::contains(block_size, vec3i{0, 0, 0}));
    CHECK(asset::contains(block_size, vec3i{5, 4, 3}));
    CHECK_FALSE(asset::contains(block_size, vec3i{6, 0, 0}));
    CHECK_FALSE(asset::contains(block_size, vec3i{0, 5, 0}));
    CHECK_FALSE(asset::contains(block_size, vec3i{0, 0, 4}));
    CHECK_FALSE(asset::contains(block_size, vec3i{-1, 0, 0}));
}

TEST_CASE("edits set, repaint and erase voxels in a new volume", "[rewrite]") {
    asset::model_registry registry;
    const auto source = make_block(registry);

    const std::array edits{
        asset::voxel_edit{.position = {0, 0, 0}, .value = voxels::gray[1]},
        asset::voxel_edit{.position = {1, 1, 1}, .value = voxels::gray[8]},
        asset::voxel_edit{.position = {2, 2, 1}, .value = voxel{}},
    };

    const auto result = asset::edited(*source, edits, registry);

    REQUIRE(result != nullptr);
    CHECK(result->size() == block_size);
    CHECK(result->pivot() == source->pivot());
    CHECK(result->get_voxel(0, 0, 0) == voxels::gray[1]);
    CHECK(result->get_voxel(1, 1, 1) == voxels::gray[8]);
    CHECK(result->is_empty(2, 2, 1));
    CHECK(result->get_voxel(2, 1, 1) == voxels::gray[7]);
    CHECK(result->get_voxel(5, 4, 3) == voxels::gray[9]);
    CHECK(solid_count(*result) == 4);
}

TEST_CASE("edits leave the source volume as it was", "[rewrite]") {
    asset::model_registry registry;
    const auto source = make_block(registry);

    const std::array edits{asset::voxel_edit{.position = {1, 1, 1}, .value = voxel{}}};
    const auto result = asset::edited(*source, edits, registry);

    CHECK(result.get() != source.get());
    CHECK(source->get_voxel(1, 1, 1) == voxels::gray[4]);
    CHECK(solid_count(*source) == 4);
}

TEST_CASE("a later edit of one cell wins over an earlier one", "[rewrite]") {
    asset::model_registry registry;
    const auto source = make_block(registry);

    const std::array edits{
        asset::voxel_edit{.position = {3, 3, 3}, .value = voxels::gray[1]},
        asset::voxel_edit{.position = {3, 3, 3}, .value = voxels::gray[5]},
    };

    const auto result = asset::edited(*source, edits, registry);

    CHECK(result->get_voxel(3, 3, 3) == voxels::gray[5]);
}

TEST_CASE("an edit outside the volume is dropped", "[rewrite]") {
    asset::model_registry registry;
    const auto source = make_block(registry);

    const std::array edits{
        asset::voxel_edit{.position = {6, 0, 0}, .value = voxels::gray[1]},
        asset::voxel_edit{.position = {0, -1, 0}, .value = voxels::gray[1]},
        asset::voxel_edit{.position = {0, 0, 40}, .value = voxels::gray[1]},
    };

    const auto result = asset::edited(*source, edits, registry);

    CHECK(solid_count(*result) == 4);
}

TEST_CASE("growing at the low side shifts the voxels and the pivot with them", "[rewrite]") {
    asset::model_registry registry;
    const auto source = make_block(registry);

    const auto result = asset::resized(*source, vec3i{2, 0, 1}, vec3i{0, 3, 0}, registry);

    REQUIRE(result != nullptr);
    CHECK(result->size() == vec3i{8, 8, 5});
    CHECK(result->pivot() == vec3f{5.f, 2.5f, 3.f});
    CHECK(result->get_voxel(3, 1, 2) == voxels::gray[4]);
    CHECK(result->get_voxel(4, 1, 2) == voxels::gray[7]);
    CHECK(result->get_voxel(4, 2, 2) == voxels::gray[2]);
    CHECK(result->get_voxel(7, 4, 4) == voxels::gray[9]);
    CHECK(solid_count(*result) == 4);
}

TEST_CASE("growing at the high side leaves every voxel where it was", "[rewrite]") {
    asset::model_registry registry;
    const auto source = make_block(registry);

    const auto result = asset::resized(*source, vec3i{0, 0, 0}, vec3i{4, 4, 4}, registry);

    REQUIRE(result != nullptr);
    CHECK(result->size() == vec3i{10, 9, 8});
    CHECK(result->pivot() == source->pivot());
    CHECK(result->get_voxel(1, 1, 1) == voxels::gray[4]);
    CHECK(result->get_voxel(5, 4, 3) == voxels::gray[9]);
    CHECK(solid_count(*result) == 4);
}

TEST_CASE("shrinking cuts off the voxels that no longer fit", "[rewrite]") {
    asset::model_registry registry;
    const auto source = make_block(registry);

    const auto result = asset::resized(*source, vec3i{-1, -1, -1}, vec3i{-2, -1, -1}, registry);

    REQUIRE(result != nullptr);
    CHECK(result->size() == vec3i{3, 3, 2});
    CHECK(result->pivot() == vec3f{2.f, 1.5f, 1.f});
    CHECK(result->get_voxel(0, 0, 0) == voxels::gray[4]);
    CHECK(result->get_voxel(1, 0, 0) == voxels::gray[7]);
    CHECK(result->get_voxel(1, 1, 0) == voxels::gray[2]);
    CHECK(solid_count(*result) == 3);
}

TEST_CASE("a volume cannot be resized to nothing", "[rewrite]") {
    asset::model_registry registry;
    const auto source = make_block(registry);

    CHECK(asset::resized(*source, vec3i{-6, 0, 0}, vec3i{0, 0, 0}, registry) == nullptr);
    CHECK(asset::resized(*source, vec3i{0, -2, 0}, vec3i{0, -3, 0}, registry) == nullptr);
    CHECK(asset::resized(*source, vec3i{0, 0, -5}, vec3i{0, 0, 0}, registry) == nullptr);
}

TEST_CASE("resizing by nothing gives an equal volume", "[rewrite]") {
    asset::model_registry registry;
    const auto source = make_block(registry);

    const auto result = asset::resized(*source, vec3i{0, 0, 0}, vec3i{0, 0, 0}, registry);

    REQUIRE(result != nullptr);
    CHECK(result->size() == block_size);
    CHECK(result->pivot() == source->pivot());
    CHECK(solid_count(*result) == 4);
}
