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

namespace {

auto near_to(const vec3f& left, const vec3f& right, float32 tolerance = 1.0e-3F) -> bool {
    return math::length(left - right) <= tolerance;
}

auto unprojected(const gfx::camera& cam, float32 ndc_x, float32 ndc_y, float32 depth) -> vec3f {
    const auto inverse = math::inverse_matrix(cam.get_view_projection_matrix());
    const vec4f point  = inverse.value_or(math::identity_matrix()) * vec4f{ndc_x, ndc_y, depth, 1.0F};
    return vec3f{point.x / point.w, point.y / point.w, point.z / point.w};
}

}  // namespace

TEST_CASE("the camera keeps its right hand level at any tilt", "[camera]") {
    gfx::camera cam{60.0F, 1.0F, 0.1F, 1000.0F};

    for (const float32 yaw : {0.0F, 37.0F, 180.0F, -135.0F}) {
        for (const float32 pitch : {-60.0F, 0.0F, 45.0F}) {
            cam.set_rotation(pitch, yaw);

            const vec3f crossed =
                math::normalize(math::cross(vec3f{0.0F, 1.0F, 0.0F}, cam.get_forward()));
            CAPTURE(yaw, pitch);
            CHECK(near_to(cam.get_right(), crossed));
        }
    }

    cam.set_rotation(-90.0F, 180.0F);
    CHECK(near_to(cam.get_forward(), vec3f{0.0F, -1.0F, 0.0F}));
    CHECK(near_to(cam.get_right(), vec3f{-1.0F, 0.0F, 0.0F}));
    CHECK(near_to(cam.get_up(), vec3f{0.0F, 0.0F, -1.0F}));

    const mat4f view = cam.get_view_matrix();
    CHECK(std::isfinite(view[0, 0]));
    CHECK(std::isfinite(view[1, 2]));
}

TEST_CASE("an orthographic camera sees along parallel rays", "[camera]") {
    constexpr float32 near_plane = 0.1F;
    constexpr float32 far_plane  = 1000.0F;

    gfx::camera cam{60.0F, 2.0F, near_plane, far_plane};
    cam.set_position(vec3f{0.0F, 0.0F, -100.0F});
    cam.set_rotation(0.0F, 0.0F);
    cam.set_orthographic(40.0F);

    const vec2i size{800, 400};
    const auto centre = cam.screen_to_world_ray(vec2d{400.0, 200.0}, size);
    const auto corner = cam.screen_to_world_ray(vec2d{0.0, 0.0}, size);

    CHECK(centre.length() == Catch::Approx(far_plane - near_plane).epsilon(0.01));
    CHECK(math::dot(centre.direction, cam.get_forward()) == Catch::Approx(1.0F).epsilon(0.001));
    CHECK(math::dot(corner.direction, centre.direction) == Catch::Approx(1.0F).epsilon(0.001));

    const vec3f across = corner.start - centre.start;
    CHECK(math::dot(across, cam.get_right()) == Catch::Approx(-40.0F).epsilon(0.001));
    CHECK(math::dot(across, cam.get_up()) == Catch::Approx(20.0F).epsilon(0.001));
}

TEST_CASE("an orthographic picking ray lands on what is drawn under the cursor", "[camera]") {
    gfx::camera cam{45.0F, 1.5F, 0.1F, 50000.0F};
    cam.set_position(vec3f{40.0F, 30.0F, -200.0F});
    cam.set_rotation(-20.0F, 15.0F);
    cam.set_orthographic(64.0F);

    const vec2i size{1500, 1000};
    const vec3f seen = cam.get_position() + cam.get_forward() * 180.0F + cam.get_right() * 11.0F -
        cam.get_up() * 7.0F;

    const vec4f clip = cam.get_view_projection_matrix() * vec4f{seen.x, seen.y, seen.z, 1.0F};
    const vec2d cursor{
        (static_cast<float64>(clip.x) * 0.5 + 0.5) * size.x,
        (static_cast<float64>(clip.y) * 0.5 + 0.5) * size.y,
    };

    const auto r      = cam.screen_to_world_ray(cursor, size);
    const vec3f along = seen - r.start;
    const vec3f off   = along - r.direction * math::dot(along, r.direction);

    CHECK(math::length(off) < 0.01F);
    CHECK(math::dot(along, r.direction) > 0.0F);
}

TEST_CASE("an orthographic camera draws a thing the same size at any depth", "[camera]") {
    gfx::camera cam{60.0F, 1.0F, 0.1F, 1000.0F};
    cam.set_rotation(0.0F, 0.0F);
    cam.set_orthographic(20.0F);

    const mat4f view_projection = cam.get_view_projection_matrix();
    const vec4f close_by        = view_projection * vec4f{5.0F, 5.0F, 3.0F, 1.0F};
    const vec4f far_off         = view_projection * vec4f{5.0F, 5.0F, 700.0F, 1.0F};

    CHECK(close_by.w == Catch::Approx(1.0F));
    CHECK(close_by.x == Catch::Approx(far_off.x));
    CHECK(close_by.y == Catch::Approx(far_off.y));
    CHECK(std::abs(close_by.x) == Catch::Approx(0.5F));
    CHECK(close_by.z > far_off.z);
}

