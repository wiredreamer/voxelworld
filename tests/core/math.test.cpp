#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>


import vw.core;

using namespace vw;
using Catch::Approx;

TEST_CASE("math constants and conversions", "[math]") {
    SECTION("radians") {
        constexpr float rad90 = math::radians(90.0f);
        REQUIRE(rad90 == Approx(math::pi / 2.0f));
    }

    SECTION("degrees") {
        constexpr float deg = math::degrees(math::pi);
        REQUIRE(deg == Approx(180.0f));
    }

    SECTION("round-trip") {
        constexpr float result = math::degrees(math::radians(45.0f));
        REQUIRE(result == Approx(45.0f));
    }

    SECTION("constexpr") {
        constexpr float rad90 = math::radians(90.0f);
        constexpr float deg90 = math::degrees(rad90);
        static_assert(deg90 > 89.9f && deg90 < 90.1f);
    }
}

TEST_CASE("math clamp", "[math]") {
    SECTION("float") {
        REQUIRE(math::clamp(5.0f, 0.0f, 10.0f) == 5.0f);
        REQUIRE(math::clamp(-5.0f, 0.0f, 10.0f) == 0.0f);
        REQUIRE(math::clamp(15.0f, 0.0f, 10.0f) == 10.0f);
    }

    SECTION("int") {
        REQUIRE(math::clamp(5, 0, 10) == 5);
        REQUIRE(math::clamp(-5, 0, 10) == 0);
        REQUIRE(math::clamp(15, 0, 10) == 10);
    }

    SECTION("double") {
        REQUIRE(math::clamp(5.0, 0.0, 10.0) == 5.0);
    }

    SECTION("vec3f") {
        auto r = math::clamp(
            vec3f{-1.0f, 5.0f, 15.0f},
            vec3f{0.0f, 0.0f, 0.0f},
            vec3f{10.0f, 10.0f, 10.0f}
        );
        REQUIRE(r.x == 0.0f);
        REQUIRE(r.y == 5.0f);
        REQUIRE(r.z == 10.0f);
    }
}

TEST_CASE("math sign", "[math]") {
    SECTION("float") {
        REQUIRE(math::sign(-10.0f) == -1.0f);
        REQUIRE(math::sign(0.0f) == 0.0f);
        REQUIRE(math::sign(10.0f) == 1.0f);
    }

    SECTION("int") {
        REQUIRE(math::sign(-5) == -1);
        REQUIRE(math::sign(0) == 0);
        REQUIRE(math::sign(5) == 1);
    }

    SECTION("vec3f") {
        auto r = math::sign(vec3f{-5.0f, 0.0f, 3.0f});
        REQUIRE(r == vec3f{-1.0f, 0.0f, 1.0f});
    }
}

