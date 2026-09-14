#include <catch2/catch_test_macros.hpp>


import vw.core;

using namespace vw;

TEST_CASE("transform default state", "[transform]") {
    transform t;
    REQUIRE(t.get_position() == vec3f{0.0f, 0.0f, 0.0f});
    REQUIRE(t.get_rotation() == quat{});
    REQUIRE(t.get_scale() == vec3f{1.0f, 1.0f, 1.0f});
}

TEST_CASE("transform setters and getters", "[transform]") {
    transform t;

    t.set_position(vec3f{1.0f, 2.0f, 3.0f});
    REQUIRE(t.get_position() == vec3f{1.0f, 2.0f, 3.0f});

    quat rot = math::euler_to_quat(vec3f{0.1f, 0.2f, 0.3f});
    t.set_rotation(rot);
    REQUIRE(math::approx_equal(t.get_rotation(), rot));

    t.set_rotation_euler(vec3f{0.1f, 0.2f, 0.3f});
    REQUIRE(math::approx_equal(t.get_rotation(), math::euler_to_quat(vec3f{0.1f, 0.2f, 0.3f})));

    auto euler_back = t.get_rotation_euler();
    REQUIRE(math::approx_equal(euler_back, vec3f{0.1f, 0.2f, 0.3f}));

    t.set_scale(vec3f{2.0f, 3.0f, 4.0f});
    REQUIRE(t.get_scale() == vec3f{2.0f, 3.0f, 4.0f});
}

TEST_CASE("transform translate", "[transform]") {
    transform t;
    t.translate(vec3f{1.0f, 2.0f, 3.0f});
    REQUIRE(t.get_position() == vec3f{1.0f, 2.0f, 3.0f});

    t.translate(vec3f{-0.5f, 0.5f, 0.0f});
    REQUIRE(t.get_position() == vec3f{0.5f, 2.5f, 3.0f});
}

TEST_CASE("transform rotate", "[transform]") {
    transform t;
    t.rotate(vec3f{0.1f, 0.2f, 0.3f});
    auto expected = math::euler_to_quat(vec3f{0.1f, 0.2f, 0.3f});
    REQUIRE(math::approx_equal(t.get_rotation(), expected));
}

TEST_CASE("transform scale", "[transform]") {
    transform t;
    REQUIRE(t.get_scale() == vec3f{1.0f, 1.0f, 1.0f});

    t.scale(vec3f{2.0f, 3.0f, 4.0f});
    REQUIRE(t.get_scale() == vec3f{2.0f, 3.0f, 4.0f});

    t.scale(vec3f{0.5f, 0.5f, 0.5f});
    REQUIRE(t.get_scale() == vec3f{1.0f, 1.5f, 2.0f});
}

TEST_CASE("transform calc_matrix", "[transform]") {
    SECTION("default transform produces identity") {
        transform t;
        auto m = t.calc_matrix();
        REQUIRE(math::approx_equal(m, math::identity_matrix()));
    }

    SECTION("translation only") {
        transform t;
        t.set_position(vec3f{1.0f, 2.0f, 3.0f});
        auto m = t.calc_matrix();
        auto expected = math::translation_matrix(vec3f{1.0f, 2.0f, 3.0f});
        REQUIRE(math::approx_equal(m, expected));
    }

    SECTION("scale only") {
        transform t;
        t.set_scale(vec3f{2.0f, 3.0f, 4.0f});
        auto m = t.calc_matrix();
        auto expected = math::scale_matrix(vec3f{2.0f, 3.0f, 4.0f});
        REQUIRE(math::approx_equal(m, expected));
    }

    SECTION("rotation_matrix(quat) matches rotation_matrix(euler)") {
        vec3f euler = {0.1f, 0.2f, 0.3f};
        auto m_euler = math::rotation_matrix(euler);
        auto q = math::euler_to_quat(euler);
        auto m_quat = math::rotation_matrix(q);
        REQUIRE(math::approx_equal(m_euler, m_quat));
    }

    SECTION("matches transform_matrix with euler") {
        transform t;
        t.set_position(vec3f{1.0f, 2.0f, 3.0f});
        t.set_rotation_euler(vec3f{0.1f, 0.2f, 0.3f});
        t.set_scale(vec3f{1.5f, 1.5f, 1.5f});

        auto m = t.calc_matrix();
        auto expected = math::transform_matrix(
            vec3f{1.0f, 2.0f, 3.0f},
            vec3f{0.1f, 0.2f, 0.3f},
            vec3f{1.5f, 1.5f, 1.5f}
        );
        REQUIRE(math::approx_equal(m, expected));
    }
}
