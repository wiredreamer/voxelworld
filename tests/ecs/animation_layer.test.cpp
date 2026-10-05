#include <catch2/catch_test_macros.hpp>

import std;

import vw.core;
import vw.asset;
import vw.ecs;
import vw.world;

using namespace vw;
using namespace vw::ecs;
using namespace vw::asset;


struct anim_test_fixture {
    world w;

    registry& reg = w.registry();
    animation_clip_registry& clip_reg = w.resource<animation_clip_registry>();
    animation_system& anim_sys = w.system<animation_system>();
    hierarchy_system& hierarchy_sys = w.system<hierarchy_system>();
    transform_system& transform_sys = w.system<transform_system>();

    auto create_root() -> entity {
        auto ent = reg.create();
        reg.add(ent, hierarchy_component{});
        reg.add(ent, transform_component{});
        reg.add(ent, animation_player_component{});
        return ent;
    }

    auto create_child(entity parent, const std::string& target_name) -> entity {
        auto ent = reg.create();
        reg.add(ent, hierarchy_component{});
        reg.add(ent, transform_component{});
        reg.add(ent, animation_target_component{});
        hierarchy_sys.modify(ent).set_parent(parent);
        anim_sys.modify_target(ent).set_target_name(target_name);
        return ent;
    }

    auto make_clip(const std::string& name, const std::vector<std::string>& targets)
        -> std::shared_ptr<animation_clip> {
        auto clip = clip_reg.create(name);
        for (const auto& target : targets) {
            animation_track track(target, 60.0f);
            auto pos_ch = make_animation_channel<animation_property::position>();
            pos_ch.add({0.0f, vec3f{0.0f, 0.0f, 0.0f}});
            pos_ch.add({1.0f, vec3f{0.0f, 1.0f, 0.0f}});
            track.add<animation_property::position>(std::move(pos_ch));
            clip->add_track(std::move(track));
        }
        return clip;
    }
};

TEST_CASE("animation_layer is_active", "[animation_layer]") {
    animation_layer layer;
    REQUIRE_FALSE(layer.is_active());

    layer.state = vw::asset::animation_state::playing;
    REQUIRE(layer.is_active());

    layer.state = vw::asset::animation_state::paused;
    REQUIRE_FALSE(layer.is_active());

    layer.state = vw::asset::animation_state::stopped;
    layer.fade_is_out = true;
    REQUIRE(layer.is_active());
}

TEST_CASE("animation_layer affects_target", "[animation_layer]") {
    animation_layer layer;
    layer.mask.insert("arm_l");
    layer.mask.insert("arm_r");

    REQUIRE(layer.affects_target("arm_l"));
    REQUIRE(layer.affects_target("arm_r"));
    REQUIRE_FALSE(layer.affects_target("leg_l"));
}

TEST_CASE("animation_player_component layer management", "[animation_layer]") {
    anim_test_fixture f;
    auto root = f.create_root();

    auto& comp = f.reg.get<animation_player_component>(root);
    REQUIRE(comp.layer_count() == 0);

    auto modifier = f.anim_sys.modify_player(root);
    modifier.layer(0);
    REQUIRE(comp.layer_count() == 1);

    modifier.layer(2);
    REQUIRE(comp.layer_count() == 3);
}

TEST_CASE("single layer plays animation", "[animation_layer]") {
    anim_test_fixture f;
    auto root  = f.create_root();
    auto child = f.create_child(root, "body");

    auto clip = f.make_clip("walk", {"body"});

    auto layer = f.anim_sys.modify_player(root).layer(0);
    layer.blend_to(clip);
    layer.set_loop_mode(animation_loop_mode::loop);
    layer.play();

    f.anim_sys.set_target_fps(60.0f);

    for (int i = 0; i < 30; ++i) {
        f.anim_sys.update(1.0f / 60.0f);
    }

    auto& transform_comp = f.reg.get<transform_component>(child);
    auto pos = transform_comp.get_transform().get_position();
    REQUIRE(pos.y > 0.0f);
}