TEST_CASE("math vec2 functions", "[math][vec2]") {
    SECTION("length") {
        REQUIRE(math::length(vec2f{3.0f, 4.0f}) == Approx(5.0f));
    }

    SECTION("length_squared") {
        REQUIRE(math::length_squared(vec2f{3.0f, 4.0f}) == Approx(25.0f));
    }

    SECTION("normalize") {
        auto n = math::normalize(vec2f{3.0f, 4.0f});
        REQUIRE(n.x == Approx(0.6f));
        REQUIRE(n.y == Approx(0.8f));
        REQUIRE(math::length(n) == Approx(1.0f));
    }

    SECTION("dot") {
        REQUIRE(math::dot(vec2f{1.0f, 0.0f}, vec2f{0.0f, 1.0f}) == Approx(0.0f));
        REQUIRE(math::dot(vec2f{3.0f, 4.0f}, vec2f{1.0f, 0.0f}) == Approx(3.0f));
    }

    SECTION("cross") {
        REQUIRE(math::cross(vec2f{1.0f, 0.0f}, vec2f{0.0f, 1.0f}) == Approx(1.0f));
    }

    SECTION("lerp") {
        auto r = math::lerp(vec2f{0.0f, 0.0f}, vec2f{10.0f, 10.0f}, 0.5f);
        REQUIRE(r.x == Approx(5.0f));
        REQUIRE(r.y == Approx(5.0f));
    }

    SECTION("min/max") {
        auto mn = math::min(vec2f{1.0f, 5.0f}, vec2f{3.0f, 2.0f});
        REQUIRE(mn == vec2f{1.0f, 2.0f});

        auto mx = math::max(vec2f{1.0f, 5.0f}, vec2f{3.0f, 2.0f});
        REQUIRE(mx == vec2f{3.0f, 5.0f});
    }

    SECTION("abs") {
        auto r = math::abs(vec2f{-1.5f, -2.5f});
        REQUIRE(r.x == 1.5f);
        REQUIRE(r.y == 2.5f);
    }

    SECTION("floor/ceil/round") {
        auto fl = math::floor(vec2f{1.7f, -1.3f});
        REQUIRE(fl.x == 1.0f);
        REQUIRE(fl.y == -2.0f);

        auto cl = math::ceil(vec2f{1.1f, -1.9f});
        REQUIRE(cl.x == 2.0f);
        REQUIRE(cl.y == -1.0f);

        auto rn = math::round(vec2f{1.5f, 2.4f});
        REQUIRE(rn.x == 2.0f);
        REQUIRE(rn.y == 2.0f);
    }

    SECTION("fract") {
        auto r = math::fract(vec2f{1.7f, 2.3f});
        REQUIRE(r.x == Approx(0.7f));
        REQUIRE(r.y == Approx(0.3f));
    }

    SECTION("sign") {
        auto r = math::sign(vec2f{-3.0f, 5.0f});
        REQUIRE(r == vec2f{-1.0f, 1.0f});
    }

    SECTION("clamp") {
        auto r = math::clamp(vec2f{-1.0f, 15.0f}, vec2f{0.0f, 0.0f}, vec2f{10.0f, 10.0f});
        REQUIRE(r == vec2f{0.0f, 10.0f});
    }
}

