#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

import std;

import vw.core;
import vw.game;

using namespace vw;
using Catch::Matchers::ContainsSubstring;
using keyboard::keys;

TEST_CASE("a layout names actions, keys and mouse buttons", "[game][bindings]") {
    const auto bindings = game::parse_input_bindings(R"({
        "actions": { "attack": ["mouse_left", "e"], "jump": ["space"] },
        "move": { "forward": ["w", "up"] }
    })");

    REQUIRE(bindings.has_value());
    REQUIRE(bindings->key_actions == std::vector<game::key_action_binding>{
                                         {keys::E, game::input_action::attack},
                                         {keys::SPACE, game::input_action::jump},
                                     });
    REQUIRE(bindings->button_actions == std::vector<game::button_action_binding>{
                                            {mouse::buttons::LEFT, game::input_action::attack},
                                        });
    REQUIRE(bindings->key_moves == std::vector<game::key_move_binding>{
                                       {keys::W, game::move_direction::forward},
                                       {keys::UP, game::move_direction::forward},
                                   });
}

TEST_CASE("an action left out of a layout is simply unbound", "[game][bindings]") {
    const auto bindings = game::parse_input_bindings(R"({ "actions": { "jump": [] } })");

    REQUIRE(bindings.has_value());
    REQUIRE(bindings->key_actions.empty());
    REQUIRE(bindings->key_moves.empty());
}

TEST_CASE("a layout that cannot be read says where and why", "[game][bindings]") {
    SECTION("broken json") {
        const auto bindings = game::parse_input_bindings(R"({ "actions": )");
        REQUIRE_FALSE(bindings.has_value());
    }
    SECTION("an action that does not exist") {
        const auto bindings = game::parse_input_bindings(R"({ "actions": { "fly": ["f"] } })");
        REQUIRE_FALSE(bindings.has_value());
        REQUIRE_THAT(bindings.error(), ContainsSubstring("fly"));
        REQUIRE_THAT(bindings.error(), ContainsSubstring("not an action"));
    }
    SECTION("a key that does not exist") {
        const auto bindings =
            game::parse_input_bindings(R"({ "actions": { "jump": ["spacebar"] } })");
        REQUIRE_FALSE(bindings.has_value());
        REQUIRE_THAT(bindings.error(), ContainsSubstring("spacebar"));
        REQUIRE_THAT(bindings.error(), ContainsSubstring("$.actions.jump"));
    }
    SECTION("a mouse button for a direction") {
        const auto bindings =
            game::parse_input_bindings(R"({ "move": { "forward": ["mouse_left"] } })");
        REQUIRE_FALSE(bindings.has_value());
        REQUIRE_THAT(bindings.error(), ContainsSubstring("mouse_left"));
    }
    SECTION("a direction that does not exist") {
        const auto bindings = game::parse_input_bindings(R"({ "move": { "up": ["w"] } })");
        REQUIRE_FALSE(bindings.has_value());
        REQUIRE_THAT(bindings.error(), ContainsSubstring("not a direction"));
    }
    SECTION("a section that does not exist") {
        const auto bindings = game::parse_input_bindings(R"({ "action": { "jump": ["space"] } })");
        REQUIRE_FALSE(bindings.has_value());
        REQUIRE_THAT(bindings.error(), ContainsSubstring("$.action"));
    }
    SECTION("a key given as a number") {
        const auto bindings = game::parse_input_bindings(R"({ "actions": { "jump": [32] } })");
        REQUIRE_FALSE(bindings.has_value());
    }
    SECTION("a missing file") {
        const auto bindings = game::load_input_bindings("no/such/layout.json");
        REQUIRE_FALSE(bindings.has_value());
        REQUIRE_THAT(bindings.error(), ContainsSubstring("no/such/layout.json"));
    }
}

TEST_CASE("a written layout reads back the same", "[game][bindings]") {
    auto bindings = game::default_input_bindings();
    bindings.key_actions.push_back({keys::ENTER, game::input_action::jump});

    const auto text   = game::dump_input_bindings(bindings);
    const auto reread = game::parse_input_bindings(text);

    REQUIRE(reread.has_value());
    REQUIRE(game::dump_input_bindings(*reread) == text);
}

TEST_CASE("the layout shipped with the game binds every action and direction", "[game][bindings]") {
    const auto file =
        std::filesystem::path{VW_ASSET_DIR} / "data" / "input_bindings.json";
    const auto bindings = game::load_input_bindings(file);

    if (!bindings) {
        FAIL(bindings.error());
    }

    for (std::size_t i = 0; i < game::input_action_count; ++i) {
        const auto action = static_cast<game::input_action>(i);
        INFO("action " << std::string{game::input_action_name(action)});

        const bool on_key = std::ranges::contains(
            bindings->key_actions, action, &game::key_action_binding::action
        );
        const bool on_button = std::ranges::contains(
            bindings->button_actions, action, &game::button_action_binding::action
        );
        REQUIRE((on_key || on_button));
    }

    for (std::size_t i = 0; i < game::move_direction_count; ++i) {
        const auto direction = static_cast<game::move_direction>(i);
        REQUIRE(std::ranges::contains(
            bindings->key_moves, direction, &game::key_move_binding::direction
        ));
    }
}

TEST_CASE("a layout put into the mapper changes what a key does", "[game][bindings]") {
    game::input_mapper mapper;

    mapper.key(keys::Q, true);
    REQUIRE(mapper.take_frame().is_held(game::input_action::ability_1));

    const auto moved = game::parse_input_bindings(R"({ "actions": { "ability_1": ["z"] } })");
    REQUIRE(moved.has_value());
    mapper.set_bindings(*moved);

    auto frame = mapper.take_frame();
    REQUIRE_FALSE(frame.is_held(game::input_action::ability_1));
    REQUIRE(frame.was_released(game::input_action::ability_1));

    mapper.key(keys::Q, true);
    REQUIRE_FALSE(mapper.take_frame().is_held(game::input_action::ability_1));

    mapper.key(keys::Z, true);
    frame = mapper.take_frame();
    REQUIRE(frame.is_held(game::input_action::ability_1));
    REQUIRE(frame.was_pressed(game::input_action::ability_1));
}
