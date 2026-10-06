#include <catch2/catch_test_macros.hpp>

import std;

import vw.core;
import vw.asset;
import vw.ecs;
import vw.world;

using namespace vw;
using namespace vw::ecs;

namespace {

constexpr float32 base = 100.0F;

auto place(world& w, asset::model_registry& models, const char* name, vec3f at) -> entity {
    auto model = models.create(name, 4, 4, 4);
    model->fill(voxels::green[4]);

    const auto ent = w.create()
                         .with<transform_component>()
                         .with<model_component>()
                         .with<lod_component>()
                         .get_entity();
    w.system<model_system>().modify(ent).set_model(std::move(model));
    w.system<transform_system>().modify(ent).set_position(at);
    return ent;
}

auto place_viewer(world& w, vec3f at) -> entity {
    const auto ent =
        w.create().with<transform_component>().with<world_view_component>().get_entity();
    w.system<transform_system>().modify(ent).set_position(at);
    return ent;
}

}  // namespace

TEST_CASE("the level climbs with distance and stops at the last one", "[world][lod]") {
    REQUIRE(lod_system::pick_level(0.0F, base, 0) == 0);
    REQUIRE(lod_system::pick_level(base * 1.9F, base, 0) == 0);
    REQUIRE(lod_system::pick_level(base * 2.0F, base, 0) == 1);
    REQUIRE(lod_system::pick_level(base * 4.0F, base, 0) == 2);
    REQUIRE(lod_system::pick_level(base * 8.0F, base, 0) == 3);
    REQUIRE(lod_system::pick_level(base * 1000.0F, base, 0) == 3);
}

TEST_CASE("without a base distance every model stays at full detail", "[world][lod]") {
    for (const float32 distance : {0.0F, 100.0F, 100000.0F}) {
        REQUIRE(lod_system::pick_level(distance, 0.0F, 0) == 0);
        REQUIRE(lod_system::pick_level(distance, 0.0F, 3) == 0);
    }
}

TEST_CASE("a level once taken is not given back at the same distance", "[world][lod]") {
    const float32 edge = base * 2.0F;

    REQUIRE(lod_system::pick_level(edge - 1.0F, base, 0) == 0);
    REQUIRE(lod_system::pick_level(edge - 1.0F, base, 1) == 1);

    const float32 release = edge * lod_system::hysteresis_release;
    REQUIRE(lod_system::pick_level(release + 1.0F, base, 1) == 1);
    REQUIRE(lod_system::pick_level(release - 1.0F, base, 1) == 0);
}

TEST_CASE("jitter across a threshold does not flip the level twice", "[world][lod]") {
    const float32 edge = base * 2.0F;

    uint32 level  = 0;
    uint32 flips  = 0;

    for (int32 i = 0; i < 20; ++i) {
        const float32 distance = (i % 2 == 0) ? edge + 1.0F : edge - 1.0F;
        const uint32 next      = lod_system::pick_level(distance, base, level);
        if (next != level) {
            ++flips;
            level = next;
        }
    }

    REQUIRE(flips == 1);
    REQUIRE(level == 1);
}

TEST_CASE("the system measures distance without height", "[world][lod]") {
    world w;
    auto& models = w.resource<asset::model_registry>();
    auto& lod    = w.system<lod_system>();

    lod.set_default_base_distance(base);
    place_viewer(w, vec3f{0.0F, 0.0F, 0.0F});

    const auto low  = place(w, models, "low", vec3f{base * 4.0F, 0.0F, 0.0F});
    const auto high = place(w, models, "high", vec3f{base * 4.0F, base * 40.0F, 0.0F});
    const auto near = place(w, models, "near", vec3f{0.0F, base * 40.0F, 0.0F});

    w.update(0.016F);

    REQUIRE(w.get<model_component>(low).get_lod_level() == 2);
    REQUIRE(w.get<model_component>(high).get_lod_level() == 2);
    REQUIRE(w.get<model_component>(near).get_lod_level() == 0);
}

TEST_CASE("a level change asks the renderer for a new mesh", "[world][lod]") {
    world w;
    auto& models = w.resource<asset::model_registry>();
    auto& lod    = w.system<lod_system>();

    lod.set_default_base_distance(base);
    const auto viewer = place_viewer(w, vec3f{0.0F, 0.0F, 0.0F});
    const auto ent    = place(w, models, "block", vec3f{0.0F, 0.0F, 0.0F});

    w.update(0.016F);
    REQUIRE(w.get<model_component>(ent).get_lod_level() == 0);

    w.system<transform_system>().modify(viewer).set_position(vec3f{base * 8.0F, 0.0F, 0.0F});
    w.update(0.016F);

    REQUIRE(w.get<model_component>(ent).get_lod_level() == 3);

    const auto& changed = w.changed<model_component>();
    REQUIRE(std::ranges::find(changed, ent) != changed.end());

    w.update(0.016F);
    REQUIRE(lod.get_stats().raised == 0);
    REQUIRE(lod.get_stats().lowered == 0);
}