TEST_CASE("math vec3 functions", "[math][vec3]") {
    SECTION("length") {
        REQUIRE(math::length(vec3f{3.0f, 4.0f, 0.0f}) == Approx(5.0f));
    }

    SECTION("length_squared") {
        REQUIRE(math::length_squared(vec3f{1.0f, 2.0f, 2.0f}) == Approx(9.0f));
    }

    SECTION("normalize") {
        auto n = math::normalize(vec3f{3.0f, 0.0f, 0.0f});
        REQUIRE(n.x == Approx(1.0f));
        REQUIRE(n.y == Approx(0.0f));
        REQUIRE(n.z == Approx(0.0f));
        REQUIRE(math::length(n) == Approx(1.0f));
    }

    SECTION("normalize zero vector") {
        auto n = math::normalize(vec3f{0.0f, 0.0f, 0.0f});
        REQUIRE(n == vec3f{0.0f, 0.0f, 0.0f});
    }

    SECTION("cross") {
        auto r = math::cross(vec3f{1.0f, 0.0f, 0.0f}, vec3f{0.0f, 1.0f, 0.0f});
        REQUIRE(r.x == Approx(0.0f));
        REQUIRE(r.y == Approx(0.0f));
        REQUIRE(r.z == Approx(1.0f));
    }

    SECTION("dot") {
        REQUIRE(math::dot(vec3f{1.0f, 0.0f, 0.0f}, vec3f{0.0f, 1.0f, 0.0f}) == Approx(0.0f));
        REQUIRE(math::dot(vec3f{1.0f, 2.0f, 3.0f}, vec3f{1.0f, 1.0f, 1.0f}) == Approx(6.0f));
    }

    SECTION("lerp") {
        auto r = math::lerp(vec3f{0.0f, 0.0f, 0.0f}, vec3f{10.0f, 20.0f, 30.0f}, 0.5f);
        REQUIRE(math::approx_equal(r, vec3f{5.0f, 10.0f, 15.0f}));
    }

    SECTION("min/max") {
        auto mn = math::min(vec3f{1.0f, 5.0f, 3.0f}, vec3f{3.0f, 2.0f, 4.0f});
        REQUIRE(mn == vec3f{1.0f, 2.0f, 3.0f});

        auto mx = math::max(vec3f{1.0f, 5.0f, 3.0f}, vec3f{3.0f, 2.0f, 4.0f});
        REQUIRE(mx == vec3f{3.0f, 5.0f, 4.0f});
    }

    SECTION("abs") {
        auto r = math::abs(vec3f{-1.5f, -2.3f, 3.7f});
        REQUIRE(r.x == 1.5f);
        REQUIRE(r.y == 2.3f);
        REQUIRE(r.z == 3.7f);
    }

    SECTION("floor") {
        auto r = math::floor(vec3f{-1.5f, -2.3f, 3.7f});
        REQUIRE(r.x == -2.0f);
        REQUIRE(r.y == -3.0f);
        REQUIRE(r.z == 3.0f);
    }

    SECTION("ceil") {
        auto r = math::ceil(vec3f{-1.5f, -2.3f, 3.7f});
        REQUIRE(r.x == -1.0f);
        REQUIRE(r.y == -2.0f);
        REQUIRE(r.z == 4.0f);
    }

    SECTION("fract") {
        auto r = math::fract(vec3f{1.7f, 2.3f, -0.5f});
        REQUIRE(r.x == Approx(0.7f));
        REQUIRE(r.y == Approx(0.3f));
        REQUIRE(r.z == Approx(0.5f));
    }

    SECTION("distance") {
        REQUIRE(math::distance(vec3f{0.0f, 0.0f, 0.0f}, vec3f{3.0f, 4.0f, 0.0f}) == Approx(5.0f));
    }

    SECTION("distance_squared") {
        REQUIRE(math::distance_squared(vec3f{0.0f, 0.0f, 0.0f}, vec3f{3.0f, 4.0f, 0.0f}) == Approx(25.0f));
    }

    SECTION("angle") {
        float a = math::angle(vec3f{1.0f, 0.0f, 0.0f}, vec3f{0.0f, 1.0f, 0.0f});
        REQUIRE(a == Approx(math::pi / 2.0f).margin(0.01f));

        float b = math::angle(vec3f{1.0f, 0.0f, 0.0f}, vec3f{1.0f, 0.0f, 0.0f});
        REQUIRE(b == Approx(0.0f).margin(0.01f));
    }

    SECTION("reflect") {
        auto r = math::reflect(vec3f{1.0f, -1.0f, 0.0f}, vec3f{0.0f, 1.0f, 0.0f});
        REQUIRE(math::approx_equal(r.x, 1.0f));
        REQUIRE(math::approx_equal(r.y, 1.0f));
        REQUIRE(math::approx_equal(r.z, 0.0f));
    }

    SECTION("refract") {
        auto incident = math::normalize(vec3f{1.0f, -1.0f, 0.0f});
        vec3f normal{0.0f, 1.0f, 0.0f};
        auto r = math::refract(incident, normal, 1.0f);
        REQUIRE(math::approx_equal(r.x, incident.x, 0.01f));
        REQUIRE(math::approx_equal(r.y, incident.y, 0.01f));
    }

    SECTION("refract total internal reflection") {
        auto incident = math::normalize(vec3f{1.0f, -0.1f, 0.0f});
        vec3f normal{0.0f, 1.0f, 0.0f};
        auto r = math::refract(incident, normal, 10.0f);
        REQUIRE(math::approx_equal(r.y, -incident.y, 0.1f));
    }

    SECTION("project") {
        auto r = math::project(vec3f{3.0f, 4.0f, 0.0f}, vec3f{1.0f, 0.0f, 0.0f});
        REQUIRE(math::approx_equal(r.x, 3.0f));
        REQUIRE(math::approx_equal(r.y, 0.0f));
        REQUIRE(math::approx_equal(r.z, 0.0f));
    }

    SECTION("reject") {
        auto r = math::reject(vec3f{3.0f, 4.0f, 0.0f}, vec3f{1.0f, 0.0f, 0.0f});
        REQUIRE(math::approx_equal(r.x, 0.0f));
        REQUIRE(math::approx_equal(r.y, 4.0f));
        REQUIRE(math::approx_equal(r.z, 0.0f));
    }

    SECTION("project + reject == original") {
        vec3f v{3.0f, 4.0f, 5.0f};
        vec3f onto{1.0f, 0.0f, 0.0f};
        auto p = math::project(v, onto);
        auto r = math::reject(v, onto);
        REQUIRE(math::approx_equal(p + r, v));
    }
}

