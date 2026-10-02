#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

import std;

import vw.core;
import vw.asset;
import vw.ecs;
import vw.world;
import vw.game;

using namespace vw;
using keyboard::keys;

namespace {

struct game_world {
    ecs::world world;
    asset::vox_parser_plain parser;
    asset::model_library library{
        world.resource<asset::model_registry>(), world.voxel_types(), VW_ASSET_DIR
    };
    asset::asset_storage assets{parser, library};

    game_world() {
        assets.load_prefab("p_humanoid", asset::asset_ref{"prefabs/p_humanoid.vox"});
        assets.load_prefab("p_sword", asset::asset_ref{"prefabs/p_sword.vox"});
        game::install_systems(world, assets);
    }

    [[nodiscard]] auto spawn_controlled() -> ecs::entity {
        const auto player = world.system<game::player_system>().spawn();
        world.system<game::input_system>().control_locally(player);
        return player;
    }

    [[nodiscard]] auto mapper() -> game::input_mapper& {
        return world.system<game::input_system>().mapper();
    }
};

}  // namespace

TEST_CASE("a spawned player is a controllable character without a weapon", "[game][player]") {
    game_world g;
    const auto player = g.world.system<game::player_system>().spawn();

    REQUIRE(g.world.has<game::player_component>(player));
    REQUIRE(g.world.has<ecs::character_controller_component>(player));
    REQUIRE(g.world.has<ecs::animation_fsm_component>(player));
    REQUIRE(g.world.get<ecs::animation_fsm_component>(player).machine_count() > 0);
    REQUIRE(g.world.get<ecs::hierarchy_component>(player).get_children().size() == 6);
    REQUIRE_FALSE(g.world.get<game::player_component>(player).has_weapon());
}

TEST_CASE("the player walks where the look points", "[game][player]") {
    game_world g;
    const auto player = g.spawn_controlled();

    g.mapper().key(keys::W, true);
    g.world.update(0.016F);

    const float32 speed = g.world.get<ecs::character_controller_component>(player).get_move_speed();
    auto wish           = g.world.get<ecs::movement_intent_component>(player).get_wish_velocity();

    REQUIRE(wish.z == Catch::Approx(-speed));
    REQUIRE(wish.x == Catch::Approx(0.0F).margin(1.0e-3F));

    g.mapper().key(keys::W, false);
    g.mapper().key(keys::D, true);
    g.world.update(0.016F);

    wish = g.world.get<ecs::movement_intent_component>(player).get_wish_velocity();
    REQUIRE(wish.x == Catch::Approx(-speed));
    REQUIRE(wish.z == Catch::Approx(0.0F).margin(1.0e-3F));
}

TEST_CASE("the weapon key takes the sword out and puts it away", "[game][player]") {
    game_world g;
    const auto player = g.spawn_controlled();

    g.mapper().key(keys::KEY_1, true);
    g.world.update(0.016F);
    REQUIRE(g.world.get<game::player_component>(player).has_weapon());

    g.world.update(0.016F);
    REQUIRE(g.world.get<game::player_component>(player).has_weapon());

    g.mapper().key(keys::KEY_1, false);
    g.mapper().key(keys::KEY_1, true);
    g.world.update(0.016F);
    REQUIRE_FALSE(g.world.get<game::player_component>(player).has_weapon());
}

TEST_CASE("a player still waiting for the ground ignores input", "[game][player]") {
    game_world g;
    const auto player = g.spawn_controlled();
    g.world.system<game::surface_placement_system>().place(player, {0.0F, 0.0F});

    g.mapper().key(keys::W, true);
    g.mapper().key(keys::KEY_1, true);
    g.world.update(0.016F);

    const auto wish = g.world.get<ecs::movement_intent_component>(player).get_wish_velocity();
    REQUIRE(wish.x == 0.0F);
    REQUIRE(wish.z == 0.0F);
    REQUIRE_FALSE(g.world.get<game::player_component>(player).has_weapon());
}
