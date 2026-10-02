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
        game::install_systems(world, assets);
    }
};

}  // namespace

TEST_CASE("a held key shows as held in every frame and as pressed once", "[game][input]") {
    game::input_mapper mapper;

    mapper.key(keys::SPACE, true);

    const auto first = mapper.take_frame();
    REQUIRE(first.is_held(game::input_action::jump));
    REQUIRE(first.was_pressed(game::input_action::jump));

    const auto second = mapper.take_frame();
    REQUIRE(second.is_held(game::input_action::jump));
    REQUIRE_FALSE(second.was_pressed(game::input_action::jump));

    mapper.key(keys::SPACE, false);

    const auto third = mapper.take_frame();
    REQUIRE_FALSE(third.is_held(game::input_action::jump));
}

TEST_CASE("a tap between two frames is not lost", "[game][input]") {
    game::input_mapper mapper;

    mapper.button(mouse::buttons::LEFT, true);
    mapper.button(mouse::buttons::LEFT, false);

    const auto frame = mapper.take_frame();
    REQUIRE(frame.was_pressed(game::input_action::attack));
    REQUIRE_FALSE(frame.is_held(game::input_action::attack));
    REQUIRE_FALSE(mapper.take_frame().was_pressed(game::input_action::attack));
}

TEST_CASE("a repeated key down does not press the action again", "[game][input]") {
    game::input_mapper mapper;

    mapper.key(keys::KEY_1, true);
    static_cast<void>(mapper.take_frame());
    mapper.key(keys::KEY_1, true);

    REQUIRE_FALSE(mapper.take_frame().was_pressed(game::input_action::toggle_weapon));
}

TEST_CASE("move keys make a unit direction", "[game][input]") {
    game::input_mapper mapper;

    mapper.key(keys::W, true);
    auto frame = mapper.take_frame();
    REQUIRE(frame.move_forward == 1.0F);
    REQUIRE(frame.move_right == 0.0F);

    mapper.key(keys::D, true);
    frame = mapper.take_frame();
    REQUIRE(frame.move_forward == Catch::Approx(std::sqrt(0.5F)));
    REQUIRE(frame.move_right == Catch::Approx(std::sqrt(0.5F)));

    mapper.key(keys::S, true);
    frame = mapper.take_frame();
    REQUIRE(frame.move_forward == 0.0F);
    REQUIRE(frame.move_right == 1.0F);
}

TEST_CASE("the cursor turns the look and the pitch stays within its limits", "[game][input]") {
    game::input_settings settings{
        .look_degrees_per_count     = 0.5F,
        .look_pitch_min_degrees     = -40.0F,
        .look_pitch_max_degrees     = 10.0F,
        .initial_look_yaw_degrees   = 90.0F,
        .initial_look_pitch_degrees = 0.0F,
    };
    game::input_mapper mapper{game::default_input_bindings(), settings};

    mapper.cursor_at(100.0, 100.0);
    auto frame = mapper.take_frame();
    REQUIRE(frame.look_yaw_degrees == 90.0F);
    REQUIRE(frame.look_pitch_degrees == 0.0F);

    mapper.cursor_at(120.0, 110.0);
    frame = mapper.take_frame();
    REQUIRE(frame.look_yaw_degrees == Catch::Approx(100.0F));
    REQUIRE(frame.look_pitch_degrees == Catch::Approx(-5.0F));

    mapper.cursor_at(120.0, 1000.0);
    REQUIRE(mapper.take_frame().look_pitch_degrees == -40.0F);

    mapper.cursor_at(120.0, -1000.0);
    REQUIRE(mapper.take_frame().look_pitch_degrees == 10.0F);
}

TEST_CASE("a dropped cursor does not jump the look when it comes back", "[game][input]") {
    game::input_mapper mapper;

    mapper.cursor_at(0.0, 0.0);
    const auto before = mapper.take_frame();

    mapper.drop_cursor();
    mapper.cursor_at(900.0, 0.0);

    REQUIRE(mapper.take_frame().look_yaw_degrees == before.look_yaw_degrees);
}

TEST_CASE("scroll is handed out once", "[game][input]") {
    game::input_mapper mapper;

    mapper.scroll(1.0F);
    mapper.scroll(2.0F);

    REQUIRE(mapper.take_frame().zoom_delta == 3.0F);
    REQUIRE(mapper.take_frame().zoom_delta == 0.0F);
}

TEST_CASE("a rebound key drives the action", "[game][input]") {
    auto bindings = game::default_input_bindings();
    bindings.key_actions.push_back({keys::E, game::input_action::attack});
    game::input_mapper mapper{std::move(bindings)};

    mapper.key(keys::E, true);

    REQUIRE(mapper.take_frame().was_pressed(game::input_action::attack));
}

TEST_CASE("released input holds nothing", "[game][input]") {
    game::input_mapper mapper;

    mapper.key(keys::W, true);
    mapper.key(keys::SPACE, true);
    mapper.release_all();

    const auto frame = mapper.take_frame();
    REQUIRE(frame.move_forward == 0.0F);
    REQUIRE(frame.held == 0);
    REQUIRE(frame.pressed == 0);
}

TEST_CASE("the look vectors follow the yaw", "[game][input]") {
    const game::input_frame north{.look_yaw_degrees = 0.0F};
    REQUIRE(north.look_forward_flat().z == Catch::Approx(1.0F));
    REQUIRE(north.look_right_flat().x == Catch::Approx(1.0F));

    const game::input_frame east{.look_yaw_degrees = 90.0F};
    REQUIRE(east.look_forward_flat().x == Catch::Approx(1.0F));
    REQUIRE(east.look_right_flat().z == Catch::Approx(-1.0F));
}

TEST_CASE("the local frame reaches the controlled entity each tick", "[game][input]") {
    game_world g;
    auto& input = g.world.system<game::input_system>();

    const auto ent = g.world.create().get_entity();
    input.control_locally(ent);
    REQUIRE(input.locally_controlled() == ent);

    input.mapper().key(keys::W, true);
    g.world.update(0.016F);

    const auto& frame = g.world.get<game::player_input_component>(ent).get_frame();
    REQUIRE(frame.move_forward == 1.0F);

    input.mapper().key(keys::W, false);
    g.world.update(0.016F);

    REQUIRE(g.world.get<game::player_input_component>(ent).get_frame().move_forward == 0.0F);
}

TEST_CASE("a submitted frame drives an entity that is not local", "[game][input]") {
    game_world g;
    auto& input = g.world.system<game::input_system>();

    const auto remote = g.world.create().with<game::player_input_component>().get_entity();

    game::input_frame frame;
    frame.move_right = 1.0F;
    input.submit(remote, frame);
    g.world.update(0.016F);

    REQUIRE(g.world.get<game::player_input_component>(remote).get_frame().move_right == 1.0F);
}
