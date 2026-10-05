#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

import std;

import vw.core;
import vw.ecs;
import vw.world;

using namespace vw;
using namespace vw::ecs;

namespace {

constexpr float32 tick_seconds = 0.05F;

auto make_character(world& w) -> entity {
    return w.create()
        .with<transform_component>()
        .with<rigid_body_component>()
        .with<character_controller_component>()
        .with<movement_intent_component>()
        .get_entity();
}

auto degrees_between(const quat& a, const quat& b) -> float32 {
    const float32 alignment = math::clamp(std::abs(math::dot(a, b)), 0.0F, 1.0F);
    return math::degrees(2.0F * std::acos(alignment));
}

auto degrees_left(world& w, entity ent, const vec3f& facing) -> float32 {
    return degrees_between(
        w.get<transform_component>(ent).get_rotation(), math::quat_look_y(facing)
    );
}

auto planar_speed(world& w, entity ent) -> float32 {
    const auto& wish = w.get<movement_intent_component>(ent).get_wish_velocity();
    return math::length(vec3f{wish.x, 0.0F, wish.z});
}

auto run_ticks(world& w, entity ent, const vec3f& input, int32 ticks) -> void {
    for (int32 tick = 0; tick < ticks; ++tick) {
        w.system<character_controller_system>().modify(ent).set_move_input(input);
        w.update(tick_seconds / 2.0F);
    }
}

}  // namespace

TEST_CASE("a character gathers speed over its acceleration time", "[world][character]") {
    world w;
    const auto ent    = make_character(w);
    const float32 top = w.get<character_controller_component>(ent).get_move_speed();
    const vec3f forward{0.0F, 0.0F, 1.0F};

    run_ticks(w, ent, forward, 1);
    REQUIRE(planar_speed(w, ent) == Catch::Approx(top * 0.25F));

    run_ticks(w, ent, forward, 3);
    REQUIRE(planar_speed(w, ent) == Catch::Approx(top));

    run_ticks(w, ent, forward, 1);
    REQUIRE(planar_speed(w, ent) == Catch::Approx(top));
}

TEST_CASE("a character comes to rest over its deceleration time", "[world][character]") {
    world w;
    const auto ent    = make_character(w);
    const float32 top = w.get<character_controller_component>(ent).get_move_speed();

    run_ticks(w, ent, vec3f{1.0F, 0.0F, 0.0F}, 4);
    REQUIRE(planar_speed(w, ent) == Catch::Approx(top));

    run_ticks(w, ent, vec3f{0.0F, 0.0F, 0.0F}, 3);
    REQUIRE(planar_speed(w, ent) == Catch::Approx(top * 0.5F));

    run_ticks(w, ent, vec3f{0.0F, 0.0F, 0.0F}, 3);
    REQUIRE(planar_speed(w, ent) == Catch::Approx(0.0F).margin(1.0e-3F));

    run_ticks(w, ent, vec3f{0.0F, 0.0F, 0.0F}, 1);
    REQUIRE(planar_speed(w, ent) == 0.0F);
}

TEST_CASE("a character turns to its facing at a constant rate", "[world][character]") {
    world w;
    const auto ent = make_character(w);
    const vec3f facing{1.0F, 0.0F, 0.0F};

    w.system<character_controller_system>()
        .modify(ent)
        .set_facing_direction(facing)
        .set_turn_degrees_per_second(600.0F);

    REQUIRE(degrees_left(w, ent, facing) == Catch::Approx(90.0F).margin(0.1F));

    w.update(tick_seconds);
    REQUIRE(degrees_left(w, ent, facing) == Catch::Approx(60.0F).margin(0.1F));

    w.update(tick_seconds);
    REQUIRE(degrees_left(w, ent, facing) == Catch::Approx(30.0F).margin(0.1F));

    w.update(tick_seconds);
    REQUIRE(degrees_left(w, ent, facing) == Catch::Approx(0.0F).margin(0.1F));
}

TEST_CASE("a character does not turn past its facing", "[world][character]") {
    world w;
    const auto ent = make_character(w);
    const vec3f facing{1.0F, 0.0F, 1.0F};

    w.system<character_controller_system>()
        .modify(ent)
        .set_facing_direction(facing)
        .set_turn_degrees_per_second(900.0F);

    w.update(1.0F);
    REQUIRE(degrees_left(w, ent, facing) == Catch::Approx(0.0F).margin(0.1F));

    w.update(tick_seconds);
    REQUIRE(degrees_left(w, ent, facing) == Catch::Approx(0.0F).margin(0.1F));
}

TEST_CASE("a half turn takes the time its rate gives", "[world][character]") {
    world w;
    const auto ent = make_character(w);
    const vec3f facing{0.0F, 0.0F, -1.0F};

    w.system<character_controller_system>()
        .modify(ent)
        .set_facing_direction(facing)
        .set_turn_degrees_per_second(900.0F);

    w.update(tick_seconds);
    w.update(tick_seconds);
    REQUIRE(degrees_left(w, ent, facing) == Catch::Approx(90.0F).margin(0.5F));

    w.update(tick_seconds);
    w.update(tick_seconds);
    REQUIRE(degrees_left(w, ent, facing) == Catch::Approx(0.0F).margin(0.5F));
}
