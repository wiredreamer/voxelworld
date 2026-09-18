#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

import std;

import vw.core;
import vw.asset;
import vw.ecs;
import vw.world;

using namespace vw;
using namespace vw::ecs;

namespace {

auto make_cube(world& w, asset::model_registry& models, const char* name) -> entity {
    auto model = models.create(name, voxels::world::category, 4, 4, 4);
    model->fill(voxels::world::grass[0]);

    const auto ent = w.create().with<transform_component>().with<model_component>().get_entity();
    w.system<model_system>().modify(ent).set_model(std::move(model));
    w.modify(ent).with<spatial_component>();
    return ent;
}

}  // namespace

TEST_CASE("world runs without a graphics device", "[world]") {
    world w;
    auto& models = w.resource<asset::model_registry>();

    const auto ent = make_cube(w, models, "cube");
    w.update(0.016F);

    REQUIRE(w.has<spatial_component>(ent));
    REQUIRE(w.get<spatial_component>(ent).get_bounds().size().x > 0.0F);
}

TEST_CASE("moving a transform refreshes the spatial bounds", "[world]") {
    world w;
    auto& models = w.resource<asset::model_registry>();

    const auto ent = make_cube(w, models, "cube");
    w.update(0.016F);

    const auto before = w.get<spatial_component>(ent).get_bounds();

    w.system<transform_system>().modify(ent).set_position(vec3f{100.0F, 0.0F, 0.0F});
    w.update(0.016F);

    const auto after = w.get<spatial_component>(ent).get_bounds();
    REQUIRE(after.min.x > before.min.x + 50.0F);
}

TEST_CASE("a moved viewer is reported to the world grid", "[world]") {
    world w;

    const auto ent =
        w.create().with<transform_component>().with<world_view_component>().get_entity();
    w.update(0.016F);
    w.registry().clear_requested<world_view_component>();

    w.system<transform_system>().modify(ent).set_position(vec3f{0.0F, 0.0F, 64.0F});
    w.update(0.016F);

    REQUIRE(std::ranges::contains(w.registry().requested<world_view_component>(), ent));
}

TEST_CASE("voxels survive a round trip through the model registry", "[world]") {
    asset::model_registry models;

    auto model = models.create("scratch", voxels::world::category, 16, 16, 16);
    model->set_voxel(1, 2, 3, voxels::world::dirt[0]);

    REQUIRE(models.get("scratch")->get_voxel(1, 2, 3) == voxels::world::dirt[0]);
    REQUIRE(models.get("scratch")->is_empty(4, 5, 6));
}

TEST_CASE("chunk occupancy matches the voxel volume bit for bit", "[world][occupancy]") {
    asset::model_identity_pool identity_pool;
    asset::page_pool pages;

    constexpr int32 side = asset::chunk_occupancy::side;
    asset::model model{identity_pool, pages, voxels::world::category, side, side, side};

    asset::model_writer writer{model};

    for (int32 x = 0; x < 8; ++x) {
        for (int32 y = 0; y < 8; ++y) {
            for (int32 z = 0; z < 8; ++z) {
                writer.set(x, y, z, voxels::world::stone_deep[2]);
            }
        }
    }

    uint32 state = 4242;
    for (int32 i = 0; i < 4000; ++i) {
        state = (state * 1664525U) + 1013904223U;
        const int32 x = static_cast<int32>((state >> 8) % side);
        const int32 y = static_cast<int32>((state >> 14) % side);
        const int32 z = static_cast<int32>((state >> 20) % side);
        writer.set(x, y, z, voxels::world::clay[0]);
    }

    asset::chunk_occupancy occupancy;
    REQUIRE(model.build_occupancy(occupancy));

    std::size_t mismatches = 0;
    for (int32 x = 0; x < side; ++x) {
        for (int32 y = 0; y < side; ++y) {
            for (int32 z = 0; z < side; ++z) {
                if (occupancy.test(x, y, z) == model.is_empty(x, y, z)) {
                    ++mismatches;
                }
            }
        }
    }
    REQUIRE(mismatches == 0);
}

TEST_CASE("chunk occupancy declines models that are not 64 cubes", "[world][occupancy]") {
    asset::model_identity_pool identity_pool;
    asset::page_pool pages;
    asset::model model{identity_pool, pages, voxels::world::category, 32, 32, 32};

    asset::chunk_occupancy occupancy;
    REQUIRE_FALSE(model.build_occupancy(occupancy));
}

