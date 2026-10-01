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

TEST_CASE("a region outside the volume clamps to nothing", "[clip]") {
    const auto outside = asset::voxel_bounds{.min = {6, 0, 0}, .max = {8, 2, 2}};
    const auto across  = asset::voxel_bounds{.min = {-2, 3, 1}, .max = {1, 9, 1}};

    REQUIRE(!asset::clamped(outside, block_size).has_value());

    const auto inside = asset::clamped(across, block_size);
    REQUIRE(inside.has_value());
    REQUIRE(inside->min == vec3i{0, 3, 1});
    REQUIRE(inside->max == vec3i{1, 4, 1});
}

TEST_CASE("a copy holds the voxels of its region and the air between them", "[clip]") {
    asset::model_registry registry;
    const auto source = make_block(registry);

    const auto clip = asset::copied(*source, {.min = {1, 1, 1}, .max = {2, 2, 1}});

    REQUIRE(clip.size == vec3i{2, 2, 1});
    REQUIRE(clip.at({0, 0, 0}) == voxels::gray[4]);
    REQUIRE(clip.at({1, 0, 0}) == voxels::gray[7]);
    REQUIRE(clip.at({1, 1, 0}) == voxels::gray[2]);
    REQUIRE(clip.at({0, 1, 0}).is_empty());
}

TEST_CASE("a copy remembers its corner relative to the pivot", "[clip]") {
    asset::model_registry registry;
    const auto source = make_block(registry);

    const auto clip = asset::copied(*source, {.min = {1, 1, 1}, .max = {2, 2, 1}});

    REQUIRE(clip.corner_from_pivot == vec3f{-2.f, -1.5f, -1.f});
}

TEST_CASE("a copy of a region outside the volume is empty", "[clip]") {
    asset::model_registry registry;
    const auto source = make_block(registry);

    const auto clip = asset::copied(*source, {.min = {9, 9, 9}, .max = {10, 10, 10}});

    REQUIRE(clip.empty());
    REQUIRE(!asset::occupied_bounds(clip).has_value());
}

TEST_CASE("erasing clears the region and keeps the rest", "[clip]") {
    asset::model_registry registry;
    const auto source = make_block(registry);

    const auto cleared = asset::erased(*source, {.min = {1, 1, 1}, .max = {2, 1, 1}}, registry);

    REQUIRE(cleared->size() == block_size);
    REQUIRE(cleared->pivot() == source->pivot());
    REQUIRE(cleared->is_empty(1, 1, 1));
    REQUIRE(cleared->is_empty(2, 1, 1));
    REQUIRE(cleared->get_voxel(2, 2, 1) == voxels::gray[2]);
    REQUIRE(cleared->get_voxel(5, 4, 3) == voxels::gray[9]);
    REQUIRE(solid_count(*source) == 4);
}

TEST_CASE("filling every cell turns the region into a solid block", "[clip]") {
    asset::model_registry registry;
    const auto source = make_block(registry);

    const auto block = asset::filled(
        *source, {.min = {1, 1, 1}, .max = {2, 2, 2}}, voxels::gray[1],
        asset::fill_scope::every_cell, registry
    );

    REQUIRE(block->size() == block_size);
    REQUIRE(block->pivot() == source->pivot());
    REQUIRE(block->get_voxel(1, 1, 1) == voxels::gray[1]);
    REQUIRE(block->get_voxel(1, 2, 2) == voxels::gray[1]);
    REQUIRE(block->get_voxel(2, 2, 1) == voxels::gray[1]);
    REQUIRE(block->get_voxel(5, 4, 3) == voxels::gray[9]);
    REQUIRE(solid_count(*block) == 9);
    REQUIRE(solid_count(*source) == 4);
}

TEST_CASE("filling only the solid cells recolours the shape and keeps its air", "[clip]") {
    asset::model_registry registry;
    const auto source = make_block(registry);

    const auto painted = asset::filled(
        *source, {.min = {1, 1, 1}, .max = {2, 2, 2}}, voxels::gray[1],
        asset::fill_scope::solid_only, registry
    );

    REQUIRE(painted->get_voxel(1, 1, 1) == voxels::gray[1]);
    REQUIRE(painted->get_voxel(2, 1, 1) == voxels::gray[1]);
    REQUIRE(painted->get_voxel(2, 2, 1) == voxels::gray[1]);
    REQUIRE(painted->is_empty(1, 2, 1));
    REQUIRE(painted->get_voxel(5, 4, 3) == voxels::gray[9]);
    REQUIRE(solid_count(*painted) == 4);
}

