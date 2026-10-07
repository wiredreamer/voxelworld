#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

import std;

import vw.core;
import vw.gfx;

using namespace vw;

TEST_CASE("a display colour survives the trip into the scene and back", "[tonemap]") {
    const gfx::tonemap_settings curves[] = {
        {},
        {.exposure = 0.4f, .white_point = 0.5f},
        {.exposure = 3.5f, .white_point = 4.0f},
    };

    for (const auto& curve : curves) {
        for (const float32 level : {0.0f, 0.02f, 0.18f, 0.5f, 0.9f, 1.0f}) {
            const vec3f display{level, level * 0.5f, 1.0f - level};

            const vec3f back =
                gfx::display_from_scene(gfx::scene_from_display(display, curve), curve);

            CHECK(back.x == Catch::Approx(display.x).margin(1e-4));
            CHECK(back.y == Catch::Approx(display.y).margin(1e-4));
            CHECK(back.z == Catch::Approx(display.z).margin(1e-4));
        }
    }
}

TEST_CASE("the white point is the scene level that lands on display white", "[tonemap]") {
    const gfx::tonemap_settings curve{};

    const float32 scene_white = curve.white_point / curve.exposure;
    const vec3f shown = gfx::display_from_scene(vec3f{scene_white, 0.0f, 0.0f}, curve);

    CHECK(shown.x == Catch::Approx(1.0f).margin(1e-5));
    CHECK(shown.y == 0.0f);
}