TEST_CASE("switching the projection keeps what fills the view", "[camera]") {
    gfx::camera cam{45.0F, 1.5F, 0.1F, 1000.0F};
    cam.set_position(vec3f{0.0F, 0.0F, -30.0F});
    cam.set_rotation(0.0F, 0.0F);

    const vec3f subject{0.0F, 0.0F, 0.0F};
    const float32 shown = cam.view_height_at(30.0F);

    CHECK(cam.apparent_distance(subject) == Catch::Approx(30.0F));

    cam.set_orthographic(shown);
    CHECK(cam.is_orthographic());
    CHECK(cam.view_height_at(5.0F) == Catch::Approx(shown));
    CHECK(cam.view_height_at(500.0F) == Catch::Approx(shown));
    CHECK(cam.apparent_distance(subject) == Catch::Approx(30.0F));
    CHECK(cam.view_depth_showing(shown) == Catch::Approx(30.0F));

    cam.set_perspective();
    CHECK_FALSE(cam.is_orthographic());
    CHECK(cam.view_height_at(30.0F) == Catch::Approx(shown));
}

TEST_CASE("the corners of the view are where the projection puts them", "[camera]") {
    gfx::camera cam{60.0F, 16.0F / 9.0F, 0.5F, 200.0F};
    cam.set_position(vec3f{3.0F, 7.0F, -4.0F});
    cam.set_rotation(-25.0F, 40.0F);

    for (const bool flat : {false, true}) {
        if (flat) {
            cam.set_orthographic(12.0F);
        }
        CAPTURE(flat);

        const std::array<vec3f, 8> corners = cam.frustum_corners(0.5F, 200.0F);

        CHECK(near_to(corners[0], unprojected(cam, -1.0F, -1.0F, 1.0F), 0.01F));
        CHECK(near_to(corners[1], unprojected(cam, 1.0F, -1.0F, 1.0F), 0.01F));
        CHECK(near_to(corners[2], unprojected(cam, 1.0F, 1.0F, 1.0F), 0.01F));
        CHECK(near_to(corners[3], unprojected(cam, -1.0F, 1.0F, 1.0F), 0.01F));
        CHECK(near_to(corners[4], unprojected(cam, -1.0F, -1.0F, 0.0F), 0.5F));
        CHECK(near_to(corners[6], unprojected(cam, 1.0F, 1.0F, 0.0F), 0.5F));
    }
}

TEST_CASE("faces are culled from a point in perspective and along a direction without", "[camera]") {
    gfx::camera cam{60.0F, 1.0F, 0.1F, 1000.0F};
    cam.set_position(vec3f{1.0F, 2.0F, 3.0F});
    cam.set_rotation(0.0F, 90.0F);

    const vec4f point = cam.culling_eye();
    CHECK(point.w == 1.0F);
    CHECK(near_to(vec3f{point.x, point.y, point.z}, vec3f{1.0F, 2.0F, 3.0F}));
    CHECK(near_to(cam.direction_to(vec3f{1.0F, 2.0F, 9.0F}), vec3f{0.0F, 0.0F, 1.0F}));

    cam.set_orthographic(10.0F);

    const vec4f direction = cam.culling_eye();
    CHECK(direction.w == 0.0F);
    CHECK(near_to(vec3f{direction.x, direction.y, direction.z}, vec3f{-1.0F, 0.0F, 0.0F}));
    CHECK(near_to(cam.direction_to(vec3f{1.0F, 2.0F, 9.0F}), vec3f{1.0F, 0.0F, 0.0F}));
}

TEST_CASE("coming closer narrows an orthographic view and leaves the camera where it is", "[camera]") {
    gfx::camera cam{60.0F, 1.0F, 0.1F, 1000.0F};
    cam.set_position(vec3f{0.0F, 0.0F, -50.0F});
    cam.set_rotation(0.0F, 0.0F);
    cam.set_orthographic(cam.view_height_at(50.0F));

    const float32 before = cam.get_orthographic_height();
    cam.move_closer(10.0F);

    CHECK(near_to(cam.get_position(), vec3f{0.0F, 0.0F, -50.0F}));
    CHECK(cam.get_orthographic_height() == Catch::Approx(before * 0.8F));

    cam.move_closer(1000.0F);
    CHECK(cam.get_orthographic_height() > 0.0F);

    cam.set_perspective();
    cam.move_closer(10.0F);
    CHECK(near_to(cam.get_position(), vec3f{0.0F, 0.0F, -40.0F}));
}

TEST_CASE("turning about the centre of the view keeps that centre in place", "[camera]") {
    gfx::camera cam{60.0F, 1.0F, 0.1F, 1000.0F};
    cam.set_position(vec3f{0.0F, 0.0F, -20.0F});
    cam.set_rotation(0.0F, 0.0F);
    cam.set_orthographic(cam.view_height_at(20.0F));

    cam.turn_about_view_centre(-30.0F, 90.0F);

    const vec3f centre = cam.get_position() + cam.get_forward() * 20.0F;
    CHECK(near_to(centre, vec3f{0.0F, 0.0F, 0.0F}, 0.01F));
    CHECK(cam.get_yaw() == Catch::Approx(90.0F));
    CHECK(cam.get_pitch() == Catch::Approx(-30.0F));
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
