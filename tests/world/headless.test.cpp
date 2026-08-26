#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

import std;

import vw.core;
import vw.ecs;
import vw.world;

using namespace vw;
using namespace vw::ecs;

namespace {

auto make_cube(world& w, asset::model_registry& models, const char* name) -> entity {
    auto model = models.create(name, blocks::terrain::category, 4, 4, 4);
    model->fill(voxel{blocks::terrain::grass[0]});

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

    REQUIRE(w.registry().requested<world_view_component>().contains(ent));
}

TEST_CASE("voxels survive a round trip through the model registry", "[world]") {
    asset::model_registry models;

    auto model = models.create("scratch", blocks::terrain::category, 16, 16, 16);
    model->set_voxel(1, 2, 3, voxel{blocks::terrain::dirt[0]});

    REQUIRE(models.get("scratch")->get_voxel(1, 2, 3).id == blocks::terrain::dirt[0]);
    REQUIRE(models.get("scratch")->is_empty(4, 5, 6));
}

TEST_CASE("chunk occupancy matches the voxel volume bit for bit", "[world][occupancy]") {
    asset::model_identity_pool identity_pool;
    asset::page_pool pages;

    constexpr int32 side = asset::chunk_occupancy::side;
    asset::model model{identity_pool, pages, blocks::terrain::category, side, side, side};

    asset::model_writer writer{model};

    // One uniform page, one sparse page and a lot of empty ones, so all three
    // page modes take part.
    for (int32 x = 0; x < 8; ++x) {
        for (int32 y = 0; y < 8; ++y) {
            for (int32 z = 0; z < 8; ++z) {
                writer.set(x, y, z, voxel{blocks::terrain::stone_deep[2]});
            }
        }
    }

    uint32 state = 4242;
    for (int32 i = 0; i < 4000; ++i) {
        state = (state * 1664525U) + 1013904223U;
        const int32 x = static_cast<int32>((state >> 8) % side);
        const int32 y = static_cast<int32>((state >> 14) % side);
        const int32 z = static_cast<int32>((state >> 20) % side);
        writer.set(x, y, z, voxel{blocks::terrain::clay[0]});
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
    asset::model model{identity_pool, pages, blocks::terrain::category, 32, 32, 32};

    asset::chunk_occupancy occupancy;
    REQUIRE_FALSE(model.build_occupancy(occupancy));
}

// Корень иерархии сам ничего не рисует, и о своей форме знает только по коробке
// физики: пока границы считались лишь по модели, тело, собранное из детей, в
// дерево не попадало вовсе, сколько бы слоёв ему ни назначили.
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

    // Смещение поднимает коробку на половину роста, поэтому начало координат
    // тела — точка между ступнями, а не середина.
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

// Масштаб вокселя доезжает до границ ровно один раз — мировой матрицей, куда
// его кладёт chunk::create_entity_. Пока границы умножали на него второй раз
// сами, чанк арены получал коробку в шестнадцать раз больше себя.
TEST_CASE("voxel scale reaches the bounds once", "[world]") {
    world w;
    auto& models = w.resource<asset::model_registry>();

    constexpr int32 voxel_scale = 16;
    constexpr int32 side        = 4;

    auto model = std::make_shared<asset::model>(
        models.get_identity_pool(), models.get_page_pool(), blocks::terrain::category, side,
        side, side, voxel_scale
    );
    model->fill(voxel{blocks::terrain::grass[0]});

    const auto ent = w.create().with<transform_component>().with<model_component>().get_entity();
    w.system<model_system>().modify(ent).set_model(std::move(model));
    w.modify(ent).with<spatial_component>();
    w.system<transform_system>().modify(ent).set_scale(
        vec3f{
            static_cast<float32>(voxel_scale),
            static_cast<float32>(voxel_scale),
            static_cast<float32>(voxel_scale)
        }
    );
    w.update(0.016F);

    const auto bounds = w.get<spatial_component>(ent).get_bounds();
    REQUIRE(bounds.size().x == static_cast<float32>(side * voxel_scale));
    REQUIRE(bounds.size().y == static_cast<float32>(side * voxel_scale));
    REQUIRE(bounds.size().z == static_cast<float32>(side * voxel_scale));
}

// А у несущего и то, и другое границы остаются модельными: по ним такую
// сущность видели до сих пор, и подмена сдвинула бы и отсев, и попадания луча.
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
