#include <catch2/catch_test_macros.hpp>

import std;

import vw.core;
import vw.asset;

using namespace vw;

namespace {

auto position_times(const asset::animation_track& track) -> std::vector<float32> {
    std::vector<float32> times;
    const auto* channel_var = track.get_channel(asset::animation_property::position);
    if (channel_var == nullptr) {
        return times;
    }
    for (const auto& key : std::get<asset::animation_channel<vec3f>>(*channel_var).get_keyframes()) {
        times.push_back(key.time);
    }
    return times;
}

auto position_at(const asset::animation_track& track, std::size_t index) -> asset::keyframe_vec3f {
    const auto* channel_var = track.get_channel(asset::animation_property::position);
    return std::get<asset::animation_channel<vec3f>>(*channel_var).get_keyframes()[index];
}

}  // namespace

TEST_CASE("a key creates the channels it has values for", "[animation_edit]") {
    asset::animation_track track{"head"};

    asset::put_key(track, {.time = 0.5F, .position = vec3f{1.F, 2.F, 3.F}, .rotation = quat{}});

    CHECK(track.has_channel(asset::animation_property::position));
    CHECK(track.has_channel(asset::animation_property::rotation));
    CHECK_FALSE(track.has_channel(asset::animation_property::scale));
    CHECK(asset::count_keys(track) == 2);
    CHECK(position_at(track, 0).value == vec3f{1.F, 2.F, 3.F});
}

TEST_CASE("keys are kept in the order of time whatever the order they came in", "[animation_edit]") {
    asset::animation_track track{"head"};

    asset::put_key(track, {.time = 2.F, .position = vec3f{}});
    asset::put_key(track, {.time = 0.F, .position = vec3f{}});
    asset::put_key(track, {.time = 1.F, .position = vec3f{}});

    CHECK(position_times(track) == std::vector<float32>{0.F, 1.F, 2.F});
}

TEST_CASE("a key at an instant that is taken replaces the one there", "[animation_edit]") {
    asset::animation_track track{"head"};

    asset::put_key(track, {.time = 1.F, .position = vec3f{1.F, 0.F, 0.F}});
    asset::put_key(track, {.time = 1.0004F, .position = vec3f{5.F, 0.F, 0.F}});

    REQUIRE(position_times(track).size() == 1);
    CHECK(position_at(track, 0).value == vec3f{5.F, 0.F, 0.F});
}

TEST_CASE("keys further apart than an instant stay apart", "[animation_edit]") {
    asset::animation_track track{"head"};

    asset::put_key(track, {.time = 1.F, .position = vec3f{}});
    asset::put_key(track, {.time = 1.01F, .position = vec3f{}});

    CHECK(position_times(track).size() == 2);
}

TEST_CASE("a key leaves the channels it does not name alone", "[animation_edit]") {
    asset::animation_track track{"head"};

    asset::put_key(track, {.time = 1.F, .position = vec3f{1.F, 1.F, 1.F}, .scale = vec3f{2.F, 2.F, 2.F}});
    asset::put_key(track, {.time = 1.F, .position = vec3f{9.F, 9.F, 9.F}});

    const auto& scale = std::get<asset::animation_channel<vec3f>>(
        *track.get_channel(asset::animation_property::scale)
    );
    CHECK(scale.get_keyframes().front().value == vec3f{2.F, 2.F, 2.F});
    CHECK(position_at(track, 0).value == vec3f{9.F, 9.F, 9.F});
}

TEST_CASE("a key carries its interpolation and tangents", "[animation_edit]") {
    asset::animation_track track{"head"};

    asset::put_key(
        track,
        {
            .time        = 0.F,
            .position    = vec3f{},
            .interp      = math::interpolation_type::cubic_bezier,
            .tangent_in  = 0.25F,
            .tangent_out = 0.75F,
        }
    );

    const auto key = position_at(track, 0);
    CHECK(key.interp == math::interpolation_type::cubic_bezier);
    CHECK(key.tangent_in == 0.25F);
    CHECK(key.tangent_out == 0.75F);
}

TEST_CASE("a new key changes what the track evaluates to", "[animation_edit]") {
    asset::animation_track track{"head"};

    asset::put_key(track, {.time = 0.F, .position = vec3f{0.F, 0.F, 0.F}});
    asset::put_key(track, {.time = 1.F, .position = vec3f{10.F, 0.F, 0.F}});
    REQUIRE(track.get_transform(1.F).has_value());
    CHECK(std::abs(track.get_transform(1.F)->get_position().x - 10.F) < 1e-3F);

    asset::put_key(track, {.time = 1.F, .position = vec3f{20.F, 0.F, 0.F}});
    CHECK(std::abs(track.get_transform(1.F)->get_position().x - 20.F) < 1e-3F);
    CHECK(track.get_duration() == 1.F);
}

TEST_CASE("dropping takes the keys of a span of time, ends included", "[animation_edit]") {
    asset::animation_track track{"head"};
    for (const float32 time : {0.F, 1.F, 2.F, 3.F}) {
        asset::put_key(track, {.time = time, .position = vec3f{}, .scale = vec3f{1.F, 1.F, 1.F}});
    }

    const auto dropped = asset::drop_keys(track, asset::animation_property::position, 1.F, 2.F);

    CHECK(dropped == 2);
    CHECK(position_times(track) == std::vector<float32>{0.F, 3.F});
    CHECK(asset::count_keys(track) == 6);
}

TEST_CASE("dropping without a property takes every channel", "[animation_edit]") {
    asset::animation_track track{"head"};
    for (const float32 time : {0.F, 1.F}) {
        asset::put_key(
            track, {.time = time, .position = vec3f{}, .rotation = quat{}, .scale = vec3f{}}
        );
    }

    const auto dropped = asset::drop_keys(track, std::nullopt, 1.F, 1.F);

    CHECK(dropped == 3);
    CHECK(asset::count_keys(track) == 3);
}

TEST_CASE("a channel left without keys is removed", "[animation_edit]") {
    asset::animation_track track{"head"};
    asset::put_key(track, {.time = 0.F, .position = vec3f{}, .rotation = quat{}});

    const auto dropped = asset::drop_keys(track, asset::animation_property::position, 0.F, 10.F);

    CHECK(dropped == 1);
    CHECK_FALSE(track.has_channel(asset::animation_property::position));
    CHECK(track.has_channel(asset::animation_property::rotation));
}

TEST_CASE("dropping where there are no keys changes nothing", "[animation_edit]") {
    asset::animation_track track{"head"};
    asset::put_key(track, {.time = 0.F, .position = vec3f{}});

    CHECK(asset::drop_keys(track, asset::animation_property::position, 5.F, 6.F) == 0);
    CHECK(asset::drop_keys(track, asset::animation_property::scale, 0.F, 10.F) == 0);
    CHECK(asset::count_keys(track) == 1);
}
