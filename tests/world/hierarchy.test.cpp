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

// Перенос под другого родителя обязан убрать узел из прежнего списка детей:
// иначе он числился бы у двух родителей, и обход дерева проходил бы его дважды.
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

    // Место считается по списку без самого узла, а слишком большое означает «в
    // конец»: так отмена переноса ставит узел туда, где он стоял.
    SECTION("past the end") {
        hierarchy.modify(first).set_parent(root, 99);
        REQUIRE(children_of(w, root) == std::vector{last, moved, first});
    }
}
