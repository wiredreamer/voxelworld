#include <catch2/catch_test_macros.hpp>

#include <memory>
#include <vector>

import vw.core;
import vw.ecs;

using namespace vw;
using namespace vw::ecs;

namespace {

struct position_component {
    float x = 0.f, y = 0.f, z = 0.f;
};

struct velocity_component {
    float dx = 0.f, dy = 0.f, dz = 0.f;
};

struct health_component {
    int hp = 100;
};

}  // namespace

TEST_CASE("registry create and destroy", "[registry]") {
    registry reg;

    auto e0 = reg.create();
    auto e1 = reg.create();
    REQUIRE(e0.index == 0);
    REQUIRE(e1.index == 1);

    reg.destroy(e0);
    auto e2 = reg.create();
    REQUIRE(e2.index == 0);
    REQUIRE(e2.generation == 1);
}

TEST_CASE("registry add and get components", "[registry]") {
    registry reg;
    auto e = reg.create();

    reg.add<position_component>(e, {1.f, 2.f, 3.f});
    REQUIRE(reg.has<position_component>(e));
    REQUIRE(reg.get<position_component>(e).x == 1.f);

    REQUIRE_FALSE(reg.has<velocity_component>(e));
}

TEST_CASE("registry emplace constructs in place", "[registry]") {
    registry reg;
    auto e = reg.create();

    auto& health = reg.emplace<health_component>(e, 55);
    REQUIRE(health.hp == 55);
    REQUIRE(reg.get<health_component>(e).hp == 55);
}

TEST_CASE("registry remove components", "[registry]") {
    registry reg;
    auto e = reg.create();

    reg.add<position_component>(e);
    reg.remove<position_component>(e);
    REQUIRE_FALSE(reg.has<position_component>(e));
}

TEST_CASE("registry remove_all", "[registry]") {
    registry reg;
    auto e = reg.create();

    reg.add<position_component>(e);
    reg.add<velocity_component>(e);
    reg.add<health_component>(e);

    reg.remove_all(e);

    REQUIRE_FALSE(reg.has<position_component>(e));
    REQUIRE_FALSE(reg.has<velocity_component>(e));
    REQUIRE_FALSE(reg.has<health_component>(e));
}

TEST_CASE("registry has is false for stale handles", "[registry]") {
    registry reg;
    auto e = reg.create();
    reg.add<position_component>(e);

    reg.destroy(e);

    REQUIRE_FALSE(reg.has<position_component>(e));
    REQUIRE_FALSE(reg.alive(e));
}

TEST_CASE("registry batch_create", "[registry]") {
    registry reg;

    auto entities = reg.batch_create(10);
    REQUIRE(entities.size() == 10);
    for (uint32 i = 0; i < entities.size(); ++i) {
        REQUIRE(entities[i].index == i);
    }
}

TEST_CASE("registry batch_destroy", "[registry]") {
    registry reg;

    auto entities = reg.batch_create(5);
    reg.batch_destroy(entities);

    auto reused = reg.batch_create(5);
    for (auto& e : reused) {
        REQUIRE(e.generation == 1);
    }
}

TEST_CASE("registry batch_add", "[registry]") {
    registry reg;

    auto entities = reg.batch_create(5);
    reg.batch_add<position_component>(entities, {1.f, 2.f, 3.f});

    for (auto& e : entities) {
        REQUIRE(reg.has<position_component>(e));
        REQUIRE(reg.get<position_component>(e).x == 1.f);
    }
}

TEST_CASE("registry batch_remove", "[registry]") {
    registry reg;

    auto entities = reg.batch_create(5);
    reg.batch_add<position_component>(entities);
    reg.batch_remove<position_component>(entities);

    for (auto& e : entities) {
        REQUIRE_FALSE(reg.has<position_component>(e));
    }
}

TEST_CASE("registry component_view", "[registry]") {
    registry reg;

    auto e0 = reg.create();
    auto e1 = reg.create();
    auto e2 = reg.create();

    reg.add<position_component>(e0, {1.f, 0.f, 0.f});
    reg.add<velocity_component>(e0, {0.1f, 0.f, 0.f});

    reg.add<position_component>(e1, {2.f, 0.f, 0.f});

    reg.add<position_component>(e2, {3.f, 0.f, 0.f});
    reg.add<velocity_component>(e2, {0.3f, 0.f, 0.f});

    auto view = reg.view<position_component, velocity_component>();
    int count = 0;
    for (const auto& [ent, pos, vel] : view) {
        REQUIRE(pos.x > 0.f);
        REQUIRE(vel.dx > 0.f);
        ++count;
    }
    REQUIRE(count == 2);
}

