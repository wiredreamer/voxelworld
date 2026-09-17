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

TEST_CASE("transform from_matrix reverses calc_matrix", "[transform]") {
    const auto round_trips = [](const vec3f& position, const vec3f& euler, const vec3f& scale) {
        transform source;
        source.set_position(position);
        source.set_rotation_euler(euler);
        source.set_scale(scale);

        const auto restored = transform::from_matrix(source.calc_matrix());

        REQUIRE(math::approx_equal(restored.get_position(), position, 1e-4f));
        REQUIRE(math::approx_equal(restored.calc_matrix(), source.calc_matrix(), 1e-4f));
    };

    SECTION("identity") {
        round_trips(vec3f{}, vec3f{}, vec3f{1.0f, 1.0f, 1.0f});
    }

    SECTION("general pose") {
        round_trips(vec3f{1.0f, -2.0f, 3.5f}, vec3f{0.3f, -1.1f, 0.7f}, vec3f{2.0f, 0.5f, 3.0f});
    }

    SECTION("half turns take every branch") {
        round_trips(vec3f{}, vec3f{math::pi, 0.0f, 0.0f}, vec3f{1.0f, 1.0f, 1.0f});
        round_trips(vec3f{}, vec3f{0.0f, math::pi, 0.0f}, vec3f{1.0f, 1.0f, 1.0f});
        round_trips(vec3f{}, vec3f{0.0f, 0.0f, math::pi}, vec3f{1.0f, 1.0f, 1.0f});
    }

    SECTION("a mirrored scale survives") {
        round_trips(vec3f{0.0f, 1.0f, 0.0f}, vec3f{0.2f, 0.4f, 0.0f}, vec3f{-1.0f, 2.0f, 1.0f});
    }
}

TEST_CASE("a node keeps its world pose under a new parent", "[transform]") {
    transform parent;
    parent.set_position(vec3f{10.0f, 0.0f, -4.0f});
    parent.set_rotation_euler(vec3f{0.0f, 0.8f, 0.0f});
    parent.set_scale(vec3f{2.0f, 2.0f, 2.0f});

    transform child;
    child.set_position(vec3f{3.0f, 5.0f, 1.0f});
    child.set_rotation_euler(vec3f{0.4f, 0.0f, -0.2f});

    const auto world_pose = child.calc_matrix();
    const auto inverse    = math::inverse_matrix(parent.calc_matrix());
    REQUIRE(inverse.has_value());

    const auto local = transform::from_matrix(*inverse * world_pose);

    REQUIRE(math::approx_equal(parent.calc_matrix() * local.calc_matrix(), world_pose, 1e-4f));
}