TEST_CASE("auto mask computed from clip", "[animation_layer]") {
    anim_test_fixture f;
    auto root = f.create_root();

    auto clip = f.make_clip("attack", {"arm_l", "arm_r", "torso"});

    auto layer = f.anim_sys.modify_player(root).layer(0);
    layer.blend_to(clip);

    auto& comp = f.reg.get<animation_player_component>(root);
    const auto& mask = comp.get_layer(0).mask;
    REQUIRE(mask.size() == 3);
    REQUIRE(mask.contains("arm_l"));
    REQUIRE(mask.contains("arm_r"));
    REQUIRE(mask.contains("torso"));
}

TEST_CASE("two layers override targets", "[animation_layer]") {
    anim_test_fixture f;
    auto root = f.create_root();
    auto leg  = f.create_child(root, "leg");
    auto arm  = f.create_child(root, "arm");

    auto walk_clip = f.clip_reg.create("walk");
    {
        animation_track leg_track("leg", 60.0f);
        auto pos_ch = make_animation_channel<animation_property::position>();
        pos_ch.add({0.0f, vec3f{0.0f, 0.0f, 0.0f}});
        pos_ch.add({1.0f, vec3f{0.0f, 1.0f, 0.0f}});
        leg_track.add<animation_property::position>(std::move(pos_ch));
        walk_clip->add_track(std::move(leg_track));

        animation_track arm_track("arm", 60.0f);
        auto arm_pos_ch = make_animation_channel<animation_property::position>();
        arm_pos_ch.add({0.0f, vec3f{0.0f, 0.0f, 0.0f}});
        arm_pos_ch.add({1.0f, vec3f{0.0f, 0.5f, 0.0f}});
        arm_track.add<animation_property::position>(std::move(arm_pos_ch));
        walk_clip->add_track(std::move(arm_track));
    }

    auto attack_clip = f.clip_reg.create("attack");
    {
        animation_track arm_track("arm", 60.0f);
        auto pos_ch = make_animation_channel<animation_property::position>();
        pos_ch.add({0.0f, vec3f{0.0f, 0.0f, 0.0f}});
        pos_ch.add({1.0f, vec3f{1.0f, 0.0f, 0.0f}});
        arm_track.add<animation_property::position>(std::move(pos_ch));
        attack_clip->add_track(std::move(arm_track));
    }

    auto player = f.anim_sys.modify_player(root);

    auto base_layer = player.layer(0);
    base_layer.blend_to(walk_clip);
    base_layer.set_loop_mode(animation_loop_mode::loop);
    base_layer.play();

    auto upper_layer = player.layer(1);
    upper_layer.blend_to(attack_clip);
    upper_layer.set_loop_mode(animation_loop_mode::once);
    upper_layer.play();

    f.anim_sys.set_target_fps(60.0f);

    for (int i = 0; i < 30; ++i) {
        f.anim_sys.update(1.0f / 60.0f);
    }

    auto leg_pos = f.reg.get<transform_component>(leg).get_transform().get_position();
    REQUIRE(leg_pos.y > 0.0f);

    auto arm_pos = f.reg.get<transform_component>(arm).get_transform().get_position();
    REQUIRE(arm_pos.x > 0.0f);
}

namespace {

auto fired_names(const animation_player_component& comp) -> std::vector<std::string> {
    std::vector<std::string> names;
    for (const auto& event : comp.get_fired_events()) {
        names.push_back(event.name);
    }
    return names;
}

}  // namespace

