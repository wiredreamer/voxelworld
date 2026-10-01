#include <catch2/catch_test_macros.hpp>

import std;

import vw.core;
import vw.asset;

using namespace vw;

namespace {

constexpr vec3i slab_size{4, 5, 6};

constexpr std::array all_axes{asset::voxel_axis::x, asset::voxel_axis::y, asset::voxel_axis::z};

auto make_slab(asset::model_registry& registry) -> std::shared_ptr<asset::model> {
    auto m = registry.create_unnamed(slab_size);
    {
        asset::model_writer writer{*m};
        writer.set(1, 2, 3, voxels::gray[4]);
        writer.set(0, 0, 0, voxels::gray[7]);
        writer.set(3, 4, 5, voxels::gray[2]);
        writer.set(2, 0, 4, voxels::gray[9]);
    }
    m->set_pivot(vec3f{1.5f, 2.f, 0.5f});
    return m;
}

auto same_volume(const asset::model& left, const asset::model& right) -> bool {
    if (left.size() != right.size() || left.pivot() != right.pivot()) {
        return false;
    }

    const auto size = left.size();
    for (int32 x = 0; x < size.x; ++x) {
        for (int32 y = 0; y < size.y; ++y) {
            for (int32 z = 0; z < size.z; ++z) {
                if (left.get_voxel(x, y, z) != right.get_voxel(x, y, z)) {
                    return false;
                }
            }
        }
    }

    return true;
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

TEST_CASE("the default orientation copies the volume as it is", "[reorient]") {
    asset::model_registry registry;
    const auto source = make_slab(registry);

    const auto copy = asset::reoriented(*source, asset::voxel_orientation{}, registry);

    REQUIRE(copy != nullptr);
    REQUIRE(copy != source);
    REQUIRE(same_volume(*copy, *source));
}

TEST_CASE("a mirror moves every voxel to the opposite side of its axis", "[reorient]") {
    asset::model_registry registry;
    const auto source = make_slab(registry);

    const auto mirror = asset::reoriented(
        *source, asset::mirrored_orientation(asset::voxel_axis::x), registry
    );

    REQUIRE(mirror->size() == slab_size);
    REQUIRE(mirror->get_voxel(2, 2, 3) == voxels::gray[4]);
    REQUIRE(mirror->get_voxel(3, 0, 0) == voxels::gray[7]);
    REQUIRE(mirror->get_voxel(0, 4, 5) == voxels::gray[2]);
    REQUIRE(mirror->get_voxel(1, 0, 4) == voxels::gray[9]);
    REQUIRE(solid_count(*mirror) == 4);
}

TEST_CASE("a mirror reflects the pivot along with the voxels", "[reorient]") {
    asset::model_registry registry;
    const auto source = make_slab(registry);

    const auto along_x = asset::reoriented(
        *source, asset::mirrored_orientation(asset::voxel_axis::x), registry
    );
    const auto along_z = asset::reoriented(
        *source, asset::mirrored_orientation(asset::voxel_axis::z), registry
    );

    REQUIRE(along_x->pivot() == vec3f{2.5f, 2.f, 0.5f});
    REQUIRE(along_z->pivot() == vec3f{1.5f, 2.f, 5.5f});
}

TEST_CASE("mirroring twice along one axis restores the volume", "[reorient]") {
    asset::model_registry registry;
    const auto source = make_slab(registry);

    for (const auto axis : all_axes) {
        const auto how   = asset::mirrored_orientation(axis);
        const auto once  = asset::reoriented(*source, how, registry);
        const auto twice = asset::reoriented(*once, how, registry);

        REQUIRE(!same_volume(*once, *source));
        REQUIRE(same_volume(*twice, *source));
    }
}

TEST_CASE("a quarter turn swaps the two sizes across its axis", "[reorient]") {
    asset::model_registry registry;
    const auto source = make_slab(registry);

    const auto about_x = asset::reoriented(
        *source, asset::rotated_orientation(asset::voxel_axis::x, 1), registry
    );
    const auto about_y = asset::reoriented(
        *source, asset::rotated_orientation(asset::voxel_axis::y, 1), registry
    );
    const auto about_z = asset::reoriented(
        *source, asset::rotated_orientation(asset::voxel_axis::z, 1), registry
    );

    REQUIRE(about_x->size() == vec3i{4, 6, 5});
    REQUIRE(about_y->size() == vec3i{6, 5, 4});
    REQUIRE(about_z->size() == vec3i{5, 4, 6});
}

TEST_CASE("a quarter turn about y carries x onto minus z and z onto x", "[reorient]") {
    asset::model_registry registry;
    const auto source = make_slab(registry);

    const auto turned = asset::reoriented(
        *source, asset::rotated_orientation(asset::voxel_axis::y, 1), registry
    );

    REQUIRE(turned->get_voxel(3, 2, 2) == voxels::gray[4]);
    REQUIRE(turned->get_voxel(0, 0, 3) == voxels::gray[7]);
    REQUIRE(turned->get_voxel(5, 4, 0) == voxels::gray[2]);
    REQUIRE(turned->get_voxel(4, 0, 1) == voxels::gray[9]);
    REQUIRE(solid_count(*turned) == 4);
    REQUIRE(turned->pivot() == vec3f{0.5f, 2.f, 2.5f});
}

TEST_CASE("four quarter turns about any axis restore the volume", "[reorient]") {
    asset::model_registry registry;
    const auto source = make_slab(registry);

    for (const auto axis : all_axes) {
        const auto how = asset::rotated_orientation(axis, 1);

        auto current = asset::reoriented(*source, how, registry);
        REQUIRE(!same_volume(*current, *source));

        for (int32 turn = 1; turn < 4; ++turn) {
            current = asset::reoriented(*current, how, registry);
        }

        REQUIRE(same_volume(*current, *source));
    }
}

TEST_CASE("a backward quarter turn undoes a forward one", "[reorient]") {
    asset::model_registry registry;
    const auto source = make_slab(registry);

    for (const auto axis : all_axes) {
        const auto forward = asset::reoriented(
            *source, asset::rotated_orientation(axis, 1), registry
        );
        const auto back = asset::reoriented(
            *forward, asset::rotated_orientation(axis, -1), registry
        );

        REQUIRE(same_volume(*back, *source));
    }
}

TEST_CASE("quarter turns wrap around a full turn", "[reorient]") {
    using asset::rotated_orientation;
    using asset::voxel_axis;

    REQUIRE(rotated_orientation(voxel_axis::y, 4) == asset::voxel_orientation{});
    REQUIRE(rotated_orientation(voxel_axis::y, 5) == rotated_orientation(voxel_axis::y, 1));
    REQUIRE(rotated_orientation(voxel_axis::y, -1) == rotated_orientation(voxel_axis::y, 3));
    REQUIRE(rotated_orientation(voxel_axis::y, -6) == rotated_orientation(voxel_axis::y, 2));
}

TEST_CASE("a half turn mirrors both axes across the turning one", "[reorient]") {
    asset::model_registry registry;
    const auto source = make_slab(registry);

    const auto half = asset::reoriented(
        *source, asset::rotated_orientation(asset::voxel_axis::y, 2), registry
    );

    const auto along_x = asset::reoriented(
        *source, asset::mirrored_orientation(asset::voxel_axis::x), registry
    );
    const auto along_both = asset::reoriented(
        *along_x, asset::mirrored_orientation(asset::voxel_axis::z), registry
    );

    REQUIRE(same_volume(*half, *along_both));
}

TEST_CASE("reorienting leaves the source untouched", "[reorient]") {
    asset::model_registry registry;
    const auto source = make_slab(registry);
    const auto before = source->get_identity();

    const auto turned = asset::reoriented(
        *source, asset::rotated_orientation(asset::voxel_axis::z, 1), registry
    );

    REQUIRE(source->get_identity() == before);
    REQUIRE(turned->get_identity() != before);
    REQUIRE(source->get_voxel(1, 2, 3) == voxels::gray[4]);
}