TEST_CASE("a fill reaching past the volume stops at its edge", "[clip]") {
    asset::model_registry registry;
    const auto source = make_block(registry);

    const auto block = asset::filled(
        *source, {.min = {4, 3, 2}, .max = {9, 9, 9}}, voxels::gray[1],
        asset::fill_scope::every_cell, registry
    );

    REQUIRE(block->size() == block_size);
    REQUIRE(block->get_voxel(5, 4, 3) == voxels::gray[1]);
    REQUIRE(block->get_voxel(4, 3, 2) == voxels::gray[1]);
    REQUIRE(solid_count(*block) == 3 + 8);
}

TEST_CASE("a paste lands where the pivot of the target puts it", "[clip]") {
    asset::model_registry registry;
    const auto source = make_block(registry);
    const auto clip   = asset::copied(*source, {.min = {1, 1, 1}, .max = {2, 2, 1}});

    auto target = registry.create_unnamed(vec3i{8, 8, 8});
    target->set_pivot(vec3f{4.f, 3.5f, 5.f});

    REQUIRE(asset::paste_origin(*target, clip) == vec3i{2, 2, 4});
    REQUIRE(asset::paste_origin(*source, clip) == vec3i{1, 1, 1});
}

TEST_CASE("a paste inside the volume keeps its size and pivot", "[clip]") {
    asset::model_registry registry;
    const auto source = make_block(registry);
    const auto clip   = asset::copied(*source, {.min = {1, 1, 1}, .max = {2, 2, 1}});

    const auto result =
        asset::pasted(*source, clip, {3, 2, 2}, asset::paste_mode::keep_air, registry);

    REQUIRE(result->size() == block_size);
    REQUIRE(result->pivot() == source->pivot());
    REQUIRE(result->get_voxel(3, 2, 2) == voxels::gray[4]);
    REQUIRE(result->get_voxel(4, 2, 2) == voxels::gray[7]);
    REQUIRE(result->get_voxel(4, 3, 2) == voxels::gray[2]);
    REQUIRE(result->get_voxel(1, 1, 1) == voxels::gray[4]);
    REQUIRE(solid_count(*result) == 7);
}

TEST_CASE("the air of a clip spares the target unless the paste replaces", "[clip]") {
    asset::model_registry registry;
    const auto source = make_block(registry);
    const auto clip   = asset::copied(*source, {.min = {1, 1, 1}, .max = {2, 2, 1}});

    auto target = registry.create_unnamed(block_size);
    {
        asset::model_writer writer{*target};
        writer.set(1, 2, 1, voxels::gray[5]);
        writer.set(1, 1, 1, voxels::gray[5]);
    }

    const auto kept =
        asset::pasted(*target, clip, {1, 1, 1}, asset::paste_mode::keep_air, registry);
    const auto replaced =
        asset::pasted(*target, clip, {1, 1, 1}, asset::paste_mode::replace, registry);

    REQUIRE(kept->get_voxel(1, 1, 1) == voxels::gray[4]);
    REQUIRE(kept->get_voxel(1, 2, 1) == voxels::gray[5]);
    REQUIRE(replaced->get_voxel(1, 1, 1) == voxels::gray[4]);
    REQUIRE(replaced->is_empty(1, 2, 1));
}

