#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

import std;

import vw.core;
import vw.asset;
import vw.ecs;
import vw.world;

using namespace vw;
using namespace vw::ecs;

namespace {

auto make_node(world& w) -> entity {
    return w.create().with<transform_component>().with<hierarchy_component>().get_entity();
}

auto children_of(world& w, entity ent) -> std::vector<entity> {
    return w.get<hierarchy_component>(ent).get_children();
}

}  // namespace

TEST_CASE("a new parent takes the node away from the old one", "[world][hierarchy]") {
    world w;
    auto& hierarchy = w.system<hierarchy_system>();

    const auto root = make_node(w);
    const auto arm  = make_node(w);
    const auto hand = make_node(w);

    hierarchy.modify(arm).set_parent(root);
    hierarchy.modify(hand).set_parent(root);

    hierarchy.modify(hand).set_parent(arm);

    REQUIRE(children_of(w, root) == std::vector{arm});
    REQUIRE(children_of(w, arm) == std::vector{hand});
    REQUIRE(w.get<hierarchy_component>(hand).get_parent() == arm);
}

TEST_CASE("a child can be put at a given place among its siblings", "[world][hierarchy]") {
    world w;
    auto& hierarchy = w.system<hierarchy_system>();

    const auto root  = make_node(w);
    const auto first = make_node(w);
    const auto last  = make_node(w);
    const auto moved = make_node(w);

    hierarchy.modify(first).set_parent(root);
    hierarchy.modify(last).set_parent(root);
    hierarchy.modify(moved).set_parent(root);

    SECTION("in front") {
        hierarchy.modify(moved).set_parent(root, 0);
        REQUIRE(children_of(w, root) == std::vector{moved, first, last});
    }

    SECTION("in the middle") {
        hierarchy.modify(moved).set_parent(root, 1);
        REQUIRE(children_of(w, root) == std::vector{first, moved, last});
    }

    SECTION("past the end") {
        hierarchy.modify(first).set_parent(root, 99);
        REQUIRE(children_of(w, root) == std::vector{last, moved, first});
    }
}

TEST_CASE("moving the root reaches every level of a deep chain", "[world][hierarchy]") {
    world w;
    auto& hierarchy = w.system<hierarchy_system>();
    auto& transform = w.system<transform_system>();

    constexpr std::size_t chain_length = 8;
    constexpr float32 step             = 2.0F;

    std::vector<entity> chain;
    chain.reserve(chain_length);
    for (std::size_t i = 0; i < chain_length; ++i) {
        const auto node = make_node(w);
        if (i > 0) {
            hierarchy.modify(node).set_parent(chain.back());
        }
        transform.modify(node).set_position({0.0F, step, 0.0F});
        chain.push_back(node);
    }

    w.update(0.0F);

    transform.modify(chain.front()).set_position({10.0F, step, 0.0F});
    w.update(0.0F);

    for (std::size_t i = 0; i < chain_length; ++i) {
        const auto world_matrix = w.get<transform_component>(chain[i]).get_world_matrix();
        const auto at           = world_matrix * vec3f{0.0F, 0.0F, 0.0F};

        REQUIRE(at.x == Catch::Approx(10.0F));
        REQUIRE(at.y == Catch::Approx(step * static_cast<float32>(i + 1)));
    }
}

TEST_CASE("a node moved after its parent in the same frame stays correct", "[world][hierarchy]") {
    world w;
    auto& hierarchy = w.system<hierarchy_system>();
    auto& transform = w.system<transform_system>();

    const auto root   = make_node(w);
    const auto middle = make_node(w);
    const auto leaf   = make_node(w);

    hierarchy.modify(middle).set_parent(root);
    hierarchy.modify(leaf).set_parent(middle);

    transform.modify(root).set_position({1.0F, 0.0F, 0.0F});
    transform.modify(middle).set_position({0.0F, 1.0F, 0.0F});
    transform.modify(leaf).set_position({0.0F, 0.0F, 1.0F});
    w.update(0.0F);

    transform.modify(root).set_position({5.0F, 0.0F, 0.0F});
    transform.modify(middle).set_position({0.0F, 3.0F, 0.0F});
    w.update(0.0F);

    const auto leaf_at = w.get<transform_component>(leaf).get_world_matrix() *
        vec3f{0.0F, 0.0F, 0.0F};

    REQUIRE(leaf_at.x == Catch::Approx(5.0F));
    REQUIRE(leaf_at.y == Catch::Approx(3.0F));
    REQUIRE(leaf_at.z == Catch::Approx(1.0F));
}
