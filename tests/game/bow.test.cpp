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

constexpr int32 units_per_voxel = 8;
constexpr float32 tick_seconds  = 0.016F;

struct archer {
    job_system jobs;
    ecs::world world;
    asset::vox_parser_plain parser;
    asset::model_library library{
        world.resource<asset::model_registry>(), world.voxel_types(), VW_ASSET_DIR
    };
    asset::asset_storage assets{parser, library};
    ecs::entity player;

    archer() {
        assets.load_prefab("p_humanoid", asset::asset_ref{"prefabs/p_humanoid.vox"});
        assets.load_prefab("p_sword", asset::asset_ref{"prefabs/p_sword.vox"});
        assets.load_prefab("p_shield", asset::asset_ref{"prefabs/p_shield.vox"});
        assets.load_prefab("p_bow", asset::asset_ref{"prefabs/p_bow.vox"});
        assets.load_prefab("p_arrow", asset::asset_ref{"prefabs/p_arrow.vox"});
        game::install_systems(world, assets);
        start_streaming_();

        player = world.system<game::player_system>().spawn();
        world.system<ecs::world_grid_system>().modify_view(player).set_view_distance(2);
        world.system<game::input_system>().control_locally(player);
        world.system<game::surface_placement_system>().place(player, {40.0F, 24.0F}, 1.0F);

        settle_();
        tap(keys::KEY_2);
        run_for(0.2F);
    }

    auto tick() -> void {
        world.update(tick_seconds);
        world.clear_changed();
    }

    auto run_for(float32 seconds) -> void {
        for (float32 elapsed = 0.0F; elapsed < seconds; elapsed += tick_seconds) {
            tick();
        }
    }

    auto tap(keys key) -> void {
        mapper().key(key, true);
        tick();
        mapper().key(key, false);
        tick();
    }

    auto pull(bool down) -> void {
        mapper().button(mouse::buttons::LEFT, down);
    }

    [[nodiscard]] auto mapper() -> game::input_mapper& {
        return world.system<game::input_system>().mapper();
    }

    [[nodiscard]] auto state() const -> const game::player_component& {
        return world.get<game::player_component>(player);
    }

    [[nodiscard]] auto shots() -> game::projectile_system& {
        return world.system<game::projectile_system>();
    }

    [[nodiscard]] auto action_state() const -> const std::string& {
        return world.get<ecs::animation_fsm_component>(player).get_machine(1).get_current_state();
    }

    [[nodiscard]] auto wait_for_arrow(float32 at_most_seconds) -> float32 {
        float32 waited = 0.0F;
        while (state().get_arrows_loosed() == 0 && waited < at_most_seconds) {
            tick();
            waited += tick_seconds;
        }
        return waited;
    }