TEST_CASE("math vec4 functions", "[math][vec4]") {
    SECTION("length") {
        REQUIRE(math::length(vec4f{1.0f, 2.0f, 2.0f, 0.0f}) == Approx(3.0f));
    }

    SECTION("normalize") {
        auto n = math::normalize(vec4f{1.0f, 2.0f, 2.0f, 0.0f});
        REQUIRE(math::length(n) == Approx(1.0f));
    }

    SECTION("dot") {
        REQUIRE(math::dot(vec4f{1.0f, 0.0f, 0.0f, 0.0f}, vec4f{0.0f, 1.0f, 0.0f, 0.0f}) == Approx(0.0f));
    }

    SECTION("lerp") {
        auto r = math::lerp(vec4f{0.0f, 0.0f, 0.0f, 0.0f}, vec4f{10.0f, 20.0f, 30.0f, 40.0f}, 0.5f);
        REQUIRE(r.x == Approx(5.0f));
        REQUIRE(r.y == Approx(10.0f));
        REQUIRE(r.z == Approx(15.0f));
        REQUIRE(r.w == Approx(20.0f));
    }

    SECTION("min/max") {
        auto mn = math::min(vec4f{1.0f, 5.0f, 3.0f, 7.0f}, vec4f{3.0f, 2.0f, 4.0f, 1.0f});
        REQUIRE(mn == vec4f{1.0f, 2.0f, 3.0f, 1.0f});
    }

    SECTION("abs/floor/ceil") {
        auto a = math::abs(vec4f{-1.0f, -2.0f, 3.0f, -4.0f});
        REQUIRE(a == vec4f{1.0f, 2.0f, 3.0f, 4.0f});

        auto f = math::floor(vec4f{1.7f, -1.3f, 2.0f, 0.5f});
        REQUIRE(f == vec4f{1.0f, -2.0f, 2.0f, 0.0f});
    }

    SECTION("clamp") {
        auto r = math::clamp(
            vec4f{-1.0f, 5.0f, 15.0f, 0.5f},
            vec4f{0.0f, 0.0f, 0.0f, 0.0f},
            vec4f{10.0f, 10.0f, 10.0f, 1.0f}
        );
        REQUIRE(r == vec4f{0.0f, 5.0f, 10.0f, 0.5f});
    }
}

TEST_CASE("math approx_equal", "[math]") {
    SECTION("float") {
        REQUIRE(math::approx_equal(1.0f, 1.000001f));
        REQUIRE_FALSE(math::approx_equal(1.0f, 1.1f));
    }

    SECTION("vec2f") {
        REQUIRE(math::approx_equal(vec2f{1.0f, 2.0f}, vec2f{1.000001f, 2.000001f}));
        REQUIRE_FALSE(math::approx_equal(vec2f{1.0f, 2.0f}, vec2f{1.1f, 2.0f}));
    }

    SECTION("vec3f") {
        REQUIRE(math::approx_equal(vec3f{1.0f, 2.0f, 3.0f}, vec3f{1.000001f, 2.000001f, 3.000001f}));
        REQUIRE_FALSE(math::approx_equal(vec3f{1.0f, 2.0f, 3.0f}, vec3f{1.0f, 2.0f, 3.1f}));
    }

    SECTION("vec4f") {
        REQUIRE(math::approx_equal(
            vec4f{1.0f, 2.0f, 3.0f, 4.0f},
            vec4f{1.000001f, 2.000001f, 3.000001f, 4.000001f}
        ));
    }

    SECTION("mat4f") {
        auto m1 = math::identity_matrix();
        auto m2 = math::identity_matrix();
        m2[0] = 1.000001f;
        REQUIRE(math::approx_equal(m1, m2));
    }

    SECTION("quat") {
        quat q1{0.0f, 0.0f, 0.0f, 1.0f};
        quat q2{0.000001f, 0.000001f, 0.000001f, 1.000001f};
        REQUIRE(math::approx_equal(q1, q2));
    }
}

