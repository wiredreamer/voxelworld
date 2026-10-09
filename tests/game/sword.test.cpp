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

struct fencer {
    job_system jobs;
    ecs::world world;
    asset::vox_parser_plain parser;
    asset::model_library library{
        world.resource<asset::model_registry>(), world.voxel_types(), VW_ASSET_DIR
    };
    asset::asset_storage assets{parser, library};
    ecs::entity player;

    explicit fencer(bool armed = true) {
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
        if (armed) {
            tap(keys::KEY_1);
            run_for(0.2F);
        }
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

    auto hold(bool down) -> void {
        mapper().button(mouse::buttons::LEFT, down);
    }

    auto click() -> void {
        hold(true);
        tick();
        hold(false);
    }

    auto ticks_until(const std::function<bool()>& done, int32 at_most = 300) -> int32 {
        int32 count = 0;
        while (!done() && count < at_most) {
            tick();
            ++count;
        }
        return count;
    }

    [[nodiscard]] auto mapper() -> game::input_mapper& {
        return world.system<game::input_system>().mapper();
    }

    [[nodiscard]] auto state() const -> const game::player_component& {
        return world.get<game::player_component>(player);
    }

    [[nodiscard]] auto tuning() -> game::movement_tuning& {
        return world.system<game::player_system>().tuning();
    }

    [[nodiscard]] auto action_state() const -> const std::string& {
        return world.get<ecs::animation_fsm_component>(player).get_machine(1).get_current_state();
    }

    [[nodiscard]] auto body_state() const -> const std::string& {
        return world.get<ecs::animation_fsm_component>(player).get_machine(0).get_current_state();
    }

    [[nodiscard]] auto players() -> game::player_system& {
        return world.system<game::player_system>();
    }

    [[nodiscard]] auto step() -> float32 {
        tick();
        const auto wish = world.get<ecs::movement_intent_component>(player).get_wish_velocity();
        return std::sqrt((wish.x * wish.x) + (wish.z * wish.z)) * tick_seconds;
    }

private:
    auto settle_() -> void {
        auto& placement = world.system<game::surface_placement_system>();
        for (int32 frame = 0; frame < 4000 && placement.is_waiting(player); ++frame) {
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

TEST_CASE("a quick click swings the chain and never charges", "[game][sword]") {
    fencer f;

    f.click();
    f.tick();
    REQUIRE(f.state().get_strike() == game::strike_kind::light);
    REQUIRE(f.action_state() == "attack_1");

    int32 hit_frames = 0;
    f.ticks_until([&] {
        hit_frames += f.state().is_hitting() ? 1 : 0;
        return !f.state().is_swinging();
    });

    REQUIRE(hit_frames > 0);
    REQUIRE(f.state().get_charge_count() == 0);
    REQUIRE_FALSE(f.state().is_charging());
    REQUIRE(f.state().get_strike() == game::strike_kind::none);
}

TEST_CASE("a held press turns the first swing into a charge that strikes on release", "[game][sword]") {
    fencer f;

    f.hold(true);
    float32 travelled = 0.0F;
    int32 hit_frames  = 0;
    const int32 to_charge = f.ticks_until([&] {
        hit_frames += f.state().is_hitting() ? 1 : 0;
        return f.state().is_charging();
    });
    REQUIRE(f.state().is_charging());
    REQUIRE(static_cast<float32>(to_charge) * tick_seconds ==
            Catch::Approx(f.tuning().charge_decide_seconds).margin(0.04F));

    for (int32 frame = 0; frame < 90; ++frame) {
        travelled += f.step();
        hit_frames += f.state().is_hitting() ? 1 : 0;
    }
    REQUIRE(f.state().is_charging());
    REQUIRE(f.state().is_charge_ready());
    REQUIRE_FALSE(f.state().is_swinging());
    REQUIRE(f.action_state() == "charge");
    REQUIRE(f.state().get_swing_count() == 1);
    REQUIRE(hit_frames == 0);
    REQUIRE(travelled == Catch::Approx(0.0F).margin(1.0e-3F));
    REQUIRE(f.state().is_attacking());

    f.hold(false);
    f.tick();
    REQUIRE_FALSE(f.state().is_charging());
    REQUIRE(f.state().is_swinging());
    REQUIRE(f.state().is_heavy_strike());
    REQUIRE(f.state().get_swing_count() == 2);
    f.tick();
    REQUIRE(f.action_state() == "charged");

    float32 lunged = 0.0F;
    for (int32 frame = 0; frame < 300 && f.state().is_swinging(); ++frame) {
        lunged += f.step();
        hit_frames += f.state().is_hitting() ? 1 : 0;
    }
    REQUIRE(hit_frames > 0);
    REQUIRE(lunged == Catch::Approx(f.tuning().charged_lunge_distance).margin(2.0F));
    REQUIRE(f.state().get_heavy_strike_count() == 1);

    f.click();
    f.tick();
    REQUIRE(f.state().get_chain_step() == 1);
}

TEST_CASE("a charge let go before it is ready strikes overhead but not heavy", "[game][sword]") {
    fencer f;

    f.hold(true);
    f.ticks_until([&] { return f.state().is_charging(); });
    REQUIRE(f.state().is_charging());
    REQUIRE_FALSE(f.state().is_charge_ready());

    f.hold(false);
    f.tick();

    REQUIRE(f.state().is_swinging());
    REQUIRE(f.state().get_strike() == game::strike_kind::overhead);
    REQUIRE_FALSE(f.state().is_heavy_strike());
    REQUIRE(f.state().get_heavy_strike_count() == 0);
}

TEST_CASE("the charge is ready when its clip says so or the timer runs out", "[game][sword]") {
    fencer f;

    f.hold(true);
    const int32 waited = f.ticks_until([&] { return f.state().is_charge_ready(); });

    REQUIRE(f.state().is_charge_ready());
    REQUIRE(static_cast<float32>(waited) * tick_seconds <= f.tuning().charge_full_seconds + 0.05F);
    REQUIRE(static_cast<float32>(waited) * tick_seconds > f.tuning().charge_decide_seconds);
}

TEST_CASE("the charged pose is held for as long as the button is", "[game][sword]") {
    fencer f;

    f.hold(true);
    f.ticks_until([&] { return f.state().is_charge_ready(); });
    f.run_for(0.3F);
    constexpr std::size_t sword_hand = 2;
    const auto hand =
        f.world.get<ecs::hierarchy_component>(f.state().get_pose()).get_children()[sword_hand];
    const auto where = [&] { return f.world.get<ecs::transform_component>(hand).get_position(); };
    const vec3f held = where();

    f.run_for(2.0F);

    REQUIRE(f.state().is_charging());
    REQUIRE(f.action_state() == "charge");
    REQUIRE(math::length(where() - held) < 0.05F);
}

TEST_CASE("a charge is gathered in the air and through a jump", "[game][sword]") {
    fencer leaper;
    leaper.tap(keys::SPACE);
    leaper.run_for(0.05F);
    REQUIRE_FALSE(leaper.world.get<ecs::rigid_body_component>(leaper.player).is_grounded());

    leaper.hold(true);
    leaper.ticks_until([&] { return leaper.state().is_charging(); });
    REQUIRE(leaper.state().is_charging());
    REQUIRE_FALSE(leaper.world.get<ecs::rigid_body_component>(leaper.player).is_grounded());
    leaper.run_for(0.1F);
    REQUIRE(leaper.state().is_charging());
    REQUIRE_FALSE(leaper.state().is_in_stance());

    leaper.hold(false);
    leaper.tick();
    REQUIRE(leaper.state().is_swinging());
    REQUIRE(leaper.state().get_strike() != game::strike_kind::light);

    fencer f;
    f.hold(true);
    f.ticks_until([&] { return f.state().is_charging(); });
    f.tap(keys::SPACE);
    f.run_for(0.1F);
    REQUIRE(f.state().is_charging());
}

TEST_CASE("a charged strike goes where the fencer looks, not where it strafes", "[game][sword]") {
    fencer f;
    f.mapper().key(keys::D, true);
    f.hold(true);
    f.ticks_until([&] { return f.state().is_charge_ready(); });
    f.run_for(0.2F);

    const auto look     = f.world.get<game::player_input_component>(f.player).get_frame().look_direction();
    const vec3f forward = math::normalize(vec3f{look.x, 0.0F, look.z});
    f.hold(false);
    f.tick();

    REQUIRE(f.state().is_heavy_strike());
    REQUIRE(math::dot(f.state().get_attack_direction(), forward) > 0.99F);
}

TEST_CASE("a dodge drops the charge without a strike", "[game][sword]") {
    for (const keys escape : {keys::LEFT_SHIFT}) {
        fencer f;

        f.hold(true);
        f.ticks_until([&] { return f.state().is_charging(); });
        REQUIRE(f.state().is_charging());

        f.mapper().key(escape, true);
        f.tick();
        f.mapper().key(escape, false);
        f.run_for(0.1F);
        REQUIRE_FALSE(f.state().is_charging());
        REQUIRE_FALSE(f.state().is_swinging());

        f.hold(false);
        f.run_for(1.5F);
        REQUIRE(f.state().get_swing_count() == 1);
        REQUIRE(f.state().get_heavy_strike_count() == 0);
        REQUIRE(f.action_state() == "none");
    }
}

TEST_CASE("holding the second strike of the chain does not charge", "[game][sword]") {
    fencer f;

    f.click();
    f.ticks_until([&] { return f.state().can_cancel(); });
    REQUIRE(f.state().can_cancel());

    f.hold(true);
    f.run_for(1.0F);

    REQUIRE(f.state().get_swing_count() == 2);
    REQUIRE(f.state().get_charge_count() == 0);
    REQUIRE_FALSE(f.state().is_charging());
}

TEST_CASE("the whirl and the pommel strike need a sword in the hand", "[game][sword]") {
    fencer bare{false};
    bare.tap(keys::Q);
    bare.tap(keys::E);
    bare.run_for(0.3F);
    REQUIRE(bare.state().get_whirl_count() == 0);
    REQUIRE(bare.state().get_pommel_count() == 0);
    REQUIRE(bare.action_state() == "none");

    fencer archer{false};
    archer.tap(keys::KEY_2);
    archer.run_for(0.2F);
    archer.tap(keys::Q);
    archer.tap(keys::E);
    archer.run_for(0.3F);
    REQUIRE(archer.state().get_whirl_count() == 0);
    REQUIRE(archer.state().get_pommel_count() == 0);

    fencer f;
    f.tap(keys::Q);
    REQUIRE(f.state().get_whirl_count() == 1);
    REQUIRE(f.state().get_strike() == game::strike_kind::whirl);
    REQUIRE(f.action_state() == "whirl");

    f.ticks_until([&] { return f.state().can_cancel(); });
    REQUIRE(f.state().can_cancel());
    f.tap(keys::E);
    REQUIRE(f.state().get_pommel_count() == 1);
    REQUIRE(f.state().get_strike() == game::strike_kind::pommel);
    REQUIRE(f.action_state() == "pommel");
    REQUIRE(f.state().get_chain_step() == 0);
}

TEST_CASE("an ability pressed mid-swing waits for the cancel window", "[game][sword]") {
    fencer f;
    f.tuning().input_buffer_seconds = 0.03F;

    f.click();
    f.ticks_until([&] { return f.state().is_hitting(); });
    f.tap(keys::Q);
    REQUIRE(f.state().get_whirl_count() == 0);

    f.ticks_until([&] { return f.state().can_cancel(); });
    f.tap(keys::Q);
    REQUIRE(f.state().get_whirl_count() == 1);
}

TEST_CASE("the whirl turns the body full turns around and leaves it as it was", "[game][sword]") {
    fencer f;
    const auto facing_before = f.world.get<ecs::transform_component>(f.player).get_rotation();

    f.tap(keys::Q);
    float32 furthest   = 0.0F;
    float32 widest_hop = 0.0F;
    float32 previous   = 0.0F;
    float32 last_hop   = 0.0F;
    float32 sharpest   = 0.0F;
    int32 hit_frames   = 0;
    std::set<std::string> states;
    std::set<game::whirl_phase> phases;
    f.ticks_until([&] {
        const float32 now = f.state().get_whirl_turn_degrees();
        const float32 hop = std::remainder(now - previous, 360.0F);
        widest_hop        = std::max(widest_hop, std::abs(hop));
        sharpest          = std::max(sharpest, std::abs(hop - last_hop));
        last_hop          = hop;
        states.insert(std::string{f.action_state()});
        phases.insert(f.state().get_whirl_phase());
        furthest          = std::max(furthest, std::abs(now));
        previous          = now;
        hit_frames += f.state().is_hitting() ? 1 : 0;
        return !f.state().is_swinging();
    });

    REQUIRE(hit_frames > 0);
    REQUIRE(furthest > (f.tuning().whirl_turns * 360.0F) - 60.0F);
    REQUIRE(widest_hop < 30.0F);
    REQUIRE(sharpest < 1.0F);
    REQUIRE(states.contains("whirl"));
    REQUIRE(states.contains("whirl_spin"));
    REQUIRE(states.contains("whirl_end"));
    REQUIRE(phases.contains(game::whirl_phase::gathering));
    REQUIRE(phases.contains(game::whirl_phase::spinning));
    REQUIRE(phases.contains(game::whirl_phase::braking));
    REQUIRE(f.state().get_whirl_turn_degrees() == Catch::Approx(0.0F).margin(1.0e-3F));

    const auto pose = f.world.get<ecs::transform_component>(f.state().get_pose()).get_rotation();
    REQUIRE(std::abs(pose.y) < 0.01F);
    const auto facing_after = f.world.get<ecs::transform_component>(f.player).get_rotation();
    REQUIRE(math::approx_equal(facing_before, facing_after));
}

TEST_CASE("a whirl of many turns is not cut short", "[game][sword]") {
    fencer f;
    f.tuning().whirl_turns = 5.0F;

    f.tap(keys::Q);
    float32 furthest = 0.0F;
    f.ticks_until([&] {
        furthest = std::max(furthest, std::abs(f.state().get_whirl_turn_degrees()));
        return !f.state().is_swinging();
    }, 900);

    REQUIRE(furthest > (5.0F * 360.0F) - 60.0F);
    REQUIRE(f.state().get_whirl_turn_degrees() == Catch::Approx(0.0F).margin(1.0e-3F));
}

TEST_CASE("the pommel strike takes a step forward", "[game][sword]") {
    fencer f;

    f.tap(keys::E);
    REQUIRE(f.state().get_strike() == game::strike_kind::pommel);

    float32 travelled = 0.0F;
    int32 hit_frames  = 0;
    for (int32 frame = 0; frame < 300 && f.state().is_swinging(); ++frame) {
        travelled += f.step();
        hit_frames += f.state().is_hitting() ? 1 : 0;
    }

    REQUIRE(hit_frames > 0);
    REQUIRE(travelled == Catch::Approx(f.tuning().pommel_lunge_distance).margin(1.5F));
}

TEST_CASE("the charge lets the fencer walk at a slowed pace", "[game][sword]") {
    fencer f;
    const float32 speed = f.world.get<ecs::character_controller_component>(f.player).get_move_speed();

    f.mapper().key(keys::W, true);
    f.hold(true);
    f.ticks_until([&] { return f.state().is_charge_ready(); });
    f.run_for(0.3F);
    REQUIRE(f.state().is_charging());

    const auto wish    = f.world.get<ecs::movement_intent_component>(f.player).get_wish_velocity();
    const float32 pace = std::sqrt((wish.x * wish.x) + (wish.z * wish.z));
    REQUIRE(f.state().is_in_stance());
    REQUIRE_FALSE(f.state().is_guarding());
    REQUIRE(
        pace ==
        Catch::Approx(speed * f.tuning().guard_speed_scale * f.tuning().charge_move_scale)
            .margin(speed * 0.1F)
    );
}

TEST_CASE("the whirl lets the fencer walk at half pace", "[game][sword]") {
    fencer f;
    const float32 speed = f.world.get<ecs::character_controller_component>(f.player).get_move_speed();

    f.mapper().key(keys::W, true);
    f.run_for(0.6F);
    f.tap(keys::Q);
    f.run_for(0.25F);
    REQUIRE(f.state().get_strike() == game::strike_kind::whirl);

    const auto wish    = f.world.get<ecs::movement_intent_component>(f.player).get_wish_velocity();
    const float32 pace = std::sqrt((wish.x * wish.x) + (wish.z * wish.z));
    REQUIRE(pace == Catch::Approx(speed * f.tuning().whirl_move_scale).margin(speed * 0.1F));
}

TEST_CASE("a hit staggers: it drops what the hands did and locks the body", "[game][blow]") {
    fencer f;
    f.hold(true);
    f.ticks_until([&] { return f.state().is_charging(); });
    REQUIRE(f.state().is_charging());

    REQUIRE(f.players().take_hit(f.player));
    f.tick();
    f.tick();
    REQUIRE(f.state().get_staggers() == 1);
    REQUIRE_FALSE(f.state().is_charging());
    REQUIRE(f.state().is_body_locked());
    REQUIRE(f.body_state() == "hit_react");
    f.hold(false);

    const uint32 swings = f.state().get_swing_count();
    const uint32 dodges = f.state().get_dodge_count();
    f.click();
    f.tap(keys::LEFT_SHIFT);
    f.tap(keys::Q);
    REQUIRE(f.state().get_swing_count() == swings);
    REQUIRE(f.state().get_dodge_count() == dodges);
    REQUIRE(f.state().get_whirl_count() == 0);

    f.ticks_until([&] { return !f.state().is_body_locked(); });
    REQUIRE_FALSE(f.state().is_body_locked());
    f.run_for(0.4F);
    REQUIRE(f.body_state() == "idle");

    f.click();
    f.tick();
    REQUIRE(f.state().get_swing_count() == swings + 1);
}

TEST_CASE("a dodge's invulnerable frames shrug a hit off", "[game][blow]") {
    fencer f;
    f.mapper().key(keys::LEFT_SHIFT, true);
    f.tick();
    f.mapper().key(keys::LEFT_SHIFT, false);
    f.ticks_until([&] { return f.state().is_invulnerable(); });
    REQUIRE(f.state().is_invulnerable());

    REQUIRE_FALSE(f.players().take_hit(f.player));
    f.tick();
    REQUIRE(f.state().get_staggers() == 0);
}

TEST_CASE("the guard breaks only while it is up", "[game][blow]") {
    fencer f;
    REQUIRE_FALSE(f.players().break_guard(f.player));

    f.mapper().button(mouse::buttons::RIGHT, true);
    f.run_for(0.2F);
    REQUIRE(f.state().is_guarding());
    REQUIRE(f.players().break_guard(f.player));
    f.tick();
    f.tick();

    REQUIRE(f.state().get_guard_breaks() == 1);
    REQUIRE_FALSE(f.state().is_guarding());
    REQUIRE(f.body_state() == "hit_react");
}

TEST_CASE("the dead lie still until revived", "[game][blow]") {
    fencer f;
    REQUIRE_FALSE(f.players().revive(f.player));
    REQUIRE(f.players().die(f.player));
    REQUIRE_FALSE(f.players().die(f.player));
    f.tick();
    f.tick();
    REQUIRE(f.state().is_dead());
    REQUIRE(f.body_state() == "death");
    REQUIRE_FALSE(f.players().take_hit(f.player));

    f.mapper().key(keys::W, true);
    f.mapper().button(mouse::buttons::RIGHT, true);
    f.click();
    f.tap(keys::SPACE);
    f.run_for(3.0F);
    REQUIRE(f.body_state() == "dead");
    REQUIRE(f.state().is_body_locked());
    REQUIRE_FALSE(f.state().is_in_stance());
    REQUIRE_FALSE(f.state().is_swinging());
    const auto wish = f.world.get<ecs::movement_intent_component>(f.player).get_wish_velocity();
    REQUIRE(std::abs(wish.x) + std::abs(wish.z) < 1.0e-3F);
    f.mapper().key(keys::W, false);
    f.mapper().button(mouse::buttons::RIGHT, false);

    REQUIRE(f.players().revive(f.player));
    f.run_for(0.5F);
    REQUIRE_FALSE(f.state().is_dead());
    REQUIRE_FALSE(f.state().is_body_locked());
    REQUIRE(f.body_state() == "idle");
}

TEST_CASE("the dead fall the way they were told to", "[game][blow]") {
    fencer f;
    REQUIRE(f.players().die(f.player, game::death_fall::forward));
    f.tick();
    f.tick();
    REQUIRE(f.body_state() == "death_front");
    f.run_for(1.5F);
    REQUIRE(f.body_state() == "dead_front");

    REQUIRE(f.players().revive(f.player));
    f.run_for(0.5F);
    REQUIRE(f.players().die(f.player));
    f.tick();
    f.tick();
    REQUIRE(f.body_state() == "death");
}