    [[nodiscard]] auto flying_velocity() -> std::optional<vec3f> {
        std::optional<vec3f> found;
        world.for_each<game::projectile_component>(
            [&](ecs::entity, const game::projectile_component& shot) {
                if (!shot.is_stuck()) {
                    found = shot.get_velocity();
                }
            }
        );
        return found;
    }

private:
    auto settle_() -> void {
        auto& placement = world.system<game::surface_placement_system>();
        for (int32 frame = 0; frame < 4000 && placement.is_waiting(player); ++frame) {
            tick();
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        const auto& streaming = world.system<ecs::world_grid_system>();
        for (int32 frame = 0; frame < 20000 && streaming.get_stats().pending_count > 0; ++frame) {
            tick();
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        const auto grounded = [&] {
            return world.get<ecs::rigid_body_component>(player).is_grounded();
        };
        for (float32 waited = 0.0F; waited < 3.0F && !grounded(); waited += tick_seconds) {
            tick();
        }
        run_for(0.1F);
    }

    auto start_streaming_() -> void {
        auto& models = world.resource<asset::model_registry>();
        auto& grid   = world.system<ecs::world_grid_system>();

        ecs::perlin_terrain_generator::params terrain{};
        terrain.world_bottom_y = -192;
        terrain.island         = false;

        ecs::plains_biome level{};
        level.height   = 0.0F;
        terrain.biomes = {level};

        grid.set_grid(std::make_unique<ecs::world_grid>(world, units_per_voxel));
        grid.set_loader(
            std::make_unique<ecs::chunk_loader>(
                std::make_unique<ecs::perlin_terrain_generator>(
                    models.get_identity_pool(), models.get_page_pool(), terrain
                ),
                jobs
            )
        );

        const auto viewer = world.create()
                                .with<ecs::transform_component>()
                                .with<ecs::world_view_component>()
                                .get_entity();
        grid.modify_view(viewer).set_view_distance(2);
    }
};

}  // namespace

TEST_CASE("the bow and the sword with the shield take each other's place", "[game][bow]") {
    archer a;
    REQUIRE(a.state().has_bow());
    REQUIRE(a.state().get_loadout() == game::loadout::bow);
    REQUIRE_FALSE(a.state().has_weapon());

    a.tap(keys::KEY_1);
    REQUIRE(a.state().get_loadout() == game::loadout::melee);
    REQUIRE(a.state().has_weapon());
    REQUIRE(a.state().has_shield());
    REQUIRE_FALSE(a.state().has_bow());

    a.tap(keys::KEY_2);
    REQUIRE(a.state().has_bow());
    REQUIRE_FALSE(a.state().has_weapon());
    REQUIRE_FALSE(a.state().has_shield());

    a.tap(keys::KEY_2);
    REQUIRE(a.state().get_loadout() == game::loadout::unarmed);
    REQUIRE_FALSE(a.state().has_bow());
}

TEST_CASE("a held pull draws the bow to the full and the arrow leaves on release", "[game][bow]") {
    archer a;

    a.pull(true);
    a.run_for(0.2F);
    REQUIRE(a.state().get_bow_phase() == game::bow_phase::drawing);
    REQUIRE(a.state().has_nocked_arrow());
    REQUIRE(a.state().get_draw_share() > 0.1F);
    REQUIRE(a.state().get_draw_share() < 0.5F);
    REQUIRE(a.action_state() == "bow_draw");

    a.run_for(1.0F);
    REQUIRE(a.state().get_bow_phase() == game::bow_phase::holding);
    REQUIRE(a.state().get_draw_share() == Catch::Approx(1.0F));
    REQUIRE(a.action_state() == "bow_hold");
    REQUIRE(a.state().get_arrows_loosed() == 0);

    a.pull(false);
    a.tick();
    REQUIRE(a.state().get_bow_phase() == game::bow_phase::releasing);
    REQUIRE(a.state().get_arrows_loosed() == 0);

    REQUIRE(a.wait_for_arrow(1.0F) < 1.0F);
    REQUIRE(a.state().get_arrows_loosed() == 1);
    REQUIRE(a.shots().get_launched_count() == 1);
    REQUIRE_FALSE(a.state().has_nocked_arrow());
    REQUIRE(a.state().get_shot_power() == Catch::Approx(1.0F));

    a.run_for(1.5F);
    REQUIRE(a.state().get_bow_phase() == game::bow_phase::rest);
    REQUIRE(a.action_state() == "none");
    REQUIRE(a.state().get_arrows_loosed() == 1);
}

TEST_CASE("a quick click still draws a little before the arrow goes, and goes weaker", "[game][bow]") {
    archer a;
    const auto& tuning = a.world.system<game::player_system>().tuning();

    a.pull(true);
    a.tick();
    a.pull(false);
    a.run_for(tuning.bow_quick_draw_seconds * 0.6F);
    REQUIRE(a.state().get_bow_phase() == game::bow_phase::drawing);
    REQUIRE(a.state().get_arrows_loosed() == 0);

    REQUIRE(a.wait_for_arrow(1.0F) < 1.0F);
    const float32 weakest = tuning.bow_quick_draw_seconds / tuning.bow_full_draw_seconds;
    REQUIRE(a.state().get_shot_power() >= weakest);
    REQUIRE(a.state().get_shot_power() < weakest + 0.1F);

    a.tick();
    const auto quick = a.flying_velocity();
    REQUIRE(quick.has_value());
    REQUIRE(math::length(*quick) == Catch::Approx(tuning.bow_quick_arrow_speed).epsilon(0.08));
}

TEST_CASE("a full draw sends the arrow faster and along the look", "[game][bow]") {
    archer a;
    const auto& tuning = a.world.system<game::player_system>().tuning();

    a.pull(true);
    a.run_for(1.2F);
    a.pull(false);
    REQUIRE(a.wait_for_arrow(1.0F) < 1.0F);
    a.tick();

    const auto full = a.flying_velocity();
    REQUIRE(full.has_value());
    REQUIRE(math::length(*full) == Catch::Approx(tuning.bow_full_arrow_speed).epsilon(0.03));

    const vec3f look =
        a.world.get<game::player_input_component>(a.player).get_frame().look_direction();
    REQUIRE(math::dot(math::normalize(*full), look) > 0.9F);
}

TEST_CASE("the next arrow can be drawn before the release has played out", "[game][bow]") {
    archer a;

    a.pull(true);
    a.tick();
    a.pull(false);
    REQUIRE(a.wait_for_arrow(1.0F) < 1.0F);

    float32 waited = 0.0F;
    while (a.state().get_arrows_loosed() < 2 && waited < 3.0F) {
        a.pull(true);
        a.tick();
        a.pull(false);
        a.tick();
        waited += 2.0F * tick_seconds;
    }
    REQUIRE(a.state().get_arrows_loosed() == 2);
    REQUIRE(a.shots().get_launched_count() == 2);
}

TEST_CASE("a dodge drops the draw and no arrow leaves", "[game][bow]") {
    archer a;

    a.pull(true);
    a.run_for(0.4F);
    REQUIRE(a.state().get_bow_phase() == game::bow_phase::drawing);

    a.mapper().key(keys::LEFT_SHIFT, true);
    a.tick();
    a.mapper().key(keys::LEFT_SHIFT, false);
    a.tick();

    REQUIRE(a.state().is_dodging());
    REQUIRE(a.state().get_bow_phase() == game::bow_phase::rest);
    REQUIRE_FALSE(a.state().has_nocked_arrow());

    a.pull(false);
    a.run_for(1.5F);
    REQUIRE(a.state().get_arrows_loosed() == 0);
    REQUIRE(a.shots().get_launched_count() == 0);
}

TEST_CASE("drawing or aiming puts the archer in the stance, facing the look", "[game][bow]") {
    archer a;
    REQUIRE_FALSE(a.state().is_in_stance());

    a.mapper().cursor_at(0.0, 0.0);
    a.tick();
    a.mapper().cursor_at(900.0, 0.0);
    a.tick();

    a.mapper().button(mouse::buttons::RIGHT, true);
    a.run_for(0.6F);
    REQUIRE(a.state().is_aiming());
    REQUIRE(a.state().is_in_stance());
    REQUIRE(a.state().is_braced());
    REQUIRE_FALSE(a.state().is_guarding());

    const vec3f look =
        a.world.get<game::player_input_component>(a.player).get_frame().look_forward_flat();
    const auto rotation = a.world.get<ecs::transform_component>(a.player).get_rotation();
    REQUIRE(std::abs(math::dot(rotation, math::quat_look_y(look))) > 0.999F);

    a.mapper().button(mouse::buttons::RIGHT, false);
    a.run_for(0.2F);
    REQUIRE_FALSE(a.state().is_aiming());
    REQUIRE_FALSE(a.state().is_in_stance());

    a.pull(true);
    a.run_for(0.2F);
    REQUIRE(a.state().is_in_stance());
    REQUIRE_FALSE(a.state().is_aiming());
}

TEST_CASE("the arrow is sighted from where the app says the eye is", "[game][bow]") {
    archer a;
    auto& input = a.world.system<game::input_system>();
    REQUIRE_FALSE(a.world.get<game::player_input_component>(a.player).get_aim_origin().has_value());

    const vec3f feet = a.world.get<ecs::transform_component>(a.player).get_position();
    const vec3f eye  = feet + vec3f{0.0F, 150.0F, 0.0F};
    input.aim_from(a.player, eye);
    REQUIRE(a.world.get<game::player_input_component>(a.player).get_aim_origin() == eye);

    a.pull(true);
    a.run_for(1.2F);
    a.pull(false);
    REQUIRE(a.wait_for_arrow(1.0F) < 1.0F);
    a.tick();

    const auto loosed = a.flying_velocity();
    REQUIRE(loosed.has_value());
    REQUIRE(math::normalize(*loosed).y > -0.08F);
}

TEST_CASE("walking off a ledge keeps the draw and the aim", "[game][bow]") {
    archer a;

    a.mapper().button(mouse::buttons::RIGHT, true);
    a.pull(true);
    a.run_for(0.3F);
    REQUIRE(a.state().get_bow_phase() == game::bow_phase::drawing);
    REQUIRE(a.state().is_aiming());

    const vec3f stood = a.world.get<ecs::transform_component>(a.player).get_position();
    a.world.system<ecs::transform_system>().modify(a.player).set_position(
        stood + vec3f{0.0F, 40.0F, 0.0F}
    );
    a.run_for(0.2F);

    REQUIRE(a.state().get_air_state() == game::air_state::falling);
    REQUIRE(a.state().get_bow_phase() != game::bow_phase::rest);
    REQUIRE(a.state().has_nocked_arrow());
    REQUIRE(a.state().is_aiming());
    REQUIRE(a.state().is_in_stance());

    a.run_for(1.0F);
    REQUIRE(a.state().get_air_state() == game::air_state::ground);
    REQUIRE(a.state().get_bow_phase() == game::bow_phase::holding);

    a.pull(false);
    REQUIRE(a.wait_for_arrow(1.0F) < 1.0F);
}

TEST_CASE("the archer still hops onto a step while aiming or drawing", "[game][bow]") {
    archer a;
    const auto hop = [&] {
        return a.world.get<ecs::character_controller_component>(a.player).get_step_hop_voxels();
    };
    const float32 free_hop = hop();
    REQUIRE(free_hop > 0.0F);

    a.mapper().button(mouse::buttons::RIGHT, true);
    a.run_for(0.2F);
    REQUIRE(a.state().is_aiming());
    REQUIRE(hop() == free_hop);

    a.mapper().button(mouse::buttons::RIGHT, false);
    a.pull(true);
    a.run_for(0.2F);
    REQUIRE(a.state().get_bow_phase() == game::bow_phase::drawing);
    REQUIRE(hop() == free_hop);
}

TEST_CASE("a jump drops the draw, and a pull still held draws again on landing", "[game][bow]") {
    archer a;

    a.mapper().button(mouse::buttons::RIGHT, true);
    a.pull(true);
    a.run_for(0.4F);
    REQUIRE(a.state().get_bow_phase() == game::bow_phase::drawing);

    a.mapper().key(keys::SPACE, true);
    a.tick();
    a.mapper().key(keys::SPACE, false);
    a.run_for(0.1F);

    REQUIRE(a.state().get_air_state() != game::air_state::ground);
    REQUIRE(a.state().get_bow_phase() == game::bow_phase::rest);
    REQUIRE_FALSE(a.state().has_nocked_arrow());
    REQUIRE_FALSE(a.state().is_aiming());

    float32 waited = 0.0F;
    while (a.state().get_air_state() != game::air_state::ground && waited < 3.0F) {
        a.tick();
        waited += tick_seconds;
    }
    a.run_for(0.1F);

    REQUIRE(a.state().is_aiming());
    REQUIRE(a.state().get_bow_phase() == game::bow_phase::drawing);
    REQUIRE(a.state().has_nocked_arrow());
    REQUIRE(a.shots().get_launched_count() == 0);
}

TEST_CASE("a pull held through a dodge draws again when the dodge ends", "[game][bow]") {
    archer a;

    a.pull(true);
    a.run_for(0.4F);

    a.mapper().key(keys::LEFT_SHIFT, true);
    a.tick();
    a.mapper().key(keys::LEFT_SHIFT, false);
    a.tick();
    REQUIRE(a.state().is_dodging());
    REQUIRE(a.state().get_bow_phase() == game::bow_phase::rest);

    a.run_for(1.2F);
    REQUIRE_FALSE(a.state().is_dodging());
    REQUIRE(a.state().get_bow_phase() != game::bow_phase::rest);
    REQUIRE(a.shots().get_launched_count() == 0);

    a.pull(false);
    REQUIRE(a.wait_for_arrow(1.0F) < 1.0F);
}

TEST_CASE("the second arrow in a row is drawn without lowering the bow", "[game][bow]") {
    archer a;

    a.pull(true);
    a.tick();
    a.pull(false);
    REQUIRE(a.wait_for_arrow(1.0F) < 1.0F);
    a.run_for(0.3F);

    a.pull(true);
    a.run_for(0.1F);
    REQUIRE(a.state().get_bow_phase() == game::bow_phase::drawing);
    REQUIRE(a.action_state() == "bow_redraw");
}
