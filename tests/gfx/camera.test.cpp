#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

import std;

import vw.core;
import vw.gfx;

using namespace vw;

TEST_CASE("the picking ray spans the whole view depth", "[camera]") {
    constexpr float32 near_plane = 0.1F;
    constexpr float32 far_plane  = 1000.0F;

    gfx::camera cam{60.0F, 16.0F / 9.0F, near_plane, far_plane};
    cam.set_position(vec3f{0.0F, 0.0F, 0.0F});
    cam.set_rotation(0.0F, 0.0F);

    const vec2i size{1600, 900};
    const auto r = cam.screen_to_world_ray(vec2d{size.x / 2.0, size.y / 2.0}, size);

    REQUIRE(r.length() == Catch::Approx(far_plane - near_plane).epsilon(0.01));
    REQUIRE(math::length(r.start) == Catch::Approx(near_plane).epsilon(0.01));
    REQUIRE(math::dot(r.direction, cam.get_forward()) == Catch::Approx(1.0F).epsilon(0.001));
}

TEST_CASE("the picking ray reaches a model in front of the camera", "[camera]") {
    gfx::camera cam{60.0F, 1.0F, 0.1F, 1000.0F};
    cam.set_position(vec3f{25.0F, 25.0F, 25.0F});
    cam.set_rotation(-30.0F, -135.0F);

    const vec2i size{1200, 1200};
    const auto r = cam.screen_to_world_ray(vec2d{600.0, 600.0}, size);

    const spatial::aabb box{vec3f{-8.0F, -8.0F, -8.0F}, vec3f{8.0F, 8.0F, 8.0F}};
    float32 t = 0.0F;
    REQUIRE(r.intersects_at(box, t));
}