TEST_CASE("a per-entity distance overrides the default", "[world][lod]") {
    world w;
    auto& models = w.resource<asset::model_registry>();
    auto& lod    = w.system<lod_system>();

    lod.set_default_base_distance(base);
    place_viewer(w, vec3f{0.0F, 0.0F, 0.0F});

    const auto plain   = place(w, models, "plain", vec3f{base * 4.0F, 0.0F, 0.0F});
    const auto patient = place(w, models, "patient", vec3f{base * 4.0F, 0.0F, 0.0F});

    lod.set_base_distance(patient, base * 4.0F);

    w.update(0.016F);

    REQUIRE(w.get<model_component>(plain).get_lod_level() == 2);
    REQUIRE(w.get<model_component>(patient).get_lod_level() == 0);
}

TEST_CASE("the stats count every model at the level it holds", "[world][lod]") {
    world w;
    auto& models = w.resource<asset::model_registry>();
    auto& lod    = w.system<lod_system>();

    lod.set_default_base_distance(base);
    place_viewer(w, vec3f{0.0F, 0.0F, 0.0F});

    place(w, models, "near", vec3f{0.0F, 0.0F, 0.0F});
    place(w, models, "one", vec3f{base * 2.0F, 0.0F, 0.0F});
    place(w, models, "two_near", vec3f{base * 4.0F, 0.0F, 0.0F});
    place(w, models, "two_far", vec3f{base * 5.0F, 0.0F, 0.0F});
    place(w, models, "three", vec3f{base * 8.0F, 0.0F, 0.0F});

    w.update(0.016F);

    const auto& stats = lod.get_stats();

    REQUIRE(stats.entities == 5);
    REQUIRE(stats.at_level[0] == 1);
    REQUIRE(stats.at_level[1] == 1);
    REQUIRE(stats.at_level[2] == 2);
    REQUIRE(stats.at_level[3] == 1);

    const auto counted = std::accumulate(stats.at_level.begin(), stats.at_level.end(), 0U);
    REQUIRE(counted == stats.entities);

    w.update(0.016F);

    REQUIRE(lod.get_stats().raised == 0);
    REQUIRE(lod.get_stats().at_level[2] == 2);
}

TEST_CASE("a per-level distance overrides the geometric ladder", "[world][lod]") {
    world w;
    auto& models = w.resource<asset::model_registry>();
    auto& lod    = w.system<lod_system>();

    lod.set_default_base_distance(base);
    lod.set_level_distance(2, base * 1.5F);
    lod.set_level_distance(3, base * 2.0F);

    place_viewer(w, vec3f{0.0F, 0.0F, 0.0F});

    const auto ent = place(w, models, "block", vec3f{base * 2.0F, 0.0F, 0.0F});

    w.update(0.016F);

    REQUIRE(w.get<model_component>(ent).get_lod_level() == 3);
    REQUIRE(lod.get_level_distances()[1] == base * 2.0F);
    REQUIRE(lod.get_level_distances()[2] == base * 1.5F);
}

TEST_CASE("a level parked at zero stops the ladder there", "[world][lod]") {
    world w;
    auto& models = w.resource<asset::model_registry>();
    auto& lod    = w.system<lod_system>();

    lod.set_default_base_distance(base);
    lod.set_level_distance(2, 0.0F);

    place_viewer(w, vec3f{0.0F, 0.0F, 0.0F});

    const auto near = place(w, models, "near", vec3f{base * 2.0F, 0.0F, 0.0F});
    const auto far  = place(w, models, "far", vec3f{base * 1000.0F, 0.0F, 0.0F});

    w.update(0.016F);

    REQUIRE(w.get<model_component>(near).get_lod_level() == 1);
    REQUIRE(w.get<model_component>(far).get_lod_level() == 1);

    const auto& stats = lod.get_stats();
    REQUIRE(stats.at_level[2] == 0);
    REQUIRE(stats.at_level[3] == 0);
}

TEST_CASE("rebuilding the base wipes a hand-set level", "[world][lod]") {
    world w;
    auto& lod = w.system<lod_system>();

    lod.set_default_base_distance(base);
    lod.set_level_distance(2, base * 1.5F);
    REQUIRE(lod.get_level_distances()[2] == base * 1.5F);

    lod.set_default_base_distance(base);
    REQUIRE(lod.get_level_distances()[2] == base * 4.0F);
}

TEST_CASE("a model without the component keeps its full detail", "[world][lod]") {
    world w;
    auto& models = w.resource<asset::model_registry>();
    auto& lod    = w.system<lod_system>();

    lod.set_default_base_distance(base);
    place_viewer(w, vec3f{0.0F, 0.0F, 0.0F});

    auto model = models.create("prop", 4, 4, 4);
    model->fill(voxels::green[4]);

    const auto prop =
        w.create().with<transform_component>().with<model_component>().get_entity();
    w.system<model_system>().modify(prop).set_model(std::move(model));
    w.system<transform_system>().modify(prop).set_position(vec3f{base * 8.0F, 0.0F, 0.0F});

    const auto chunk = place(w, models, "chunk", vec3f{base * 8.0F, 0.0F, 0.0F});

    w.update(0.016F);

    REQUIRE(w.get<model_component>(chunk).get_lod_level() == 3);
    REQUIRE(w.get<model_component>(prop).get_lod_level() == 0);
    REQUIRE(lod.get_stats().entities == 1);
}
