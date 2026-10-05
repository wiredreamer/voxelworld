#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

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
constexpr float32 display_tick_seconds = 0.016F;
constexpr float32 drop_height   = 9.4F;

struct grounded_world {
    job_system jobs;
    ecs::world world;
    asset::vox_parser_plain parser;
    asset::model_library library{
        world.resource<asset::model_registry>(), world.voxel_types(), VW_ASSET_DIR
    };
    asset::asset_storage assets{parser, library};
    ecs::entity player;
    float32 tick_seconds;

    explicit grounded_world(float32 tick = display_tick_seconds) : tick_seconds{tick} {
        assets.load_prefab("p_humanoid", asset::asset_ref{"prefabs/p_humanoid.vox"});
        assets.load_prefab("p_sword", asset::asset_ref{"prefabs/p_sword.vox"});
        assets.load_prefab("p_shield", asset::asset_ref{"prefabs/p_shield.vox"});
        game::install_systems(world, assets);
        start_streaming_();

        player = world.system<game::player_system>().spawn();
        world.system<ecs::world_grid_system>().modify_view(player).set_view_distance(2);
        world.system<game::input_system>().control_locally(player);
        world.system<game::surface_placement_system>().place(player, {40.0F, 24.0F}, 1.0F);
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

    [[nodiscard]] auto grounded() const -> bool {
        return world.get<ecs::rigid_body_component>(player).is_grounded();
    }

    [[nodiscard]] auto jumps() const -> uint32 {
        return world.get<ecs::character_controller_component>(player).get_jump_count();
    }

    [[nodiscard]] auto settle() -> bool {
        auto& placement = world.system<game::surface_placement_system>();
        for (int32 frame = 0; frame < 4000 && placement.is_waiting(player); ++frame) {
            tick();
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        for (float32 waited = 0.0F; waited < 3.0F && !grounded(); waited += tick_seconds) {
            tick();
        }
        run_for(0.1F);
        return grounded();
    }

    auto lift(float32 height) -> void {
        auto position = world.get<ecs::transform_component>(player).get_position();
        position.y += height;
        world.system<ecs::transform_system>().modify(player).set_position(position);
    }

    auto press_jump() -> void {
        world.system<game::input_system>().mapper().key(keys::SPACE, true);
        tick();
        world.system<game::input_system>().mapper().key(keys::SPACE, false);
    }

private:
    auto start_streaming_() -> void {
        auto& models = world.resource<asset::model_registry>();
        auto& grid   = world.system<ecs::world_grid_system>();

        ecs::perlin_terrain_generator::params terrain{};
        terrain.world_bottom_y = -192;

        grid.set_grid(std::make_unique<ecs::world_grid>(world, units_per_voxel));
        grid.set_loader(
            std::make_unique<ecs::chunk_loader>(
                std::make_unique<ecs::perlin_terrain_generator>(
                    models.get_identity_pool(), models.get_page_pool(), terrain
                ),
                jobs
            ),
            jobs
        );

        const auto viewer = world.create()
                                .with<ecs::transform_component>()
                                .with<ecs::world_view_component>()
                                .get_entity();
        grid.modify_view(viewer).set_view_distance(2);
    }
};

}  // namespace

TEST_CASE("a jump is taken just after walking off the ground, not later", "[game][jump]") {
    grounded_world g;
    REQUIRE(g.settle());

    const uint32 before = g.jumps();
    g.lift(drop_height);
    g.tick();
    REQUIRE_FALSE(g.grounded());

    g.press_jump();
    REQUIRE(g.jumps() == before + 1);

    REQUIRE(g.settle());
    const uint32 landed = g.jumps();
    g.lift(drop_height * 4.0F);
    g.run_for(0.2F);
    REQUIRE_FALSE(g.grounded());

    g.press_jump();
    g.run_for(0.05F);
    REQUIRE(g.jumps() == landed);
}

TEST_CASE("a jump pressed just before landing is taken on landing", "[game][jump]") {
    grounded_world g;
    REQUIRE(g.settle());

    const uint32 before = g.jumps();
    g.lift(drop_height);
    g.run_for(0.16F);
    REQUIRE_FALSE(g.grounded());
    REQUIRE(g.jumps() == before);

    g.press_jump();
    REQUIRE(g.jumps() == before);
    g.run_for(0.2F);
    REQUIRE(g.jumps() == before + 1);
}

TEST_CASE("without a buffer the same early press is lost", "[game][jump]") {
    grounded_world g;
    g.world.system<game::player_system>().tuning().input_buffer_seconds = 0.0F;
    REQUIRE(g.settle());

    const uint32 before = g.jumps();
    g.lift(drop_height);
    g.run_for(0.16F);
    g.press_jump();
    g.run_for(0.2F);
    REQUIRE(g.jumps() == before);
}

TEST_CASE("every pressed jump lifts the body when frames outpace physics", "[game][jump]") {
    constexpr float32 fast_tick = 1.0F / 240.0F;
    grounded_world g{fast_tick};
    REQUIRE(g.settle());

    for (int32 attempt = 0; attempt < 6; ++attempt) {
        INFO("attempt " << attempt);
        const float32 floor_y = g.world.get<ecs::transform_component>(g.player).get_position().y;
        const uint32 before   = g.jumps();

        for (int32 offset = 0; offset < attempt; ++offset) {
            g.tick();
        }
        g.press_jump();
        g.run_for(0.15F);

        REQUIRE(g.jumps() == before + 1);
        REQUIRE(g.world.get<ecs::transform_component>(g.player).get_position().y > floor_y + 10.0F);
        REQUIRE(g.settle());
    }
}

TEST_CASE("a jump rises, falls and lands, a drop only falls", "[game][jump]") {
    grounded_world g{GENERATE(display_tick_seconds, 1.0F / 240.0F)};
    REQUIRE(g.settle());
    g.run_for(0.3F);

    const auto& state = g.world.get<game::player_component>(g.player);
    const auto& fsm   = g.world.get<ecs::animation_fsm_component>(g.player);
    const auto phases = [&](float32 seconds) {
        std::vector<std::string> seen{"idle"};
        for (float32 elapsed = 0.0F; elapsed < seconds; elapsed += g.tick_seconds) {
            g.tick();
            const auto& current = fsm.get_machine(0).get_current_state();
            if (seen.back() != current) {
                seen.push_back(current);
            }
        }
        return seen;
    };

    REQUIRE(state.get_air_state() == game::air_state::ground);
    REQUIRE(fsm.get_machine(0).get_current_state() == "idle");

    g.press_jump();
    REQUIRE(phases(2.0F) == std::vector<std::string>{"idle", "rise", "fall", "land", "idle"});
    REQUIRE(state.get_air_state() == game::air_state::ground);

    g.lift(drop_height * 2.0F);
    REQUIRE(phases(2.0F) == std::vector<std::string>{"idle", "fall", "land", "idle"});

    g.lift(drop_height * 8.0F);
    REQUIRE(phases(3.0F) == std::vector<std::string>{"idle", "fall", "land_hard", "idle"});
}

TEST_CASE("a hard landing takes control away until its clip gives it back", "[game][jump]") {
    grounded_world g{GENERATE(display_tick_seconds, 1.0F / 240.0F)};
    REQUIRE(g.settle());
    g.run_for(0.3F);

    const auto& state = g.world.get<game::player_component>(g.player);
    g.lift(drop_height * 8.0F);
    for (int32 tick = 0; tick < 400 && state.get_hard_landings() == 0; ++tick) {
        g.tick();
    }
    REQUIRE(state.get_hard_landings() == 1);

    g.world.system<game::input_system>().mapper().key(keys::W, true);
    float32 locked_for = 0.0F;
    for (float32 elapsed = 0.0F; elapsed < 0.9F; elapsed += g.tick_seconds) {
        g.tick();
        if (state.is_body_locked()) {
            locked_for += g.tick_seconds;
            const auto& wish = g.world.get<ecs::movement_intent_component>(g.player).get_wish_velocity();
            REQUIRE(std::abs(wish.z) < 5.0F);
        }
    }

    REQUIRE(locked_for > 0.5F);
    REQUIRE(locked_for < 0.7F);
    REQUIRE_FALSE(state.is_body_locked());
    const auto& wish = g.world.get<ecs::movement_intent_component>(g.player).get_wish_velocity();
    REQUIRE(std::abs(wish.z) > 90.0F);
}

TEST_CASE("a landing is soft from a jump or a short drop and hard from a tall one", "[game][jump]") {
    grounded_world g{GENERATE(display_tick_seconds, 1.0F / 240.0F)};
    REQUIRE(g.settle());
    g.run_for(0.3F);

    const auto& state = g.world.get<game::player_component>(g.player);
    const uint32 soft = state.get_soft_landings();
    const uint32 hard = state.get_hard_landings();

    g.press_jump();
    g.run_for(0.1F);
    REQUIRE(g.settle());
    REQUIRE(state.get_soft_landings() == soft + 1);
    REQUIRE(state.get_hard_landings() == hard);

    g.lift(drop_height * 2.0F);
    g.run_for(0.15F);
    REQUIRE(g.settle());
    REQUIRE(state.get_soft_landings() == soft + 2);

    g.lift(drop_height * 8.0F);
    g.run_for(0.15F);
    REQUIRE(g.settle());
    REQUIRE(state.get_soft_landings() == soft + 2);
    REQUIRE(state.get_hard_landings() == hard + 1);
}

TEST_CASE("a jump rises through its takeoff and cannot jump again when frames outpace physics", "[game][jump]") {
    constexpr float32 fast_tick = 1.0F / 240.0F;
    grounded_world g{fast_tick};
    REQUIRE(g.settle());
    g.run_for(0.3F);

    const auto& fsm = g.world.get<ecs::animation_fsm_component>(g.player);
    const uint32 before = g.jumps();

    g.press_jump();
    bool rose = false;
    for (float32 elapsed = 0.0F; elapsed < 0.2F; elapsed += fast_tick) {
        g.tick();
        rose = rose || fsm.get_machine(0).get_current_state() == "rise";
    }
    REQUIRE(rose);

    g.run_for(0.02F);
    g.press_jump();
    g.run_for(0.1F);
    REQUIRE(g.jumps() == before + 1);
}

TEST_CASE("a running jump splits the legs, the swing leg is the one not on the ground", "[game][jump]") {
    grounded_world g;
    REQUIRE(g.settle());
    g.run_for(0.3F);

    const auto& state  = g.world.get<game::player_component>(g.player);
    const auto& layers = g.world.get<ecs::animation_player_component>(g.player);

    g.press_jump();
    g.run_for(0.3F);
    REQUIRE(std::abs(state.get_stride()) < 0.05F);
    REQUIRE(g.settle());
    g.run_for(0.5F);

    std::string planted;
    g.world.system<game::input_system>().mapper().key(keys::W, true);
    for (float32 elapsed = 0.0F; elapsed < 0.45F; elapsed += g.tick_seconds) {
        g.tick();
        for (const auto& event : layers.get_fired_events()) {
            if (event.name == "footstep") {
                planted = event.payload;
            }
        }
    }
    REQUIRE_FALSE(planted.empty());

    g.lift(drop_height * 2.0F);
    float32 widest = 0.0F;
    for (float32 elapsed = 0.0F; elapsed < 0.4F; elapsed += g.tick_seconds) {
        g.tick();
        if (std::abs(state.get_stride()) > std::abs(widest)) {
            widest = state.get_stride();
        }
    }
    const float32 expected = planted == "left" ? 1.0F : -1.0F;
    REQUIRE(widest * expected > 0.6F);

    g.world.system<game::input_system>().mapper().key(keys::W, false);
    REQUIRE(g.settle());
    g.run_for(0.6F);
    REQUIRE(std::abs(state.get_stride()) < 0.05F);
}

TEST_CASE("a roll carries the body two heights and turns it over once", "[game][dodge]") {
    grounded_world g;
    REQUIRE(g.settle());
    g.run_for(0.3F);

    const auto& state   = g.world.get<game::player_component>(g.player);
    const auto& tuning  = g.world.system<game::player_system>().tuning();
    const auto pose     = state.get_pose();
    const vec3f start   = g.world.get<ecs::transform_component>(g.player).get_position();
    auto& mapper        = g.world.system<game::input_system>().mapper();

    mapper.key(keys::W, true);
    mapper.key(keys::LEFT_SHIFT, true);
    g.tick();
    mapper.key(keys::LEFT_SHIFT, false);
    mapper.key(keys::W, false);
    REQUIRE(state.is_rolling());
    REQUIRE(state.get_dodge_direction().z < -0.99F);

    float32 most_upside_down = 1.0F;
    float32 highest_centre   = 0.0F;
    float32 lowest_centre    = 1.0e9F;
    for (float32 elapsed = 0.0F; elapsed < tuning.roll_seconds; elapsed += g.tick_seconds) {
        g.tick();
        const auto& placed = g.world.get<ecs::transform_component>(pose);
        const auto turn    = placed.get_rotation();
        most_upside_down   = std::min(most_upside_down, 1.0F - 2.0F * turn.x * turn.x);

        const float32 cosine = 1.0F - 2.0F * turn.x * turn.x;
        const float32 centre = placed.get_position().y + tuning.roll_pivot_height * cosine;
        highest_centre       = std::max(highest_centre, centre);
        lowest_centre        = std::min(lowest_centre, centre);
    }
    REQUIRE(most_upside_down < -0.9F);
    REQUIRE(highest_centre > tuning.roll_pivot_height + 2.0F);
    REQUIRE(lowest_centre < tuning.roll_pivot_height - 4.0F);

    g.run_for(tuning.roll_recovery_seconds + 0.05F);
    REQUIRE_FALSE(state.is_rolling());

    const vec3f end     = g.world.get<ecs::transform_component>(g.player).get_position();
    const float32 moved = std::sqrt((end.x - start.x) * (end.x - start.x) + (end.z - start.z) * (end.z - start.z));
    REQUIRE(moved > tuning.roll_distance * 0.85F);
    REQUIRE(moved < tuning.roll_distance * 1.1F);

    g.run_for(0.3F);
    const auto& rest = g.world.get<ecs::transform_component>(pose);
    REQUIRE(std::abs(rest.get_position().y) < 0.01F);
    REQUIRE(std::abs(rest.get_position().z) < 0.01F);
}

TEST_CASE("the roll centre moves smoothly when frames outpace animation", "[game][dodge]") {
    grounded_world g{1.0F / 240.0F};
    REQUIRE(g.settle());
    g.run_for(0.3F);
    const auto& state  = g.world.get<game::player_component>(g.player);
    const auto& tuning = g.world.system<game::player_system>().tuning();
    auto& mapper       = g.world.system<game::input_system>().mapper();
    mapper.key(keys::W, true);
    mapper.key(keys::LEFT_SHIFT, true);
    g.tick();
    mapper.key(keys::LEFT_SHIFT, false);
    mapper.key(keys::W, false);
    std::vector<float32> heights;
    for (int32 tail = 0; tail < 24; tail += state.is_rolling() ? 0 : 1) {
        g.tick();
        const auto& placed   = g.world.get<ecs::transform_component>(state.get_pose());
        const auto turn      = placed.get_rotation();
        const float32 cosine = 1.0F - 2.0F * turn.x * turn.x;
        heights.push_back(placed.get_position().y + tuning.roll_pivot_height * cosine);
    }
    float32 worst = 0.0F;
    for (std::size_t i = 2; i < heights.size(); ++i) {
        worst = std::max(worst, std::abs(heights[i] - 2.0F * heights[i - 1] + heights[i - 2]));
    }
    REQUIRE(worst < 0.2F);
}

TEST_CASE("a roll is invulnerable for the window its clip marks", "[game][dodge]") {
    grounded_world g;
    REQUIRE(g.settle());
    g.run_for(0.3F);
    const auto& state  = g.world.get<game::player_component>(g.player);
    const auto& tuning = g.world.system<game::player_system>().tuning();
    auto& mapper       = g.world.system<game::input_system>().mapper();
    REQUIRE_FALSE(state.is_invulnerable());

    mapper.key(keys::W, true);
    mapper.key(keys::LEFT_SHIFT, true);
    g.tick();
    mapper.key(keys::LEFT_SHIFT, false);
    mapper.key(keys::W, false);
    REQUIRE(state.is_rolling());

    float32 elapsed        = 0.0F;
    float32 first_iframe   = -1.0F;
    float32 iframe_seconds = 0.0F;
    while (state.is_rolling()) {
        g.tick();
        elapsed += g.tick_seconds;
        if (state.is_invulnerable()) {
            first_iframe = first_iframe < 0.0F ? elapsed : first_iframe;
            iframe_seconds += g.tick_seconds;
        }
        REQUIRE(elapsed < tuning.roll_seconds + tuning.roll_recovery_seconds + 0.1F);
    }
    REQUIRE(first_iframe > 0.0F);
    REQUIRE(first_iframe < 0.15F);
    REQUIRE(iframe_seconds > 0.26F);
    REQUIRE(iframe_seconds < 0.34F);
    REQUIRE_FALSE(state.is_invulnerable());
}

TEST_CASE("a dash lunges one height without turning over and is invulnerable briefly", "[game][dodge]") {
    grounded_world g;
    REQUIRE(g.settle());
    g.run_for(0.3F);
    const auto& state = g.world.get<game::player_component>(g.player);
    auto& tuning      = g.world.system<game::player_system>().tuning();
    auto& mapper      = g.world.system<game::input_system>().mapper();
    tuning.dodge      = game::dodge_kind::dash;
    const vec3f start = g.world.get<ecs::transform_component>(g.player).get_position();

    mapper.key(keys::W, true);
    mapper.key(keys::LEFT_SHIFT, true);
    g.tick();
    mapper.key(keys::LEFT_SHIFT, false);
    mapper.key(keys::W, false);
    REQUIRE(state.is_dashing());
    REQUIRE_FALSE(state.is_rolling());

    float32 elapsed        = 0.0F;
    float32 iframe_seconds = 0.0F;
    float32 most_tilted    = 1.0F;
    while (state.is_dodging()) {
        g.tick();
        elapsed += g.tick_seconds;
        iframe_seconds += state.is_invulnerable() ? g.tick_seconds : 0.0F;
        const auto turn = g.world.get<ecs::transform_component>(state.get_pose()).get_rotation();
        most_tilted     = std::min(most_tilted, 1.0F - 2.0F * turn.x * turn.x);
        REQUIRE(elapsed < tuning.dash_seconds + tuning.dash_recovery_seconds + 0.1F);
    }
    REQUIRE(most_tilted > 0.9F);
    REQUIRE(iframe_seconds > 0.08F);
    REQUIRE(iframe_seconds < 0.16F);

    const vec3f end     = g.world.get<ecs::transform_component>(g.player).get_position();
    const float32 moved = std::sqrt((end.x - start.x) * (end.x - start.x) + (end.z - start.z) * (end.z - start.z));
    REQUIRE(moved > tuning.dash_distance * 0.85F);
    REQUIRE(moved < tuning.dash_distance * 1.15F);
    REQUIRE(state.get_dodge_charges() == tuning.dodge_charges - 1);
}

TEST_CASE("a dodge into a thin wall stops at it, neither through nor over it", "[game][dodge]") {
    const auto kind          = GENERATE(game::dodge_kind::roll, game::dodge_kind::dash);
    const float32 frame_rate = GENERATE(60.0F, 240.0F);
    INFO("dash " << (kind == game::dodge_kind::dash) << ", frames a second " << frame_rate);
    grounded_world g{1.0F / frame_rate};
    REQUIRE(g.settle());
    g.run_for(0.3F);
    const auto& state = g.world.get<game::player_component>(g.player);
    auto& tuning      = g.world.system<game::player_system>().tuning();
    auto& mapper      = g.world.system<game::input_system>().mapper();
    auto& grid        = *g.world.system<ecs::world_grid_system>().grid();
    tuning.dodge      = kind;

    const int32 unit  = grid.world_units_per_voxel();
    const vec3f start = g.world.get<ecs::transform_component>(g.player).get_position();
    const auto voxel_of = [unit](float32 at) {
        return static_cast<int32>(std::floor(at / static_cast<float32>(unit)));
    };
    const int32 wall_z = voxel_of(start.z - 24.0F);
    const int32 feet_y = voxel_of(start.y);
    const int32 top_y  = feet_y + 8;
    for (int32 x = voxel_of(start.x) - 4; x <= voxel_of(start.x) + 4; ++x) {
        for (int32 y = feet_y - 3; y <= top_y; ++y) {
            grid.set_voxel(vec3i{x, y, wall_z} * unit, voxels::gray[7]);
        }
    }

    mapper.key(keys::W, true);
    mapper.key(keys::LEFT_SHIFT, true);
    g.tick();
    mapper.key(keys::LEFT_SHIFT, false);
    mapper.key(keys::W, false);
    REQUIRE(state.is_dodging());
    while (state.is_dodging()) {
        g.tick();
    }
    g.run_for(0.2F);

    const vec3f end = g.world.get<ecs::transform_component>(g.player).get_position();
    const float32 wall_near_face = static_cast<float32>((wall_z + 1) * unit);
    REQUIRE(end.z > wall_near_face);
    REQUIRE(end.z < wall_near_face + 12.0F);
    REQUIRE(end.y < static_cast<float32>(top_y * unit));
}

TEST_CASE("a dodge spends its charge and gets it back after the recharge", "[game][dodge]") {
    grounded_world g;
    REQUIRE(g.settle());
    g.run_for(0.3F);
    const auto& state  = g.world.get<game::player_component>(g.player);
    const auto& tuning = g.world.system<game::player_system>().tuning();
    auto& mapper       = g.world.system<game::input_system>().mapper();
    const auto press_dodge = [&] {
        mapper.key(keys::LEFT_SHIFT, true);
        g.tick();
        mapper.key(keys::LEFT_SHIFT, false);
    };

    REQUIRE(state.get_dodge_charges() == tuning.dodge_charges);
    mapper.key(keys::W, true);
    press_dodge();
    mapper.key(keys::W, false);
    REQUIRE(state.get_dodge_count() == 1);
    REQUIRE(state.get_dodge_charges() == tuning.dodge_charges - 1);
    while (state.is_rolling()) {
        g.tick();
    }

    press_dodge();
    g.run_for(tuning.dodge_recharge_seconds - 0.2F);
    REQUIRE(state.get_dodge_count() == 1);
    REQUIRE_FALSE(state.is_rolling());
    REQUIRE(state.get_dodge_recharge_left() > 0.0F);

    g.run_for(0.3F);
    REQUIRE(state.get_dodge_charges() == tuning.dodge_charges);
    press_dodge();
    REQUIRE(state.get_dodge_count() == 2);
    REQUIRE(state.is_rolling());
}

TEST_CASE("a dodge pressed just before the charge returns is taken when it does", "[game][dodge]") {
    grounded_world g;
    REQUIRE(g.settle());
    g.run_for(0.3F);
    const auto& state  = g.world.get<game::player_component>(g.player);
    const auto& tuning = g.world.system<game::player_system>().tuning();
    auto& mapper       = g.world.system<game::input_system>().mapper();

    mapper.key(keys::W, true);
    mapper.key(keys::LEFT_SHIFT, true);
    g.tick();
    mapper.key(keys::LEFT_SHIFT, false);
    mapper.key(keys::W, false);
    while (state.is_rolling()) {
        g.tick();
    }
    while (state.get_dodge_recharge_left() > tuning.input_buffer_seconds * 0.5F) {
        g.tick();
    }

    mapper.key(keys::LEFT_SHIFT, true);
    g.tick();
    mapper.key(keys::LEFT_SHIFT, false);
    REQUIRE(state.get_dodge_count() == 1);
    g.run_for(tuning.input_buffer_seconds);
    REQUIRE(state.get_dodge_count() == 2);
}

TEST_CASE("holding jump does not hop again", "[game][jump]") {
    grounded_world g;
    REQUIRE(g.settle());

    const uint32 before = g.jumps();
    g.world.system<game::input_system>().mapper().key(keys::SPACE, true);
    g.run_for(2.0F);
    REQUIRE(g.jumps() == before + 1);
}

namespace {

struct guard_stand {
    grounded_world g;
    const game::player_component& state = g.world.get<game::player_component>(g.player);
    game::input_mapper& mapper          = g.world.system<game::input_system>().mapper();