TEST_CASE("math scalar lerp", "[math]") {
    REQUIRE(math::lerp(0.0f, 10.0f, 0.0f) == Approx(0.0f));
    REQUIRE(math::lerp(0.0f, 10.0f, 0.5f) == Approx(5.0f));
    REQUIRE(math::lerp(0.0f, 10.0f, 1.0f) == Approx(10.0f));
}

TEST_CASE("math uint8 lerp", "[math]") {
    REQUIRE(math::lerp(static_cast<uint8>(0), static_cast<uint8>(255), 0.5f) == 127);
}

TEST_CASE("math smoothstep", "[math]") {
    SECTION("scalar") {
        REQUIRE(math::smoothstep(0.0f, 1.0f, 0.0f) == Approx(0.0f));
        REQUIRE(math::smoothstep(0.0f, 1.0f, 1.0f) == Approx(1.0f));
        REQUIRE(math::smoothstep(0.0f, 1.0f, 0.5f) == Approx(0.5f).margin(0.01f));
        REQUIRE(math::smoothstep(0.0f, 1.0f, -1.0f) == Approx(0.0f));
        REQUIRE(math::smoothstep(0.0f, 1.0f, 2.0f) == Approx(1.0f));
    }

    SECTION("vec3f") {
        auto r = math::smoothstep(
            vec3f{0.0f, 0.0f, 0.0f},
            vec3f{1.0f, 2.0f, 4.0f},
            vec3f{0.5f, 1.0f, 2.0f}
        );
        REQUIRE(r.x == Approx(0.5f).margin(0.01f));
        REQUIRE(r.y == Approx(0.5f).margin(0.01f));
        REQUIRE(r.z == Approx(0.5f).margin(0.01f));
    }
}

TEST_CASE("math mix", "[math]") {
    REQUIRE(math::mix(0.0f, 10.0f, 0.5f) == Approx(5.0f));

    auto v = math::mix(vec3f{0.0f, 0.0f, 0.0f}, vec3f{10.0f, 20.0f, 30.0f}, 0.5f);
    REQUIRE(v.x == Approx(5.0f));
    REQUIRE(v.y == Approx(10.0f));
    REQUIRE(v.z == Approx(15.0f));
}

TEST_CASE("math easing functions", "[math]") {
    vec3f a{0.0f, 0.0f, 0.0f};
    vec3f b{10.0f, 10.0f, 10.0f};

    SECTION("ease_in boundaries") {
        REQUIRE(math::approx_equal(math::ease_in(a, b, 0.0f), a));
        REQUIRE(math::approx_equal(math::ease_in(a, b, 1.0f), b));
    }

    SECTION("ease_out boundaries") {
        REQUIRE(math::approx_equal(math::ease_out(a, b, 0.0f), a));
        REQUIRE(math::approx_equal(math::ease_out(a, b, 1.0f), b));
    }

    SECTION("ease_in_out boundaries") {
        REQUIRE(math::approx_equal(math::ease_in_out(a, b, 0.0f), a));
        REQUIRE(math::approx_equal(math::ease_in_out(a, b, 1.0f), b));
    }

    SECTION("apply_easing") {
        REQUIRE(math::apply_easing(0.0f, math::interpolation_type::linear) == 0.0f);
        REQUIRE(math::apply_easing(1.0f, math::interpolation_type::linear) == 1.0f);

        REQUIRE(math::apply_easing(0.5f, math::interpolation_type::step) == 0.0f);
        REQUIRE(math::apply_easing(1.0f, math::interpolation_type::step) == 1.0f);

        REQUIRE(math::apply_easing(0.0f, math::interpolation_type::ease_in) == 0.0f);
        REQUIRE(math::apply_easing(1.0f, math::interpolation_type::ease_in) == 1.0f);
    }
}

