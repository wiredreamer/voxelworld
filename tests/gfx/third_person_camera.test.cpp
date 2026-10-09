#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

import std;

import vw.core;
import vw.asset;
import vw.ecs;
import vw.world;
import vw.gfx;

using namespace vw;
using Catch::Approx;

namespace {

constexpr float32 tick_seconds = 1.0F / 60.0F;
constexpr float32 arm          = 40.0F;
constexpr float32 head_height  = 10.0F;
constexpr float32 pivot_rise   = 5.0F;
constexpr float32 skin         = 2.0F;
constexpr float32 probe        = 4.0F;

struct stage {
    ecs::world world;
    gfx::camera eye;
    gfx::third_person_camera_controller rig{
        eye,
        world,
        gfx::third_person_camera_params{
            .arm_length     = arm,
            .arm_length_min = 1.0F,
            .arm_length_max = 200.0F,
            .target_offset  = {0.0F, head_height, 0.0F},
            .collision_skin = skin,
            .pivot_rise     = pivot_rise,
            .probe_radius   = probe,
        }
    };
    ecs::entity hero = world.create()
        .with<ecs::hierarchy_component>()
        .with<ecs::transform_component>()
        .get_entity();

    auto wall(const vec3f& centre, const vec3i& size) -> ecs::entity {
        auto model = world.resource<asset::model_registry>().create_unnamed(size.x, size.y, size.z);
        model->fill(voxels::gray[9]);
        model->set_pivot({
            static_cast<float32>(size.x) * 0.5F,
            static_cast<float32>(size.y) * 0.5F,
            static_cast<float32>(size.z) * 0.5F,
        });

        const auto ent = world.create()
            .with<ecs::hierarchy_component>()
            .with<ecs::transform_component>()
            .with<ecs::spatial_component>()
            .with<ecs::model_component>()
            .get_entity();
        world.system<ecs::model_system>().modify(ent).set_model(model);
        world.system<ecs::spatial_system>().modify(ent).set_layer(ecs::spatial_layer::terrain);
        world.system<ecs::transform_system>().modify(ent).set_position(centre);
        settle();
        return ent;
    }

    auto settle() -> void {
        world.update(tick_seconds);
        world.clear_changed();
    }

    auto look(float32 pitch_degrees, float32 delta_time = tick_seconds) -> vec3f {
        rig.update(hero, 0.0F, pitch_degrees, 0.0F, 0.0F, false, delta_time);
        return eye.get_position();
    }
};

}  // namespace

TEST_CASE("the camera turns about a point above the head, not about the head", "[camera][third_person]") {
    stage s;
    s.settle();

    const vec3f level = s.look(0.0F);

    CHECK(level.x == Approx(0.0F).margin(1e-3F));
    CHECK(level.y == Approx(head_height + pivot_rise).margin(1e-3F));
    CHECK(level.z == Approx(-arm).margin(1e-3F));
}

TEST_CASE("looking up pulls the camera in and looking down lets it out", "[camera][third_person]") {
    stage s;
    s.settle();
    const auto& params = s.rig.get_params();

    s.look(0.0F);
    const float32 level = s.rig.get_actual_arm_length();

    s.look(params.look_up_full_degrees);
    const float32 up = s.rig.get_actual_arm_length();

    s.look(-params.look_down_full_degrees, 10.0F);
    const float32 down = s.rig.get_actual_arm_length();

    CHECK(level == Approx(arm));
    CHECK(up == Approx(arm * params.look_up_arm_share));
    CHECK(down == Approx(arm * params.look_down_arm_share));

    s.look(params.look_up_full_degrees * 0.5F);
    const float32 halfway = s.rig.get_actual_arm_length();
    CHECK(halfway < level);
    CHECK(halfway > up);
}

TEST_CASE("looking up does not sink the camera under the feet", "[camera][third_person]") {
    stage s;
    s.settle();

    const vec3f eye = s.look(s.rig.get_params().look_up_full_degrees);

    CHECK(eye.y > 0.0F);
}

TEST_CASE("a wall behind stops the camera in front of its face", "[camera][third_person]") {
    stage s;
    s.wall({0.0F, 15.0F, -28.0F}, {64, 64, 16});
    const float32 face = -20.0F;

    const vec3f eye = s.look(0.0F);

    CHECK(eye.z == Approx(face + skin).margin(1e-2F));
}

TEST_CASE("a ledge beside the line of sight still stops the camera", "[camera][third_person]") {
    stage s;
    s.wall({10.0F, 15.0F, -28.0F}, {16, 64, 16});
    const float32 face = -20.0F;

    const vec3f eye = s.look(0.0F);

    CHECK(eye.x == Approx(0.0F).margin(1e-3F));
    CHECK(eye.z == Approx(face + skin).margin(0.2F));
}

TEST_CASE("the camera snaps in at a wall and eases back out", "[camera][third_person]") {
    stage s;
    const auto block = s.wall({0.0F, 15.0F, -28.0F}, {64, 64, 16});
    const float32 near = s.look(0.0F).z;

    s.world.system<ecs::transform_system>().modify(block).set_position({0.0F, 15.0F, -500.0F});
    s.settle();

    const float32 just_after = s.look(0.0F).z;
    CHECK(just_after < near);
    CHECK(just_after > -arm);

    float32 settled = just_after;
    for (uint32 frame = 0; frame < 240; ++frame) {
        settled = s.look(0.0F).z;
    }
    CHECK(settled == Approx(-arm).margin(1e-2F));
}

TEST_CASE("the shoulder shift does not carry the pivot into a wall", "[camera][third_person]") {
    stage s;
    s.wall({16.0F, 15.0F, 0.0F}, {16, 64, 128});
    const float32 face = 8.0F;

    vec3f eye{};
    for (uint32 frame = 0; frame < 240; ++frame) {
        s.rig.update(s.hero, 0.0F, 0.0F, 0.0F, 0.0F, true, tick_seconds);
        eye = s.eye.get_position();
    }

    CHECK(eye.x < face);
}

TEST_CASE("under a ceiling lower than the head the camera stays below it", "[camera][third_person]") {
    stage s;
    s.rig.get_params().pivot_rise = 0.0F;
    const float32 ceiling = head_height - 2.0F;
    s.wall({0.0F, ceiling + 8.0F, 0.0F}, {256, 16, 256});

    const vec3f eye = s.look(0.0F);

    CHECK(eye.y < ceiling);
    CHECK(eye.y == Approx(ceiling - skin).margin(1e-2F));
    CHECK(eye.z < -arm * 0.25F);
}

TEST_CASE("the body the camera follows never stops its rays", "[camera][third_person]") {
    stage s;
    const auto skull = s.wall({0.0F, head_height - 2.0F, 0.0F}, {8, 4, 8});
    s.world.system<ecs::hierarchy_system>().modify(skull).set_parent(s.hero);
    s.settle();

    const vec3f eye = s.look(0.0F);

    CHECK(eye.y == Approx(head_height + pivot_rise).margin(1e-3F));
    CHECK(eye.z == Approx(-arm).margin(1e-3F));
}