    explicit guard_stand(float32 tick = display_tick_seconds) : g{tick} {
        static_cast<void>(g.settle());
        mapper.key(keys::KEY_1, true);
        g.tick();
        mapper.key(keys::KEY_1, false);
        g.run_for(0.3F);
    }

    [[nodiscard]] auto action_state() const -> const std::string& {
        return g.world.get<ecs::animation_fsm_component>(g.player).get_machine(1).get_current_state();
    }

    [[nodiscard]] auto look() const -> vec3f {
        return g.world.get<game::player_input_component>(g.player).get_frame().look_forward_flat();
    }

    [[nodiscard]] auto faces_the_look() const -> bool {
        const auto rotation = g.world.get<ecs::transform_component>(g.player).get_rotation();
        return std::abs(math::dot(rotation, math::quat_look_y(look()))) > 0.999F;
    }

    [[nodiscard]] auto planar_speed() const -> float32 {
        const auto wish = g.world.get<ecs::movement_intent_component>(g.player).get_wish_velocity();
        return std::sqrt(wish.x * wish.x + wish.z * wish.z);
    }
};

}  // namespace

TEST_CASE("the shield comes with the sword and is raised while the block is held", "[game][guard]") {
    guard_stand s;
    REQUIRE(s.g.grounded());
    REQUIRE(s.state.has_weapon());
    REQUIRE(s.state.has_shield());
    REQUIRE_FALSE(s.state.is_guarding());

    s.mapper.button(mouse::buttons::RIGHT, true);
    s.g.run_for(0.2F);
    REQUIRE(s.state.is_guarding());
    REQUIRE(s.action_state() == "block");

    s.mapper.button(mouse::buttons::LEFT, true);
    s.g.tick();
    s.mapper.button(mouse::buttons::LEFT, false);
    s.g.run_for(0.2F);
    REQUIRE(s.state.get_swing_count() == 0);

    s.mapper.button(mouse::buttons::RIGHT, false);
    s.g.run_for(0.3F);
    REQUIRE_FALSE(s.state.is_guarding());
    REQUIRE(s.action_state() == "none");
}

TEST_CASE("in the guard the body faces the look and walks slower aside than ahead", "[game][guard]") {
    guard_stand s;
    const auto& tuning = s.g.world.system<game::player_system>().tuning();
    const float32 run_speed =
        s.g.world.get<ecs::character_controller_component>(s.g.player).get_move_speed();

    s.mapper.cursor_at(0.0, 0.0);
    s.g.tick();
    const vec3f look_before = s.look();
    s.mapper.cursor_at(900.0, 0.0);
    s.g.tick();
    REQUIRE(math::dot(look_before, s.look()) < 0.5F);
    REQUIRE_FALSE(s.faces_the_look());

    s.mapper.button(mouse::buttons::RIGHT, true);
    s.g.run_for(0.6F);
    REQUIRE(s.faces_the_look());

    s.mapper.key(keys::D, true);
    s.g.run_for(0.6F);
    REQUIRE(s.planar_speed() == Catch::Approx(run_speed * tuning.guard_side_speed_scale).epsilon(0.02));
    s.mapper.key(keys::D, false);

    s.mapper.key(keys::W, true);
    s.g.run_for(0.6F);
    REQUIRE(s.planar_speed() == Catch::Approx(run_speed * tuning.guard_speed_scale).epsilon(0.02));
    REQUIRE(s.faces_the_look());
}

TEST_CASE("the stance steps pick a clip by the side the body moves to", "[game][guard]") {
    guard_stand s;
    const auto& tuning = s.g.world.system<game::player_system>().tuning();
    const auto& layers = s.g.world.get<ecs::animation_player_component>(s.g.player);
    const auto locomotion_state = [&] {
        return s.g.world.get<ecs::animation_fsm_component>(s.g.player).get_machine(0).get_current_state();
    };

    s.mapper.button(mouse::buttons::RIGHT, true);
    s.g.run_for(0.3F);
    REQUIRE(locomotion_state() == "stance_idle");

    s.mapper.key(keys::S, true);
    s.g.run_for(0.6F);
    REQUIRE(locomotion_state() == "walk_b");
    REQUIRE(layers.get_layer(0).playback_speed ==
            Catch::Approx(s.planar_speed() / tuning.stance_step_speed).epsilon(0.02));
    s.mapper.key(keys::S, false);

    s.mapper.key(keys::D, true);
    s.g.run_for(0.6F);
    REQUIRE(locomotion_state() == "walk_r");
    REQUIRE(layers.get_layer(0).playback_speed > s.planar_speed() / tuning.stance_step_speed);

    s.mapper.key(keys::A, true);
    s.mapper.key(keys::D, false);
    s.g.run_for(0.6F);
    REQUIRE(locomotion_state() == "walk_l");

    s.mapper.key(keys::A, false);
    s.mapper.button(mouse::buttons::RIGHT, false);
    s.g.run_for(0.3F);
    REQUIRE_FALSE(locomotion_state().starts_with("walk_"));
}

TEST_CASE("a hit on the shield plays the recoil only while guarding", "[game][guard]") {
    guard_stand s;
    auto& players = s.g.world.system<game::player_system>();

    REQUIRE_FALSE(players.take_hit_on_shield(s.g.player));
    REQUIRE(s.state.get_blocked_hits() == 0);

    s.mapper.button(mouse::buttons::RIGHT, true);
    s.g.run_for(0.2F);
    REQUIRE(players.take_hit_on_shield(s.g.player));
    s.g.tick();
    REQUIRE(s.action_state() == "block_impact");
    REQUIRE(s.state.get_blocked_hits() == 1);

    s.g.run_for(0.6F);
    REQUIRE(s.action_state() == "block");
}

TEST_CASE("standing in the stance the feet hold the ground and step after a wide turn", "[game][guard]") {
    guard_stand s;
    const auto& tuning = s.g.world.system<game::player_system>().tuning();

    s.mapper.cursor_at(0.0, 0.0);
    s.mapper.button(mouse::buttons::RIGHT, true);
    s.g.run_for(1.0F);
    s.mapper.button(mouse::buttons::RIGHT, false);
    s.g.run_for(1.0F);
    s.mapper.button(mouse::buttons::RIGHT, true);
    s.g.run_for(0.3F);
    const uint32 steps_before = s.state.get_turn_steps();

    s.mapper.cursor_at(300.0, 0.0);
    s.g.run_for(0.3F);
    REQUIRE(s.state.get_turn_steps() == steps_before);
    REQUIRE(std::abs(s.state.get_foot_twist_degrees(0)) == Catch::Approx(30.0F).margin(2.0F));
    REQUIRE(std::abs(s.state.get_foot_twist_degrees(1)) == Catch::Approx(30.0F).margin(2.0F));

    s.mapper.cursor_at(900.0, 0.0);
    s.g.run_for(1.0F);
    REQUIRE(s.state.get_turn_steps() >= steps_before + 2);
    for (const std::size_t foot : {std::size_t{0}, std::size_t{1}}) {
        REQUIRE_FALSE(s.state.is_foot_stepping(foot));
        REQUIRE(std::abs(s.state.get_foot_twist_degrees(foot)) < tuning.stance_turn_step_degrees);
    }

    s.mapper.button(mouse::buttons::RIGHT, false);
    s.g.run_for(1.0F);
    REQUIRE(std::abs(s.state.get_foot_twist_degrees(0)) < 1.0F);
    REQUIRE(std::abs(s.state.get_foot_twist_degrees(1)) < 1.0F);
}

TEST_CASE("the feet stay under the body however long the stance turns in place", "[game][guard]") {
    const float32 tick = GENERATE(display_tick_seconds, 0.004F);
    guard_stand s{tick};
    const auto foot_reach = [&] {
        float32 farthest = 0.0F;
        for (const auto* part : {"foot_left", "foot_right"}) {
            s.g.world.for_each<ecs::animation_target_component>(
                [&](ecs::entity ent, const ecs::animation_target_component& target) {
                    if (target.get_name() == part) {
                        const vec3f at = s.g.world.get<ecs::transform_component>(ent).get_position();
                        REQUIRE(std::isfinite(at.x));
                        REQUIRE(std::isfinite(at.z));
                        farthest = std::max(farthest, std::sqrt(at.x * at.x + at.z * at.z));
                    }
                }
            );
        }
        return farthest;
    };

    s.mapper.button(mouse::buttons::RIGHT, true);
    float64 cursor = 0.0;
    float32 farthest = 0.0F;
    const int32 ticks = static_cast<int32>(6.4F / tick);
    for (int32 tick_index = 0; tick_index < ticks; ++tick_index) {
        cursor += (tick_index * tick) - std::floor(tick_index * tick / 1.92F) * 1.92F < 0.96F ? 1560.0 * tick : -750.0 * tick;
        s.mapper.cursor_at(cursor, 0.0);
        s.g.tick();
        farthest = std::max(farthest, foot_reach());
    }
    REQUIRE(farthest < 12.0F);
}