TEST_CASE("math cubic_bezier", "[math]") {
    SECTION("evaluate_cubic_bezier boundaries") {
        REQUIRE(math::evaluate_cubic_bezier(0.0f, 0.0f, 0.25f, 0.75f, 1.0f) == Approx(0.0f));
        REQUIRE(math::evaluate_cubic_bezier(1.0f, 0.0f, 0.25f, 0.75f, 1.0f) == Approx(1.0f));
    }

    SECTION("cubic_bezier vec3") {
        vec3f a{0.0f, 0.0f, 0.0f};
        vec3f b{10.0f, 10.0f, 10.0f};
        auto r0 = math::cubic_bezier(a, b, 0.0f, 0.25f, 0.75f);
        auto r1 = math::cubic_bezier(a, b, 1.0f, 0.25f, 0.75f);
        REQUIRE(math::approx_equal(r0, a));
        REQUIRE(math::approx_equal(r1, b));
    }
}

TEST_CASE("math interpolate vec3", "[math]") {
    vec3f a{0.0f, 0.0f, 0.0f};
    vec3f b{10.0f, 10.0f, 10.0f};

    SECTION("linear") {
        auto r = math::interpolate(a, b, 0.5f, math::interpolation_type::linear);
        REQUIRE(math::approx_equal(r, vec3f{5.0f, 5.0f, 5.0f}));
    }

    SECTION("step") {
        auto r0 = math::interpolate(a, b, 0.5f, math::interpolation_type::step);
        REQUIRE(math::approx_equal(r0, a));
        auto r1 = math::interpolate(a, b, 1.0f, math::interpolation_type::step);
        REQUIRE(math::approx_equal(r1, b));
    }
}

TEST_CASE("math interpolate color", "[math]") {
    SECTION("step") {
        auto r = math::interpolate(colors::red_6, colors::blue_6, 0.5f, math::interpolation_type::step);
        REQUIRE(r == colors::red_6);
        auto r2 = math::interpolate(colors::red_6, colors::blue_6, 1.0f, math::interpolation_type::step);
        REQUIRE(r2 == colors::blue_6);
    }

    SECTION("linear midpoint") {
        auto r = math::interpolate(colors::black, colors::white, 0.5f, math::interpolation_type::linear);
        REQUIRE(r.r() == 127);
        REQUIRE(r.g() == 127);
        REQUIRE(r.b() == 127);
    }
}

TEST_CASE("math quaternion functions", "[math][quat]") {
    SECTION("dot") {
        quat a{0.0f, 0.0f, 0.0f, 1.0f};
        REQUIRE(math::dot(a, a) == Approx(1.0f));
    }

    SECTION("normalize") {
        quat q{1.0f, 2.0f, 3.0f, 4.0f};
        auto n = math::normalize(q);
        float len = std::sqrt(math::dot(n, n));
        REQUIRE(len == Approx(1.0f));
    }

    SECTION("slerp boundaries") {
        quat a{0.0f, 0.0f, 0.0f, 1.0f};
        quat b = math::normalize(quat{0.0f, 0.707f, 0.0f, 0.707f});

        auto r0 = math::slerp(a, b, 0.0f);
        REQUIRE(math::approx_equal(r0, a));

        auto r1 = math::slerp(a, b, 1.0f);
        REQUIRE(math::approx_equal(r1, b, 0.01f));
    }

    SECTION("slerp nearly parallel") {
        quat a{0.0f, 0.0f, 0.0f, 1.0f};
        quat b{0.0001f, 0.0f, 0.0f, 0.99999999f};
        auto r = math::slerp(a, b, 0.5f);
        REQUIRE(math::approx_equal(r.w, 1.0f, 0.01f));
    }

    SECTION("euler round-trip") {
        vec3f euler{0.1f, 0.2f, 0.3f};
        auto q = math::euler_to_quat(euler);
        auto back = math::quat_to_euler(q);
        REQUIRE(math::approx_equal(euler, back, 0.01f));
    }
}