TEST_CASE("a collider without a model still gets spatial bounds", "[world]") {
    world w;

    const auto ent = w.create()
                         .with<transform_component>()
                         .with<spatial_component>()
                         .with<box_collider_component>()
                         .get_entity();

    w.system<spatial_system>().modify(ent).set_layer(spatial_layer::character);
    w.system<physics_system>()
        .modify_collider(ent)
        .set_extents(vec3f{12.0F, 24.0F, 12.0F})
        .set_offset(vec3f{0.0F, 12.0F, 0.0F});
    w.system<transform_system>().modify(ent).set_position(vec3f{100.0F, 40.0F, -20.0F});
    w.update(0.016F);

    const auto bounds = w.get<spatial_component>(ent).get_bounds();

    REQUIRE(bounds.min.y == 40.0F);
    REQUIRE(bounds.max.y == 64.0F);
    REQUIRE(bounds.size().x == 12.0F);

    std::vector<entity> found;
    w.system<spatial_system>().query_all(
        spatial::aabb{vec3f{96.0F, 36.0F, -24.0F}, vec3f{104.0F, 44.0F, -16.0F}}, found,
        spatial_layer::character
    );
    REQUIRE(std::ranges::find(found, ent) != found.end());
}

TEST_CASE("voxel scale reaches the bounds once", "[world]") {
    world w;
    auto& models = w.resource<asset::model_registry>();

    constexpr int32 world_units_per_voxel = 16;
    constexpr int32 side        = 4;

    auto model = std::make_shared<asset::model>(
        models.get_identity_pool(), models.get_page_pool(), voxels::world::category, side,
        side, side, world_units_per_voxel
    );
    model->fill(voxels::world::grass[0]);

    const auto ent = w.create().with<transform_component>().with<model_component>().get_entity();
    w.system<model_system>().modify(ent).set_model(std::move(model));
    w.modify(ent).with<spatial_component>();
    w.system<transform_system>().modify(ent).set_scale(
        vec3f{
            static_cast<float32>(world_units_per_voxel),
            static_cast<float32>(world_units_per_voxel),
            static_cast<float32>(world_units_per_voxel)
        }
    );
    w.update(0.016F);

    const auto bounds = w.get<spatial_component>(ent).get_bounds();
    REQUIRE(bounds.size().x == static_cast<float32>(side * world_units_per_voxel));
    REQUIRE(bounds.size().y == static_cast<float32>(side * world_units_per_voxel));
    REQUIRE(bounds.size().z == static_cast<float32>(side * world_units_per_voxel));
}

TEST_CASE("a model outranks a collider when an entity has both", "[world]") {
    world w;
    auto& models = w.resource<asset::model_registry>();

    const auto ent = make_cube(w, models, "cube");
    w.modify(ent).with<box_collider_component>();
    w.system<physics_system>().modify_collider(ent).set_extents(vec3f{100.0F, 100.0F, 100.0F});
    w.update(0.016F);

    REQUIRE(w.get<spatial_component>(ent).get_bounds().size().x == 4.0F);
}

TEST_CASE("every system reports its own timing", "[world]") {
    world w;
    w.update(0.016F);

    const auto& stats = w.get_update_stats();

    REQUIRE(world_system_names.size() == world_system_count);
    for (const auto name : world_system_names) {
        REQUIRE_FALSE(name.empty());
    }

    float32 sum = 0.0F;
    for (const auto ms : stats.ms) {
        REQUIRE(ms >= 0.0F);
        sum += ms;
    }
    REQUIRE(stats.total_ms == Catch::Approx(sum));
}

TEST_CASE("removing a model is reported as a model change", "[world]") {
    world w;
    auto& models = w.resource<asset::model_registry>();

    const auto ent = make_cube(w, models, "cube");
    w.update(0.016F);
    w.clear_changed();

    w.modify(ent).without<model_component>();
    w.update(0.016F);

    REQUIRE(std::ranges::contains(w.changed<model_component>(), ent));
}

TEST_CASE("a hidden model lets the picking ray through", "[world]") {
    world w;
    auto& models = w.resource<asset::model_registry>();

    const auto front = make_cube(w, models, "front");
    const auto back  = make_cube(w, models, "back");
    w.system<transform_system>().modify(back).set_position(vec3f{0.0F, 0.0F, 10.0F});
    w.update(0.016F);

    const spatial::ray ray{vec3f{2.0F, 2.0F, -10.0F}, vec3f{2.0F, 2.0F, 30.0F}};
    std::vector<entity> candidates;

    const auto seen = w.system<spatial_system>().voxel_ray_cast(ray, candidates);
    REQUIRE(seen.has_value());
    REQUIRE(seen->ent == front);

    w.system<model_system>().modify(front).set_visible(false);

    const auto through = w.system<spatial_system>().voxel_ray_cast(ray, candidates);
    REQUIRE(through.has_value());
    REQUIRE(through->ent == back);
}