TEST_CASE("registry view over an unregistered component is empty", "[registry]") {
    registry reg;

    auto e = reg.create();
    reg.add<position_component>(e);

    auto view  = reg.view<position_component, health_component>();
    int  count = 0;
    for ([[maybe_unused]] const auto& row : view) {
        ++count;
    }
    REQUIRE(count == 0);
}

TEST_CASE("registry change sets and dependencies", "[registry]") {
    registry reg;

    auto e = reg.create();
    reg.add<position_component>(e);
    reg.add<velocity_component>(e);

    reg.add_change_dep(component_id_of<position_component>(),
                       component_id_of<velocity_component>());

    reg.notify_changed<position_component>(e);

    REQUIRE(std::ranges::contains(reg.changed<position_component>(), e));
    REQUIRE(std::ranges::contains(reg.requested<velocity_component>(), e));

    reg.clear_changed();
    REQUIRE(reg.changed<position_component>().empty());
}

TEST_CASE("registry propagates a change only to entities holding the dependent", "[registry]") {
    registry reg;

    auto e = reg.create();
    reg.add<position_component>(e);

    reg.add_change_dep(component_id_of<position_component>(),
                       component_id_of<velocity_component>());
    reg.notify_changed<position_component>(e);

    REQUIRE(reg.requested<velocity_component>().empty());
}

TEST_CASE("registry reports a change written after another actor read the set", "[registry]") {
    registry reg;
    auto e = reg.create();
    reg.add<position_component>(e);

    constexpr uint32 reader = 3;
    constexpr uint32 writer = 5;

    reg.open_change_tick();
    reg.set_change_actor(reader);
    REQUIRE(reg.changed<position_component>().empty());

    reg.set_change_actor(writer);
    reg.notify_changed<position_component>(e);
    reg.notify_changed<position_component>(e);
    reg.set_change_actor(no_change_actor);

    const auto late = reg.late_changes();
    REQUIRE(late.size() == 1);
    REQUIRE(late.front().component_id == component_id_of<position_component>());
    REQUIRE(late.front().reader == reader);
    REQUIRE(late.front().writer == writer);
}

TEST_CASE("registry accepts a change order that puts the writer first", "[registry]") {
    registry reg;
    auto e = reg.create();
    reg.add<position_component>(e);

    reg.open_change_tick();
    reg.set_change_actor(1);
    reg.notify_changed<position_component>(e);
    reg.set_change_actor(2);
    REQUIRE(reg.changed<position_component>().size() == 1);
    reg.set_change_actor(no_change_actor);

    reg.clear_changed();
    reg.open_change_tick();
    reg.set_change_actor(1);
    reg.notify_changed<position_component>(e);
    reg.set_change_actor(2);
    REQUIRE(reg.changed<position_component>().size() == 1);
    reg.set_change_actor(no_change_actor);

    REQUIRE(reg.late_changes().empty());
}

TEST_CASE("registry ignores reads and writes made outside of an actor", "[registry]") {
    registry reg;
    auto e = reg.create();
    reg.add<position_component>(e);

    reg.open_change_tick();
    REQUIRE(reg.changed<position_component>().empty());
    reg.notify_changed<position_component>(e);

    reg.set_change_actor(4);
    REQUIRE(reg.changed<position_component>().size() == 1);
    reg.notify_changed<position_component>(e);
    reg.set_change_actor(no_change_actor);

    REQUIRE(reg.late_changes().empty());
}

TEST_CASE("registry accepts a component registered without its type", "[registry]") {
    registry reg;

    const uint32 script_component = component_id_of<struct script_tag>();
    const component_layout layout{.size = sizeof(int), .align = alignof(int)};

    auto& pool = reg.ensure_pool(script_component, layout);

    auto e = reg.create();
    *static_cast<int*>(pool.emplace(e)) = 7;

    REQUIRE(reg.try_pool(script_component) == &pool);
    REQUIRE(reg.try_pool(script_component)->has(e));
    REQUIRE(*static_cast<const int*>(pool.get(e)) == 7);

    std::vector<uint32> ids;
    reg.collect_components(e, ids);
    REQUIRE(ids.size() == 1);
    REQUIRE(ids.front() == script_component);
}

TEST_CASE("registry destroy drops component storage", "[registry]") {
    registry reg;

    auto e = reg.create();
    reg.add<position_component>(e);
    REQUIRE(reg.try_pool_of<position_component>()->size() == 1);

    reg.destroy(e);
    REQUIRE(reg.try_pool_of<position_component>()->size() == 0);
}