TEST_CASE("a fired event is handed out for exactly one tick", "[animation_layer][animation_events]") {
    anim_test_fixture f;
    auto root = f.create_root();
    f.create_child(root, "body");

    auto clip = f.make_clip("swing", {"body"});
    clip->set_events({{0.0F, "start", ""}, {0.5F, "half", "left"}, {1.0F, "end", ""}});

    auto layer = f.anim_sys.modify_player(root).layer(0);
    layer.blend_to(clip);
    layer.set_loop_mode(animation_loop_mode::once);
    layer.play();
    f.anim_sys.set_target_fps(60.0f);

    const auto& comp = f.reg.get<animation_player_component>(root);

    f.anim_sys.update(1.0f / 60.0f);
    REQUIRE(comp.get_fired_events().size() == 1);
    REQUIRE(comp.get_fired_events().front() == fired_animation_event{
        .layer = 0, .clip = "swing", .name = "start", .payload = ""
    });

    f.anim_sys.update(1.0f / 60.0f);
    REQUIRE(comp.get_fired_events().empty());

    std::vector<std::string> later;
    std::string half_payload;
    for (int i = 0; i < 70; ++i) {
        f.anim_sys.update(1.0f / 60.0f);
        for (const auto& event : comp.get_fired_events()) {
            later.push_back(event.name);
            if (event.name == "half") {
                half_payload = event.payload;
            }
        }
    }

    REQUIRE(later == std::vector<std::string>{"half", "end"});
    REQUIRE(half_payload == "left");
    REQUIRE_FALSE(comp.is_any_playing());
    REQUIRE(comp.get_fired_events().empty());
}

TEST_CASE("a tick without an animation step still clears the events", "[animation_layer][animation_events]") {
    anim_test_fixture f;
    auto root = f.create_root();
    f.create_child(root, "body");

    auto clip = f.make_clip("swing", {"body"});
    clip->set_events({{0.0F, "start", ""}});

    auto layer = f.anim_sys.modify_player(root).layer(0);
    layer.blend_to(clip);
    layer.set_loop_mode(animation_loop_mode::loop);
    layer.play();
    f.anim_sys.set_target_fps(30.0f);

    const auto& comp = f.reg.get<animation_player_component>(root);

    f.anim_sys.update(1.0f / 30.0f);
    REQUIRE(fired_names(comp) == std::vector<std::string>{"start"});

    f.anim_sys.update(1.0f / 120.0f);
    REQUIRE(comp.get_fired_events().empty());
}

TEST_CASE("a clip fading out in a crossfade fires nothing", "[animation_layer][animation_events]") {
    anim_test_fixture f;
    auto root = f.create_root();
    f.create_child(root, "body");

    auto outgoing = f.make_clip("outgoing", {"body"});
    outgoing->set_events({{0.5F, "outgoing.half", ""}});
    auto incoming = f.make_clip("incoming", {"body"});
    incoming->set_events({{0.0F, "incoming.start", ""}});

    auto layer = f.anim_sys.modify_player(root).layer(0);
    layer.blend_to(outgoing);
    layer.set_loop_mode(animation_loop_mode::loop);
    layer.play();
    f.anim_sys.set_target_fps(60.0f);

    const auto& comp = f.reg.get<animation_player_component>(root);
    for (int i = 0; i < 24; ++i) {
        f.anim_sys.update(1.0f / 60.0f);
        REQUIRE(comp.get_fired_events().empty());
    }

    transition crossfade;
    crossfade.duration = 0.5F;
    f.anim_sys.modify_player(root).layer(0).blend_to(incoming, crossfade);

    std::vector<std::string> fired;
    for (int i = 0; i < 30; ++i) {
        f.anim_sys.update(1.0f / 60.0f);
        for (const auto& name : fired_names(comp)) {
            fired.push_back(name);
        }
    }

    REQUIRE(fired == std::vector<std::string>{"incoming.start"});
}

TEST_CASE("is_any_playing reflects layer state", "[animation_layer]") {
    anim_test_fixture f;
    auto root = f.create_root();

    auto clip = f.make_clip("test", {"body"});

    auto& comp = f.reg.get<animation_player_component>(root);
    REQUIRE_FALSE(comp.is_any_playing());

    f.anim_sys.modify_player(root).layer(0).blend_to(clip);
    f.anim_sys.modify_player(root).layer(0).play();
    REQUIRE(comp.is_any_playing());

    f.anim_sys.modify_player(root).layer(0).stop();
    REQUIRE_FALSE(comp.is_any_playing());
}
