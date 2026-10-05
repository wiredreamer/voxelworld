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
    const auto pose = g.world.get<game::player_component>(player).get_pose();
    REQUIRE(g.world.get<ecs::hierarchy_component>(player).get_children().size() == 1);
    REQUIRE(g.world.get<ecs::hierarchy_component>(pose).get_children().size() == 6);
    REQUIRE_FALSE(g.world.get<game::player_component>(player).has_weapon());
}

TEST_CASE("the player walks where the look points", "[game][player]") {
    game_world g;
    const auto player = g.spawn_controlled();

    const auto run_for = [&](float32 seconds) {
        for (float32 elapsed = 0.0F; elapsed < seconds; elapsed += 0.016F) {
            g.world.update(0.016F);
        }
    };

    g.mapper().key(keys::W, true);
    g.world.update(0.016F);

    const float32 speed = g.world.get<ecs::character_controller_component>(player).get_move_speed();
    auto wish           = g.world.get<ecs::movement_intent_component>(player).get_wish_velocity();
    REQUIRE(-wish.z > 0.0F);
    REQUIRE(-wish.z < speed * 0.5F);

    run_for(0.6F);
    wish = g.world.get<ecs::movement_intent_component>(player).get_wish_velocity();

    REQUIRE(wish.z == Catch::Approx(-speed));
    REQUIRE(wish.x == Catch::Approx(0.0F).margin(1.0e-3F));

    g.mapper().key(keys::W, false);
    g.mapper().key(keys::D, true);
    run_for(0.6F);

    wish = g.world.get<ecs::movement_intent_component>(player).get_wish_velocity();
    REQUIRE(wish.x == Catch::Approx(-speed));
    REQUIRE(wish.z == Catch::Approx(0.0F).margin(1.0e-3F));
}

TEST_CASE("the body leans the way the player gathers speed", "[game][player]") {
    game_world g;
    const auto player = g.spawn_controlled();
    const auto& state = g.world.get<game::player_component>(player);

    const auto run_for = [&](float32 seconds) {
        for (float32 elapsed = 0.0F; elapsed < seconds; elapsed += 0.016F) {
            g.world.update(0.016F);
        }
    };

    g.mapper().key(keys::W, true);
    run_for(1.0F);
    g.mapper().key(keys::W, false);
    run_for(1.0F);

    g.mapper().key(keys::W, true);
    run_for(0.1F);
    REQUIRE(state.get_lean_forward_degrees() > 1.0F);
    REQUIRE(state.get_lean_right_degrees() == Catch::Approx(0.0F).margin(0.1F));

    run_for(1.0F);
    REQUIRE(state.get_lean_forward_degrees() == Catch::Approx(0.0F).margin(0.1F));

    g.mapper().key(keys::W, false);
    run_for(0.1F);
    REQUIRE(state.get_lean_forward_degrees() < -1.0F);

    run_for(1.0F);
    g.mapper().key(keys::W, true);
    run_for(1.0F);
    g.mapper().key(keys::W, false);
    g.mapper().key(keys::D, true);
    run_for(0.05F);
    REQUIRE(state.get_lean_right_degrees() > 1.0F);

    const auto pose_rotation = g.world.get<ecs::transform_component>(state.get_pose()).get_rotation();
    REQUIRE_FALSE(math::approx_equal(pose_rotation, quat{0.0F, 0.0F, 0.0F, 1.0F}));
}

TEST_CASE("a standing attack strikes where the body faces, not where the camera looks", "[game][player]") {
    game_world g;
    const auto player = g.spawn_controlled();
    const auto& state = g.world.get<game::player_component>(player);

    const auto run_for = [&](float32 seconds) {
        for (float32 elapsed = 0.0F; elapsed < seconds; elapsed += 0.016F) {
            g.world.update(0.016F);
        }
    };

    g.mapper().key(keys::KEY_1, true);
    g.mapper().key(keys::W, true);
    run_for(1.0F);
    g.mapper().key(keys::KEY_1, false);
    g.mapper().key(keys::W, false);
    run_for(1.0F);
    REQUIRE(state.has_weapon());

    g.mapper().cursor_at(0.0, 0.0);
    g.world.update(0.016F);
    g.mapper().cursor_at(900.0, 0.0);
    g.mapper().button(mouse::buttons::LEFT, true);
    g.world.update(0.016F);
    g.mapper().button(mouse::buttons::LEFT, false);

    REQUIRE(state.is_swinging());
    REQUIRE(state.get_attack_direction().x == Catch::Approx(0.0F).margin(1.0e-3F));
    REQUIRE(state.get_attack_direction().z == Catch::Approx(-1.0F));

    run_for(0.3F);
    REQUIRE_FALSE(state.is_swinging());
    const auto rotation = g.world.get<ecs::transform_component>(player).get_rotation();
    const auto facing_back = math::quat_look_y({0.0F, 0.0F, -1.0F});
    REQUIRE(std::abs(math::dot(rotation, facing_back)) == Catch::Approx(1.0F).margin(1.0e-4F));
}

