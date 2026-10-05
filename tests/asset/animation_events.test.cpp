#include <catch2/catch_test_macros.hpp>

import std;

import vw.core;
import vw.asset;

using namespace vw;
using namespace vw::asset;

namespace {

auto make_clip() -> std::shared_ptr<animation_clip> {
    auto clip = std::make_shared<animation_clip>("a_swing");

    animation_track track("body", 60.0F);
    auto position = make_animation_channel<animation_property::position>();
    position.add({0.0F, vec3f{0.0F, 0.0F, 0.0F}});
    position.add({1.0F, vec3f{0.0F, 1.0F, 0.0F}});
    track.add<animation_property::position>(std::move(position));
    clip->add_track(std::move(track));

    clip->set_events({
        {0.0F, "start", ""},
        {0.25F, "quarter", ""},
        {0.5F, "half", ""},
        {1.0F, "end", ""},
    });
    return clip;
}

auto make_layer(animation_loop_mode mode, float32 speed = 1.0F) -> animation_layer {
    animation_layer layer;
    layer.clip           = make_clip();
    layer.state          = animation_state::playing;
    layer.loop_mode      = mode;
    layer.playback_speed = speed;
    return layer;
}

auto run(animation_layer& layer, float32 delta_time, int32 steps) -> std::vector<std::string> {
    std::vector<std::string> names;
    std::vector<const animation_event*> crossed;
    for (int32 step = 0; step < steps; ++step) {
        crossed.clear();
        advance_layer_time(layer, delta_time, crossed);
        for (const auto* event : crossed) {
            names.push_back(event->name);
        }
    }
    return names;
}

auto count(const std::vector<std::string>& names, std::string_view name) -> std::ptrdiff_t {
    return std::ranges::count(names, name);
}

using names = std::vector<std::string>;

}  // namespace

TEST_CASE("a clip played once fires every event once, the last one as it stops", "[animation_events]") {
    auto layer = make_layer(animation_loop_mode::once);

    REQUIRE(run(layer, 0.1F, 12) == names{"start", "quarter", "half", "end"});
    REQUIRE(layer.state == animation_state::stopped);
    REQUIRE(layer.time == 1.0F);
}

TEST_CASE("a looped clip fires each event once a lap", "[animation_events]") {
    auto layer = make_layer(animation_loop_mode::loop);

    const auto fired = run(layer, 0.125F, 24);

    REQUIRE(fired.size() == 12);
    REQUIRE(std::vector(fired.begin(), fired.begin() + 4) == names{"start", "quarter", "half", "end"});
    for (const auto* name : {"start", "quarter", "half", "end"}) {
        REQUIRE(count(fired, name) == 3);
    }
    REQUIRE(layer.time == 0.0F);
}

TEST_CASE("a fast clip loses no event when a frame steps over several", "[animation_events]") {
    auto layer = make_layer(animation_loop_mode::loop, 3.0F);

    const auto fired = run(layer, 0.125F, 8);

    REQUIRE(fired.size() == 12);
    for (const auto* name : {"start", "quarter", "half", "end"}) {
        REQUIRE(count(fired, name) == 3);
    }
}

TEST_CASE("a frame longer than the clip fires every lap it covers in order", "[animation_events]") {
    auto layer = make_layer(animation_loop_mode::loop);

    REQUIRE(run(layer, 2.5F, 1) == names{
        "start", "quarter", "half", "end", "start", "quarter", "half", "end", "start", "quarter",
    });
    REQUIRE(layer.time == 0.5F);
}

TEST_CASE("ping pong fires the turning events once and the rest both ways", "[animation_events]") {
    auto layer = make_layer(animation_loop_mode::ping_pong);

    REQUIRE(run(layer, 0.125F, 16) == names{"start", "quarter", "half", "end", "half", "quarter"});
    REQUIRE(layer.time == 0.0F);
    REQUIRE(layer.direction == 1.0F);

    REQUIRE(run(layer, 0.125F, 1) == names{"start"});
}

TEST_CASE("a reversed loop fires events backwards", "[animation_events]") {
    auto layer = make_layer(animation_loop_mode::loop, -1.0F);
    layer.time = 1.0F;

    REQUIRE(run(layer, 0.125F, 8) == names{"end", "half", "quarter", "start"});
    REQUIRE(layer.time == 1.0F);
}

TEST_CASE("a paused clip neither moves nor fires", "[animation_events]") {
    auto layer  = make_layer(animation_loop_mode::loop);
    layer.state = animation_state::paused;

    REQUIRE(run(layer, 0.125F, 10).empty());
    REQUIRE(layer.time == 0.0F);
}

TEST_CASE("a held clip fires nothing", "[animation_events]") {
    auto layer = make_layer(animation_loop_mode::loop, 0.0F);
    layer.time = 0.25F;

    REQUIRE(run(layer, 0.125F, 10).empty());
    REQUIRE(layer.time == 0.25F);
}
