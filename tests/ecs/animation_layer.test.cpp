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
