#include <catch2/catch_test_macros.hpp>

import std;

import vw.core;

using namespace vw;

TEST_CASE("every key has a name of its own and is found by it", "[input]") {
    std::set<std::string_view> seen;

    for (std::size_t i = 1; i < keyboard::key_count; ++i) {
        const auto key  = static_cast<keyboard::keys>(i);
        const auto name = keyboard::key_name(key);

        INFO("key " << i << " named '" << std::string{name} << "'");
        REQUIRE_FALSE(name.empty());
        REQUIRE(seen.insert(name).second);
        REQUIRE(keyboard::key_from_name(name) == key);
    }
}

TEST_CASE("key names follow the keys they stand for", "[input]") {
    using keyboard::keys;

    REQUIRE(keyboard::key_name(keys::A) == "a");
    REQUIRE(keyboard::key_name(keys::Z) == "z");
    REQUIRE(keyboard::key_name(keys::KEY_0) == "0");
    REQUIRE(keyboard::key_name(keys::KEY_9) == "9");
    REQUIRE(keyboard::key_name(keys::F12) == "f12");
    REQUIRE(keyboard::key_name(keys::SPACE) == "space");
    REQUIRE(keyboard::key_name(keys::PAGE_DOWN) == "page_down");
    REQUIRE(keyboard::key_name(keys::DOWN) == "down");
    REQUIRE(keyboard::key_name(keys::RIGHT_SUPER) == "right_super");
    REQUIRE(keyboard::key_name(keys::SLASH) == "slash");
    REQUIRE(keyboard::key_name(keys::NUM_9) == "num_9");
    REQUIRE(keyboard::key_name(keys::NUM_EQUAL) == "num_equal");
}

TEST_CASE("a name that is no key is not found", "[input]") {
    REQUIRE_FALSE(keyboard::key_from_name("unknown").has_value());
    REQUIRE_FALSE(keyboard::key_from_name("W").has_value());
    REQUIRE_FALSE(keyboard::key_from_name("mouse_left").has_value());
    REQUIRE_FALSE(keyboard::key_from_name("").has_value());
}

TEST_CASE("mouse buttons are named apart from keys", "[input]") {
    for (std::size_t i = 0; i < mouse::button_count; ++i) {
        const auto button = static_cast<mouse::buttons>(i);
        const auto name   = mouse::button_name(button);

        REQUIRE(mouse::button_from_name(name) == button);
        REQUIRE_FALSE(keyboard::key_from_name(name).has_value());
    }

    REQUIRE(mouse::button_name(mouse::buttons::LEFT) == "mouse_left");
    REQUIRE(mouse::button_name(mouse::buttons::BUTTON_8) == "mouse_8");
}

TEST_CASE("modifiers combine as flags", "[input]") {
    using keyboard::mods;

    const auto held = mods::SHIFT | mods::CTRL;

    REQUIRE(held & mods::SHIFT);
    REQUIRE(held & mods::CTRL);
    REQUIRE_FALSE(held & mods::ALT);
    REQUIRE_FALSE(mods::NONE & mods::SHIFT);
}