TEST_CASE("a paste past the edge grows the volume and moves the pivot", "[clip]") {
    asset::model_registry registry;
    const auto source = make_block(registry);
    const auto clip   = asset::copied(*source, {.min = {1, 1, 1}, .max = {2, 2, 1}});

    const auto result =
        asset::pasted(*source, clip, {-2, 4, 3}, asset::paste_mode::keep_air, registry);

    REQUIRE(result->size() == vec3i{8, 6, 4});
    REQUIRE(result->pivot() == vec3f{5.f, 2.5f, 2.f});
    REQUIRE(result->get_voxel(0, 4, 3) == voxels::gray[4]);
    REQUIRE(result->get_voxel(1, 4, 3) == voxels::gray[7]);
    REQUIRE(result->get_voxel(1, 5, 3) == voxels::gray[2]);
    REQUIRE(result->get_voxel(3, 1, 1) == voxels::gray[4]);
    REQUIRE(result->get_voxel(7, 4, 3) == voxels::gray[9]);
    REQUIRE(solid_count(*result) == 7);
}

TEST_CASE("the air rim of a clip does not grow the volume", "[clip]") {
    asset::model_registry registry;
    const auto source = make_block(registry);
    const auto clip   = asset::copied(*source, {.min = {0, 0, 0}, .max = {2, 2, 2}});

    const auto result =
        asset::pasted(*source, clip, {-1, -1, -1}, asset::paste_mode::replace, registry);

    REQUIRE(result->size() == block_size);
    REQUIRE(result->get_voxel(0, 0, 0) == voxels::gray[4]);
    REQUIRE(result->get_voxel(1, 0, 0) == voxels::gray[7]);
    REQUIRE(result->get_voxel(1, 1, 0) == voxels::gray[2]);
    REQUIRE(result->is_empty(1, 1, 1));
}

TEST_CASE("an empty clip pastes as a plain copy", "[clip]") {
    asset::model_registry registry;
    const auto source = make_block(registry);

    const auto result = asset::pasted(
        *source, asset::voxel_clip{}, {0, 0, 0}, asset::paste_mode::replace, registry
    );

    REQUIRE(result->size() == block_size);
    REQUIRE(solid_count(*result) == 4);
}

TEST_CASE("a mirrored clip keeps its box and swaps its sides", "[clip]") {
    asset::model_registry registry;
    const auto source = make_block(registry);
    const auto clip   = asset::copied(*source, {.min = {1, 1, 1}, .max = {2, 2, 1}});

    const auto mirror =
        asset::reoriented(clip, asset::mirrored_orientation(asset::voxel_axis::x));

    REQUIRE(mirror.size == clip.size);
    REQUIRE(mirror.corner_from_pivot == clip.corner_from_pivot);
    REQUIRE(mirror.at({1, 0, 0}) == voxels::gray[4]);
    REQUIRE(mirror.at({0, 0, 0}) == voxels::gray[7]);
    REQUIRE(mirror.at({0, 1, 0}) == voxels::gray[2]);
    REQUIRE(mirror.at({1, 1, 0}).is_empty());
}

TEST_CASE("a turned clip swaps its sizes and stays around its centre", "[clip]") {
    asset::model_registry registry;
    const auto source = make_block(registry);
    const auto clip   = asset::copied(*source, {.min = {1, 1, 1}, .max = {4, 2, 1}});

    const auto turned =
        asset::reoriented(clip, asset::rotated_orientation(asset::voxel_axis::z, 1));

    REQUIRE(clip.size == vec3i{4, 2, 1});
    REQUIRE(turned.size == vec3i{2, 4, 1});
    REQUIRE(turned.corner_from_pivot == clip.corner_from_pivot + vec3f{1.f, -1.f, 0.f});
    REQUIRE(turned.at({1, 0, 0}) == voxels::gray[4]);
    REQUIRE(turned.at({1, 1, 0}) == voxels::gray[7]);
    REQUIRE(turned.at({0, 1, 0}) == voxels::gray[2]);
}

TEST_CASE("a clip pasted back where it was copied changes nothing", "[clip]") {
    asset::model_registry registry;
    const auto source = make_block(registry);
    const auto region = asset::voxel_bounds{.min = {0, 0, 0}, .max = {5, 4, 3}};
    const auto clip   = asset::copied(*source, region);

    const auto result = asset::pasted(
        *source, clip, asset::paste_origin(*source, clip), asset::paste_mode::replace, registry
    );

    REQUIRE(result->size() == block_size);
    REQUIRE(result->pivot() == source->pivot());
    REQUIRE(solid_count(*result) == 4);
    REQUIRE(result->get_voxel(5, 4, 3) == voxels::gray[9]);
}