TEST_CASE("math matrix functions", "[math][mat4]") {
    SECTION("identity") {
        auto id = math::identity_matrix();
        REQUIRE(id[0, 0] == 1.0f);
        REQUIRE(id[1, 1] == 1.0f);
        REQUIRE(id[2, 2] == 1.0f);
        REQUIRE(id[3, 3] == 1.0f);
        REQUIRE(id[0, 1] == 0.0f);
    }

    SECTION("translation") {
        auto t = math::translation_matrix(vec3f{1.0f, 2.0f, 3.0f});
        REQUIRE(t[0, 3] == 1.0f);
        REQUIRE(t[1, 3] == 2.0f);
        REQUIRE(t[2, 3] == 3.0f);
        REQUIRE(t[0, 0] == 1.0f);
    }

    SECTION("scale") {
        auto s = math::scale_matrix(vec3f{2.0f, 3.0f, 4.0f});
        REQUIRE(s[0, 0] == 2.0f);
        REQUIRE(s[1, 1] == 3.0f);
        REQUIRE(s[2, 2] == 4.0f);
    }

    SECTION("rotation_matrix_x") {
        auto r = math::rotation_matrix_x(0.0f);
        REQUIRE(math::approx_equal(r, math::identity_matrix()));
    }

    SECTION("rotation_matrix_y") {
        auto r = math::rotation_matrix_y(0.0f);
        REQUIRE(math::approx_equal(r, math::identity_matrix()));
    }

    SECTION("rotation_matrix_z") {
        auto r = math::rotation_matrix_z(0.0f);
        REQUIRE(math::approx_equal(r, math::identity_matrix()));
    }

    SECTION("rotation combined zero") {
        auto r = math::rotation_matrix(vec3f{0.0f, 0.0f, 0.0f});
        REQUIRE(math::approx_equal(r, math::identity_matrix()));
    }

    SECTION("transpose") {
        auto t = math::translation_matrix(vec3f{1.0f, 2.0f, 3.0f});
        auto tr = math::transpose_matrix(t);
        REQUIRE(tr[3, 0] == t[0, 3]);
        REQUIRE(tr[3, 1] == t[1, 3]);
        REQUIRE(tr[3, 2] == t[2, 3]);

        auto tr2 = math::transpose_matrix(tr);
        REQUIRE(math::approx_equal(tr2, t));
    }

    SECTION("inverse") {
        auto t = math::translation_matrix(vec3f{1.0f, 2.0f, 3.0f});
        auto inv = math::inverse_matrix(t);
        REQUIRE(inv.has_value());
        auto result = t * inv.value();
        REQUIRE(math::approx_equal(result, math::identity_matrix()));
    }

    SECTION("inverse of identity") {
        auto inv = math::inverse_matrix(math::identity_matrix());
        REQUIRE(inv.has_value());
        REQUIRE(math::approx_equal(inv.value(), math::identity_matrix()));
    }

    SECTION("inverse zero determinant") {
        mat4f zero;
        auto inv = math::inverse_matrix(zero);
        REQUIRE_FALSE(inv.has_value());
        REQUIRE(inv.error() == math::error_type::zero_determinant);
    }

    SECTION("transform_matrix") {
        auto m = math::transform_matrix(
            vec3f{0.0f, 0.0f, 0.0f},
            vec3f{0.0f, 0.0f, 0.0f},
            vec3f{1.0f, 1.0f, 1.0f}
        );
        REQUIRE(math::approx_equal(m, math::identity_matrix()));
    }
}

TEST_CASE("math perspective_matrix", "[math][mat4]") {
    auto p = math::perspective_matrix(90.0f, 1.0f, 0.1f, 100.0f);
    REQUIRE(p[3, 2] == -1.0f);
    REQUIRE(p[3, 3] == 0.0f);
}

TEST_CASE("math perspective_matrix_reversed", "[math][mat4]") {
    constexpr float near = 0.1f;
    constexpr float far  = 100.0f;

    auto p = math::perspective_matrix_reversed(90.0f, 1.0f, near, far);

    auto depth_at = [&p](float z_eye) {
        auto clip = p * vec4f{0.0f, 0.0f, z_eye, 1.0f};
        return clip.z / clip.w;
    };

    SECTION("near maps to one and far to zero") {
        REQUIRE(depth_at(-near) == Approx(1.0f).margin(1e-4f));
        REQUIRE(depth_at(-far) == Approx(0.0f).margin(1e-4f));
    }

    SECTION("depth decreases with distance") {
        REQUIRE(depth_at(-1.0f) > depth_at(-10.0f));
        REQUIRE(depth_at(-10.0f) > depth_at(-50.0f));
    }
}