TEST_CASE("an attack while moving strikes along the move and steps into it", "[game][player]") {
    game_world g;
    const auto player = g.spawn_controlled();
    const auto& state = g.world.get<game::player_component>(player);
    const auto& tuning = g.world.system<game::player_system>().tuning();

    const auto run_for = [&](float32 seconds) {
        for (float32 elapsed = 0.0F; elapsed < seconds; elapsed += 0.016F) {
            g.world.update(0.016F);
        }
    };

    g.mapper().key(keys::KEY_1, true);
    g.world.update(0.016F);
    g.mapper().key(keys::KEY_1, false);
    run_for(0.5F);

    g.mapper().key(keys::D, true);
    g.mapper().button(mouse::buttons::LEFT, true);

    float32 travelled_x = 0.0F;
    float32 travelled_z = 0.0F;
    int32 frames        = 0;
    int32 hit_frames    = 0;
    do {
        g.world.update(0.016F);
        g.mapper().button(mouse::buttons::LEFT, false);
        const auto wish = g.world.get<ecs::movement_intent_component>(player).get_wish_velocity();
        travelled_x += wish.x * 0.016F;
        travelled_z += wish.z * 0.016F;
        hit_frames += state.is_hitting() ? 1 : 0;
        ++frames;
    } while (state.is_swinging() && frames < 100);

    const auto clip = asset::voxa_deserializer{}.deserialize(
        std::filesystem::path{VW_ASSET_DIR} / "animations" / "a_sword_attack.voxa"
    );
    REQUIRE(clip.has_value());
    constexpr float32 attack_rate = 2.0F;
    const float32 swing_seconds   = (*clip)->get_duration() / attack_rate;

    REQUIRE(state.get_attack_direction().x == Catch::Approx(-1.0F));
    REQUIRE(state.get_attack_direction().z == Catch::Approx(0.0F).margin(1.0e-3F));
    REQUIRE(static_cast<float32>(frames) * 0.016F == Catch::Approx(swing_seconds).margin(0.04F));
    REQUIRE(hit_frames > 0);
    REQUIRE_FALSE(state.is_hitting());
    REQUIRE(-travelled_x == Catch::Approx(tuning.lunge_distance).margin(1.5F));
    REQUIRE(travelled_z == Catch::Approx(0.0F).margin(1.0e-3F));

    const auto rotation = g.world.get<ecs::transform_component>(player).get_rotation();
    REQUIRE(math::approx_equal(rotation, math::quat_look_y({-1.0F, 0.0F, 0.0F})));

    run_for(0.6F);
    const auto wish = g.world.get<ecs::movement_intent_component>(player).get_wish_velocity();
    REQUIRE(-wish.x > 0.0F);
}

TEST_CASE("a strike pressed late in a swing follows it, one pressed early is dropped", "[game][player]") {
    game_world g;
    const auto player = g.spawn_controlled();
    const auto& state = g.world.get<game::player_component>(player);

    g.mapper().key(keys::KEY_1, true);
    g.world.update(0.016F);
    g.mapper().key(keys::KEY_1, false);
    g.world.update(0.016F);

    const auto click = [&] {
        g.mapper().button(mouse::buttons::LEFT, true);
        g.world.update(0.016F);
        g.mapper().button(mouse::buttons::LEFT, false);
    };

    const auto swinging_ticks = [&] {
        int32 ticks = 0;
        for (int32 tick = 0; tick < 100; ++tick) {
            g.world.update(0.016F);
            ticks += state.is_swinging() ? 1 : 0;
        }
        return ticks;
    };

    click();
    int32 one_swing = 1;
    while (state.is_swinging() && one_swing < 100) {
        g.world.update(0.016F);
        ++one_swing;
    }
    for (int32 tick = 0; tick < 40; ++tick) {
        g.world.update(0.016F);
    }

    click();
    for (int32 tick = 0; tick < 3; ++tick) {
        g.world.update(0.016F);
    }
    click();
    REQUIRE(swinging_ticks() < one_swing);

    for (int32 tick = 0; tick < 40; ++tick) {
        g.world.update(0.016F);
    }
    click();
    for (int32 tick = 0; tick < one_swing - 5; ++tick) {
        g.world.update(0.016F);
    }
    REQUIRE(state.is_swinging());
    click();
    REQUIRE(swinging_ticks() > one_swing);
}

TEST_CASE("a pinned parameter drives the machines against the game", "[game][player]") {
    game_world g;
    const auto player = g.spawn_controlled();
    const auto& fsm   = g.world.get<ecs::animation_fsm_component>(player);
    auto& machines    = g.world.system<ecs::animation_fsm_system>();

    const auto declared = fsm.get_declared_parameters();
    const auto attack   = std::ranges::find(declared, "attack", &asset::voxf_param::name);
    REQUIRE(attack != declared.end());
    REQUIRE(attack->type == asset::voxf_param_type::trigger);

    machines.modify(player).pin_parameter("grounded", 1.0F);
    g.world.update(0.016F);
    REQUIRE(fsm.get_board().get("grounded") == 1.0F);
    REQUIRE(fsm.get_machine(0).get_current_state() == "idle");

    machines.modify(player).pin_parameter("speed", 50.0F);
    REQUIRE(fsm.is_pinned("speed"));
    g.world.update(0.016F);
    g.world.update(0.016F);
    REQUIRE(fsm.get_board().get("speed") == 50.0F);
    REQUIRE(fsm.get_machine(0).get_current_state() == "run");

    machines.modify(player).unpin_parameter("speed");
    REQUIRE_FALSE(fsm.is_pinned("speed"));
    g.world.update(0.016F);
    g.world.update(0.016F);
    REQUIRE(fsm.get_board().get("speed") == 0.0F);
    REQUIRE(fsm.get_machine(0).get_current_state() == "idle");

    machines.modify(player).unpin_all_parameters();
    g.world.update(0.016F);
    REQUIRE(fsm.get_board().get("grounded") == 0.0F);
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