TEST_CASE("math orthographic_matrix", "[math][mat4]") {
    auto o = math::orthographic_matrix(-1.0f, 1.0f, -1.0f, 1.0f, 0.1f, 100.0f);
    REQUIRE(o[0, 0] == Approx(1.0f));
    REQUIRE(o[1, 1] == Approx(1.0f));
}

TEST_CASE("math orthographic_matrix_reversed", "[math][mat4]") {
    constexpr float near = 0.1f;
    constexpr float far  = 100.0f;

    auto o = math::orthographic_matrix_reversed(8.0f, 4.0f, near, far);

    auto projected = [&o](const vec3f& in_view) {
        return o * vec4f{in_view.x, in_view.y, in_view.z, 1.0f};
    };

    SECTION("near maps to one and far to zero") {
        REQUIRE(projected({0.0f, 0.0f, -near}).z == Approx(1.0f).margin(1e-4f));
        REQUIRE(projected({0.0f, 0.0f, -far}).z == Approx(0.0f).margin(1e-4f));
    }

    SECTION("depth falls evenly with distance") {
        const float middle = -(near + far) * 0.5f;
        REQUIRE(projected({0.0f, 0.0f, middle}).z == Approx(0.5f).margin(1e-4f));
    }

    SECTION("nothing is divided by depth") {
        REQUIRE(projected({1.0f, 1.0f, -1.0f}).w == Approx(1.0f));
        REQUIRE(projected({1.0f, 1.0f, -90.0f}).w == Approx(1.0f));
        REQUIRE(projected({1.0f, 1.0f, -1.0f}).x == Approx(projected({1.0f, 1.0f, -90.0f}).x));
    }

    SECTION("the width and the height span the frame, the height upside down") {
        REQUIRE(projected({4.0f, 0.0f, -1.0f}).x == Approx(1.0f));
        REQUIRE(projected({-4.0f, 0.0f, -1.0f}).x == Approx(-1.0f));
        REQUIRE(projected({0.0f, 2.0f, -1.0f}).y == Approx(-1.0f));
        REQUIRE(projected({0.0f, -2.0f, -1.0f}).y == Approx(1.0f));
    }
}

TEST_CASE("math look_at_matrix", "[math][mat4]") {
    SECTION("two-arg overload") {
        auto m = math::look_at_matrix(vec3f{0.0f, 0.0f, 5.0f}, vec3f{0.0f, 0.0f, 0.0f});
        auto eye_in_view = m * vec3f{0.0f, 0.0f, 5.0f};
        REQUIRE(math::approx_equal(eye_in_view, vec3f{0.0f, 0.0f, 0.0f}, 0.01f));
    }

    SECTION("three-arg overload") {
        auto m = math::look_at_matrix(
            vec3f{0.0f, 0.0f, 5.0f},
            vec3f{0.0f, 0.0f, 0.0f},
            vec3f{0.0f, 1.0f, 0.0f}
        );
        auto eye_in_view = m * vec3f{0.0f, 0.0f, 5.0f};
        REQUIRE(math::approx_equal(eye_in_view, vec3f{0.0f, 0.0f, 0.0f}, 0.01f));
    }
}

TEST_CASE("math perpendicular", "[math]") {
    auto p = math::perpendicular(vec3f{0.0f, 0.0f, 0.0f}, vec3f{0.0f, 0.0f, -1.0f});
    auto dir = math::normalize(vec3f{0.0f, 0.0f, -1.0f});
    REQUIRE(std::abs(math::dot(p, dir)) < 0.01f);
    REQUIRE(math::length(p) == Approx(1.0f).margin(0.01f));
}

TEST_CASE("math is_safe_zero", "[math]") {
    REQUIRE(math::is_safe_zero(0.0f));
    REQUIRE(math::is_safe_zero(0.000001f));
    REQUIRE_FALSE(math::is_safe_zero(1.0f));
    REQUIRE_FALSE(math::is_safe_zero(-1.0f));
}
