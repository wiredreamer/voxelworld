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
constexpr int32 game_units_per_voxel = 16;
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
    int32 units;

    explicit grounded_world(float32 tick = display_tick_seconds, int32 voxel_units = units_per_voxel)
        : tick_seconds{tick}, units{voxel_units} {
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
        terrain.island         = false;

        ecs::plains_biome level{};
        level.height   = 0.0F;
        terrain.biomes = {level};

        grid.set_grid(std::make_unique<ecs::world_grid>(world, units));
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
            grid.set_voxel(vec3i{x, y, wall_z} * unit, voxels::gray[14]);
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

    const auto& hands = s.g.world.get<ecs::animation_player_component>(s.g.player).get_layer(1);
    float32 faintest  = 1.0F;
    for (int32 frame = 0; frame < 60; ++frame) {
        s.g.tick();
        faintest = std::min(faintest, hands.fade_influence);
    }
    REQUIRE(s.action_state() == "block");
    REQUIRE(faintest > 0.9F);
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

TEST_CASE("a strike leaves the guard at once, goes where the body looks and the guard comes back", "[game][guard]") {
    guard_stand s;
    const auto& layers = s.g.world.get<ecs::animation_player_component>(s.g.player);

    s.mapper.cursor_at(0.0, 0.0);
    s.mapper.button(mouse::buttons::RIGHT, true);
    s.g.run_for(0.6F);
    REQUIRE(s.state.is_guarding());

    s.mapper.key(keys::D, true);
    s.g.run_for(0.3F);
    s.mapper.button(mouse::buttons::LEFT, true);
    s.g.tick();
    s.mapper.button(mouse::buttons::LEFT, false);

    REQUIRE(s.state.is_swinging());
    REQUIRE_FALSE(s.state.is_guarding());
    REQUIRE(s.state.is_in_stance());
    REQUIRE(s.action_state() == "attack_1");
    REQUIRE(math::dot(s.state.get_attack_direction(), s.look()) > 0.99F);
    REQUIRE(layers.get_layer(1).fade_influence > 0.99F);
    s.mapper.key(keys::D, false);

    for (int32 tick_index = 0; tick_index < 200 && s.state.is_swinging(); ++tick_index) {
        s.g.tick();
        REQUIRE(s.state.is_in_stance());
    }
    s.g.tick();
    REQUIRE(s.state.is_guarding());
    s.g.run_for(0.2F);
    REQUIRE(s.action_state() == "block");
}

TEST_CASE("a run steps onto a one voxel ledge without leaving the ground, a taller wall stops it", "[game][jump]") {
    const int32 wall_voxels = GENERATE(1, 2);
    INFO("wall " << wall_voxels << " voxels");

    grounded_world g;
    g.world.system<game::player_system>().tuning().ride_footing = false;
    REQUIRE(g.settle());

    auto& mapper = g.world.system<game::input_system>().mapper();
    mapper.cursor_at(0.0, 0.0);
    g.tick();

    const vec3f look = g.world.get<game::player_input_component>(g.player).get_frame().look_forward_flat();
    const bool along_x = std::abs(look.x) > std::abs(look.z);
    const int32 ahead  = (along_x ? look.x : look.z) > 0.0F ? 1 : -1;

    const vec3f start = g.world.get<ecs::transform_component>(g.player).get_position();
    const auto vs     = static_cast<float32>(units_per_voxel);
    const int32 floor_y = static_cast<int32>(std::lround((start.y - 0.5F) / vs)) - 1;
    const vec3i origin{
        static_cast<int32>(std::floor(start.x / vs)), floor_y,
        static_cast<int32>(std::floor(start.z / vs))
    };

    auto& grid = *g.world.system<ecs::world_grid_system>().grid();
    const auto put = [&](int32 along, int32 aside, int32 up, voxel v) {
        const vec3i offset = along_x ? vec3i{along * ahead, up, aside} : vec3i{aside, up, along * ahead};
        grid.set_voxel((origin + offset) * units_per_voxel, v);
    };

    constexpr int32 wall_at = 8;
    for (int32 along = -3; along <= 20; ++along) {
        for (int32 aside = -3; aside <= 3; ++aside) {
            put(along, aside, 0, voxels::red[6]);
            for (int32 up = 1; up <= 8; ++up) {
                const bool wall = along >= wall_at && up <= wall_voxels;
                put(along, aside, up, wall ? voxels::red[6] : voxels::air);
            }
        }
    }
    g.run_for(0.3F);
    REQUIRE(g.grounded());

    const auto& body          = g.world.get<ecs::rigid_body_component>(g.player);
    const uint32 steps_before = body.get_steps_taken();
    const auto risen = [&] {
        return g.world.get<ecs::transform_component>(g.player).get_position().y - start.y;
    };
    const auto shown_height = [&] {
        return g.world.get<ecs::transform_component>(g.player).get_shown_position().y - start.y +
               g.world.get<ecs::rigid_body_component>(g.player).get_step_sink();
    };

    float32 longest_in_the_air = 0.0F;
    float32 in_the_air         = 0.0F;
    float32 biggest_jolt       = 0.0F;
    float32 deepest_sink       = 0.0F;
    float32 shown_before       = shown_height();

    mapper.key(keys::W, true);
    for (float32 elapsed = 0.0F; elapsed < 1.0F; elapsed += g.tick_seconds) {
        g.tick();

        in_the_air         = g.grounded() ? 0.0F : in_the_air + g.tick_seconds;
        longest_in_the_air = std::max(longest_in_the_air, in_the_air);

        const float32 shown = shown_height();
        biggest_jolt        = std::max(biggest_jolt, std::abs(shown - shown_before));
        deepest_sink        = std::min(deepest_sink, body.get_step_sink());
        shown_before        = shown;
    }
    mapper.key(keys::W, false);
    g.run_for(0.4F);

    const vec3f end   = g.world.get<ecs::transform_component>(g.player).get_position();
    const uint32 took = body.get_steps_taken() - steps_before;
    const float32 wall_face = (static_cast<float32>(along_x ? origin.x : origin.z) + 0.5F +
                               static_cast<float32>(ahead) * (static_cast<float32>(wall_at) - 0.5F)) * vs;
    const float32 travelled = static_cast<float32>(ahead) * ((along_x ? end.x : end.z) - wall_face);

    REQUIRE(g.grounded());
    REQUIRE(longest_in_the_air == 0.0F);
    if (wall_voxels == 1) {
        REQUIRE(took == 1);
        REQUIRE(end.y == Catch::Approx(start.y + vs).margin(0.5F));
        REQUIRE(travelled > 2.0F * vs);
        REQUIRE(biggest_jolt < 0.45F * vs);
        REQUIRE(deepest_sink > -0.25F * vs);
    } else {
        REQUIRE(took == 0);
        REQUIRE(end.y == Catch::Approx(start.y).margin(0.5F));
        REQUIRE(travelled < 0.0F);
    }
}

namespace {

struct stair_run {
    float32 pace_on_the_flat   = 0.0F;
    float32 slowest_on_stairs  = std::numeric_limits<float32>::max();
    float32 longest_in_the_air = 0.0F;
    float32 biggest_jolt       = 0.0F;
    float32 deepest_sink       = 0.0F;
    float32 risen              = 0.0F;
    float32 sink_at_the_end    = 0.0F;
    uint32 steps_taken         = 0;
    bool grounded_at_the_end   = false;
};

auto run_up_stairs(bool across_the_corner, bool riding = false) -> stair_run {
    constexpr int32 stairs_at = 8;
    constexpr int32 steps     = 6;

    grounded_world g{display_tick_seconds, riding ? game_units_per_voxel : units_per_voxel};
    const int32 units_per_voxel = g.units;
    g.world.system<game::player_system>().tuning().ride_footing = riding;
    REQUIRE(g.settle());

    auto& mapper = g.world.system<game::input_system>().mapper();
    mapper.cursor_at(0.0, 0.0);
    g.tick();

    const vec3f look = g.world.get<game::player_input_component>(g.player).get_frame().look_forward_flat();
    const bool along_x = std::abs(look.x) > std::abs(look.z);
    const int32 ahead  = (along_x ? look.x : look.z) > 0.0F ? 1 : -1;

    const vec3f start = g.world.get<ecs::transform_component>(g.player).get_position();
    const auto vs     = static_cast<float32>(units_per_voxel);
    const int32 floor_y = static_cast<int32>(std::lround((start.y - 0.5F) / vs)) - 1;
    const vec3i origin{
        static_cast<int32>(std::floor(start.x / vs)), floor_y,
        static_cast<int32>(std::floor(start.z / vs))
    };

    auto& grid = *g.world.system<ecs::world_grid_system>().grid();
    for (int32 along = -3; along <= stairs_at + steps + 14; ++along) {
        for (int32 aside = -(stairs_at + steps + 14); aside <= stairs_at + steps + 14; ++aside) {
            const int32 by_run  = along - stairs_at + 1;
            const int32 by_side = std::abs(aside) - stairs_at + 1;
            const int32 height  =
                std::clamp(across_the_corner ? std::max(by_run, by_side) : by_run, 0, steps);

            for (int32 up = 0; up <= steps + 8; ++up) {
                const vec3i offset = along_x ? vec3i{along * ahead, up, aside}
                                             : vec3i{aside, up, along * ahead};
                grid.set_voxel(
                    (origin + offset) * units_per_voxel, up <= height ? voxels::red[6] : voxels::air
                );
            }
        }
    }
    g.run_for(0.3F);
    REQUIRE(g.grounded());

    const auto& body = g.world.get<ecs::rigid_body_component>(g.player);
    constexpr int32 pace_window_ticks = 12;

    vec3f stood_at = g.world.get<ecs::transform_component>(g.player).get_position();
    const auto speed = [&] {
        const vec3f now = g.world.get<ecs::transform_component>(g.player).get_position();
        const vec3f gone{now.x - stood_at.x, 0.0F, now.z - stood_at.z};
        stood_at = now;
        return math::length(gone) / (g.tick_seconds * static_cast<float32>(pace_window_ticks));
    };
    const auto risen = [&] {
        return g.world.get<ecs::transform_component>(g.player).get_position().y - start.y;
    };
    const auto shown_height = [&] {
        return g.world.get<ecs::transform_component>(g.player).get_shown_position().y - start.y +
               g.world.get<ecs::rigid_body_component>(g.player).get_step_sink();
    };

    stair_run seen;
    const uint32 steps_before = body.get_steps_taken();

    int32 ticks_in_the_window = 0;

    float32 in_the_air   = 0.0F;
    float32 shown_before = shown_height();

    mapper.key(keys::W, true);
    if (across_the_corner) {
        mapper.key(keys::D, true);
    }
    const float32 run_seconds = 1.8F * vs / 8.0F;
    for (float32 elapsed = 0.0F; elapsed < run_seconds; elapsed += g.tick_seconds) {
        g.tick();

        in_the_air              = g.grounded() ? 0.0F : in_the_air + g.tick_seconds;
        seen.longest_in_the_air = std::max(seen.longest_in_the_air, in_the_air);

        const float32 shown = shown_height();
        seen.biggest_jolt   = std::max(seen.biggest_jolt, std::abs(shown - shown_before));
        seen.deepest_sink   = std::min(seen.deepest_sink, body.get_step_sink());
        shown_before        = shown;

        if (++ticks_in_the_window < pace_window_ticks) {
            continue;
        }
        ticks_in_the_window = 0;

        const float32 pace = speed();
        if (risen() < 0.25F * vs) {
            seen.pace_on_the_flat = pace;
        } else if (risen() < (static_cast<float32>(steps) - 0.5F) * vs) {
            seen.slowest_on_stairs = std::min(seen.slowest_on_stairs, pace);
        }
    }
    mapper.key(keys::W, false);
    mapper.key(keys::D, false);
    g.run_for(0.5F);

    seen.risen               = risen() / vs;
    seen.sink_at_the_end     = body.get_step_sink();
    seen.steps_taken         = body.get_steps_taken() - steps_before;
    seen.grounded_at_the_end = g.grounded();
    return seen;
}

}  // namespace

TEST_CASE("a run walks up a staircase of one voxel steps without leaving the ground", "[game][jump]") {
    const stair_run seen = run_up_stairs(false);

    REQUIRE(seen.grounded_at_the_end);
    REQUIRE(seen.risen == Catch::Approx(6.0F).margin(0.1F));
    REQUIRE(seen.steps_taken == 6);
    REQUIRE(seen.pace_on_the_flat > 0.0F);
    REQUIRE(seen.slowest_on_stairs >= seen.pace_on_the_flat * 0.8F);
    REQUIRE(seen.longest_in_the_air == 0.0F);
    REQUIRE(seen.biggest_jolt < 0.45F * static_cast<float32>(units_per_voxel));
    REQUIRE(seen.deepest_sink > -0.25F * static_cast<float32>(units_per_voxel));
    REQUIRE(std::abs(seen.sink_at_the_end) < 0.1F);
}

TEST_CASE("a diagonal run climbs the inside corner of two staircases", "[game][jump]") {
    const stair_run seen = run_up_stairs(true);

    REQUIRE(seen.grounded_at_the_end);
    REQUIRE(seen.risen >= 5.9F);
    REQUIRE(seen.pace_on_the_flat > 0.0F);
    REQUIRE(seen.slowest_on_stairs >= seen.pace_on_the_flat * 0.8F);
    REQUIRE(seen.longest_in_the_air == 0.0F);
    REQUIRE(seen.biggest_jolt < 0.45F * static_cast<float32>(units_per_voxel));
    REQUIRE(seen.deepest_sink > -0.25F * static_cast<float32>(units_per_voxel));
}

TEST_CASE("between physics steps a running body is shown moving, though it stands still", "[game][physics]") {
    constexpr float32 fast_tick = 1.0F / 240.0F;

    grounded_world g{fast_tick};
    REQUIRE(g.settle());

    auto& mapper = g.world.system<game::input_system>().mapper();
    mapper.cursor_at(0.0, 0.0);
    mapper.key(keys::W, true);
    g.run_for(0.5F);

    const auto& placed = g.world.get<ecs::transform_component>(g.player);

    int32 frames_the_body_stood  = 0;
    int32 frames_the_shown_stood = 0;
    float32 longest_shown_stride = 0.0F;
    float32 farthest_behind      = 0.0F;

    vec3f body_before  = placed.get_position();
    vec3f shown_before = placed.get_shown_position();
    for (int32 frame = 0; frame < 240; ++frame) {
        g.tick();

        const vec3f body  = placed.get_position();
        const vec3f shown = placed.get_shown_position();

        frames_the_body_stood  += body == body_before ? 1 : 0;
        frames_the_shown_stood += shown == shown_before ? 1 : 0;
        longest_shown_stride    = std::max(longest_shown_stride, math::length(shown - shown_before));
        farthest_behind         = std::max(farthest_behind, math::length(body - shown));

        body_before  = body;
        shown_before = shown;
    }
    mapper.key(keys::W, false);

    const float32 run_speed = g.world.get<ecs::character_controller_component>(g.player).get_move_speed();
    const float32 physics_stride = run_speed / 60.0F;

    REQUIRE(frames_the_body_stood > 150);
    REQUIRE(frames_the_shown_stood < 10);
    REQUIRE(longest_shown_stride < physics_stride * 0.6F);
    REQUIRE(farthest_behind <= physics_stride * 1.01F);
}

namespace {

struct zigzag_report {
    int32 twitches       = 0;
    float32 fastest_rise = 0.0F;
    float32 fastest_sink = 0.0F;
    int32 false_rises    = 0;
};

// см. docs/walker-plan.md#этапы
auto zigzag_over_bumps(uint32 seed, bool riding) -> zigzag_report {
    constexpr int32 reach         = 60;
    constexpr int32 window_ticks  = 4;
    constexpr float32 run_seconds = 40.0F;

    grounded_world g{display_tick_seconds, riding ? game_units_per_voxel : units_per_voxel};
    const int32 units_per_voxel = g.units;
    g.world.system<game::player_system>().tuning().ride_footing = riding;
    REQUIRE(g.settle());

    auto& mapper = g.world.system<game::input_system>().mapper();
    mapper.cursor_at(0.0, 0.0);
    g.tick();

    const vec3f start   = g.world.get<ecs::transform_component>(g.player).get_position();
    const auto vs       = static_cast<float32>(units_per_voxel);
    const int32 floor_y = static_cast<int32>(std::lround((start.y - 0.5F) / vs)) - 1;
    const vec3i origin{
        static_cast<int32>(std::floor(start.x / vs)), floor_y,
        static_cast<int32>(std::floor(start.z / vs))
    };

    auto& grid = *g.world.system<ecs::world_grid_system>().grid();
    for (int32 x = -reach; x <= reach; ++x) {
        for (int32 z = -reach; z <= reach; ++z) {
            const float32 wave = std::sin(static_cast<float32>(x) * 0.55F) +
                                 std::cos(static_cast<float32>(z) * 0.45F) +
                                 std::sin(static_cast<float32>(x + z) * 0.3F);
            const bool under_the_feet = std::abs(x) <= 1 && std::abs(z) <= 1;
            const int32 height =
                under_the_feet ? 0 : std::clamp(static_cast<int32>(std::lround(wave)), -1, 2) + 1;
            for (int32 up = -2; up <= 12; ++up) {
                grid.set_voxel(
                    (origin + vec3i{x, up, z}) * units_per_voxel,
                    up <= height ? voxels::red[6] : voxels::air
                );
            }
        }
    }
    g.run_for(0.5F);

    std::mt19937 dice{seed};
    std::uniform_real_distribution<float32> hold{0.08F, 0.5F};
    std::uniform_int_distribution<int32> pick{0, 3};

    zigzag_report report;
    float32 height_before = g.world.get<ecs::transform_component>(g.player).get_position().y;
    bool grounded_before  = false;

    const auto& collider = g.world.get<ecs::box_collider_component>(g.player);
    const vec3f half     = collider.get_extents() * 0.5F;
    const auto solid     = [&](int32 vx, int32 vy, int32 vz) {
        return !grid.get_voxel(vec3i{vx, vy, vz} * units_per_voxel).is_empty();
    };
    const auto stood_on = [&](const vec3f& at) {
        const float32 feet = at.y + collider.get_offset().y - half.y;
        return ecs::footing_under(
            solid, vs, {.x = at.x, .z = at.z, .half_x = half.x, .half_z = half.z}, feet, vs
        ).stand;
    };
    const float32 lifted  = 0.15F * vs;
    const float32 settled = 0.02F * vs;
    float32 ground_before = 0.0F;
    bool rising           = false;

    std::vector<float32> shown;
    std::vector<bool> on_the_ground;
    float32 until = 0.0F;
    float64 yaw   = 0.0;
    for (float32 elapsed = 0.0F; elapsed < run_seconds; elapsed += g.tick_seconds) {
        if (elapsed >= until) {
            until = elapsed + hold(dice);

            const int32 way = pick(dice);
            mapper.key(keys::W, true);
            mapper.key(keys::A, way == 1);
            mapper.key(keys::D, way == 2);
            if (way == 3) {
                yaw += 140.0;
                mapper.cursor_at(yaw, 0.0);
            }
        }
        g.tick();

        const auto& body = g.world.get<ecs::rigid_body_component>(g.player);
        shown.push_back(
            g.world.get<ecs::transform_component>(g.player).get_shown_position().y + body.get_step_sink()
        );
        on_the_ground.push_back(body.is_grounded());

        const float32 height = g.world.get<ecs::transform_component>(g.player).get_position().y;
        if (grounded_before && body.is_grounded()) {
            report.fastest_rise = std::max(report.fastest_rise, height - height_before);
            report.fastest_sink = std::max(report.fastest_sink, height_before - height);
        }
        height_before   = height;

        const vec3f placed  = g.world.get<ecs::transform_component>(g.player).get_position();
        const float32 under = stood_on(placed);
        const float32 lift  = (placed.y + collider.get_offset().y - half.y) - under;
        if (!body.is_grounded()) {
            rising = false;
        } else if (!rising && lift < settled) {
            ground_before = under;
        } else if (!rising && lift > lifted) {
            rising = true;
        } else if (rising && lift < settled) {
            rising = false;
            report.false_rises += under == ground_before ? 1 : 0;
            ground_before = under;
        }
        grounded_before = body.is_grounded();
    }

    const float32 enough = 0.15F * vs;
    int32& twitches      = report.twitches;
    for (std::size_t at = window_ticks; at + window_ticks < shown.size(); ++at) {
        bool stood = true;
        float32 lowest_before = shown[at];
        float32 lowest_after  = shown[at];
        float32 highest_before = shown[at];
        float32 highest_after  = shown[at];
        for (std::size_t off = 1; off <= window_ticks; ++off) {
            stood          = stood && on_the_ground[at - off] && on_the_ground[at + off];
            lowest_before  = std::min(lowest_before, shown[at - off]);
            lowest_after   = std::min(lowest_after, shown[at + off]);
            highest_before = std::max(highest_before, shown[at - off]);
            highest_after  = std::max(highest_after, shown[at + off]);
        }
        const bool peak = shown[at] - lowest_before > enough && shown[at] - lowest_after > enough;
        const bool dip  = highest_before - shown[at] > enough && highest_after - shown[at] > enough;
        if (stood && on_the_ground[at] && (peak || dip)) {
            ++twitches;
            at += window_ticks;
        }
    }
    return report;
}

}  // namespace

TEST_CASE("a zigzag over bumps never twitches the shown height", "[game][walker][!shouldfail]") {
    int32 twitches = 0;
    for (const uint32 seed : {1U, 2U, 3U}) {
        twitches += zigzag_over_bumps(seed, false).twitches;
    }
    INFO("twitches " << twitches);
    REQUIRE(twitches == 0);
}

TEST_CASE("riding its footing the body never moves up or down faster than it may", "[game][walker]") {
    constexpr float32 steps_in_a_tick = 2.0F;
    for (const uint32 seed : {1U, 2U, 3U}) {
        INFO("seed " << seed);
        const zigzag_report seen = zigzag_over_bumps(seed, true);
        const game::movement_tuning tuning{};
        const float32 a_step = ecs::physics_system::fixed_dt * steps_in_a_tick;

        REQUIRE(seen.fastest_rise <= (tuning.ride_rise_speed * a_step) + 0.05F);
        REQUIRE(seen.fastest_sink <= (tuning.ride_sink_speed * a_step) + 0.05F);
        WARN("seed " << seed << ": " << seen.false_rises << " rises that led nowhere");
    }
}

TEST_CASE("riding its footing the body walks up stairs as up a ramp", "[game][walker]") {
    const bool across_the_corner = GENERATE(false, true);
    const stair_run seen         = run_up_stairs(across_the_corner, true);
    const auto voxel             = static_cast<float32>(game_units_per_voxel);

    REQUIRE(seen.grounded_at_the_end);
    REQUIRE(seen.risen >= 5.9F);
    REQUIRE(seen.steps_taken == 0);
    REQUIRE(seen.pace_on_the_flat > 0.0F);
    REQUIRE(seen.slowest_on_stairs >= seen.pace_on_the_flat * 0.8F);
    REQUIRE(seen.longest_in_the_air == 0.0F);
    REQUIRE(seen.biggest_jolt < 0.6F * voxel);
    REQUIRE(seen.deepest_sink == 0.0F);
}

namespace {

struct riding_track {
    grounded_world g{display_tick_seconds, game_units_per_voxel};
    float32 voxel = static_cast<float32>(game_units_per_voxel);
    vec3f start{};
    vec3i origin{};
    bool along_x = false;
    int32 ahead  = 1;

    explicit riding_track(const std::function<int32(int32)>& height_at) {
        g.world.system<game::player_system>().tuning().ride_footing = true;
        REQUIRE(g.settle());

        mapper().cursor_at(0.0, 0.0);
        g.tick();

        const vec3f look =
            g.world.get<game::player_input_component>(g.player).get_frame().look_forward_flat();
        along_x = std::abs(look.x) > std::abs(look.z);
        ahead   = (along_x ? look.x : look.z) > 0.0F ? 1 : -1;

        start  = at();
        origin = {
            static_cast<int32>(std::floor(start.x / voxel)),
            static_cast<int32>(std::lround((start.y - 0.5F) / voxel)) - 1,
            static_cast<int32>(std::floor(start.z / voxel))
        };

        auto& grid = *g.world.system<ecs::world_grid_system>().grid();
        for (int32 along = -3; along <= 40; ++along) {
            for (int32 aside = -3; aside <= 3; ++aside) {
                for (int32 up = -10; up <= 10; ++up) {
                    const vec3i offset = along_x ? vec3i{along * ahead, up, aside}
                                                 : vec3i{aside, up, along * ahead};
                    grid.set_voxel(
                        (origin + offset) * game_units_per_voxel,
                        up <= height_at(along) ? voxels::red[6] : voxels::air
                    );
                }
            }
        }
        g.run_for(0.3F);
        REQUIRE(g.grounded());
    }

    [[nodiscard]] auto mapper() -> game::input_mapper& {
        return g.world.system<game::input_system>().mapper();
    }

    [[nodiscard]] auto at() const -> vec3f {
        return g.world.get<ecs::transform_component>(g.player).get_position();
    }

    [[nodiscard]] auto risen() const -> float32 {
        return (at().y - start.y) / voxel;
    }

    [[nodiscard]] auto gone() const -> float32 {
        const vec3f now = at();
        return static_cast<float32>(ahead) * (along_x ? now.x - start.x : now.z - start.z) / voxel;
    }
};

}  // namespace

TEST_CASE("riding its footing the body goes down a step without leaving the ground", "[game][walker]") {
    riding_track track{[](int32 along) { return along >= 4 ? -1 : 0; }};

    float32 lowest_step = 0.0F;
    float32 before      = track.at().y;
    bool over_the_edge  = false;
    track.mapper().key(keys::W, true);
    for (int32 tick = 0; tick < 90; ++tick) {
        track.g.tick();
        REQUIRE(track.g.grounded());
        lowest_step = std::min(lowest_step, track.at().y - before);
        before      = track.at().y;

        over_the_edge = over_the_edge || track.gone() > 4.0F - (5.9F / track.voxel);
        if (!over_the_edge) {
            REQUIRE(track.risen() == Catch::Approx(0.0F).margin(0.01F));
        }
    }

    REQUIRE(track.risen() == Catch::Approx(-1.0F).margin(0.02F));
    REQUIRE(lowest_step > -0.45F * track.voxel);
}

TEST_CASE("riding its footing the body walks off a cliff, falls and lands", "[game][walker]") {
    riding_track track{[](int32 along) { return along >= 4 ? -6 : 0; }};

    bool fell = false;
    track.mapper().key(keys::W, true);
    for (int32 tick = 0; tick < 200; ++tick) {
        track.g.tick();
        fell = fell || !track.g.grounded();
        if (!fell) {
            REQUIRE(track.risen() == Catch::Approx(0.0F).margin(0.01F));
        }
    }

    REQUIRE(fell);
    REQUIRE(track.g.grounded());
    REQUIRE(track.risen() == Catch::Approx(-6.0F).margin(0.02F));
}

TEST_CASE("riding its footing the body is stopped by a wall of two voxels", "[game][walker]") {
    constexpr int32 wall_at = 4;
    riding_track track{[](int32 along) { return along >= wall_at ? 2 : 0; }};

    track.mapper().key(keys::W, true);
    track.g.run_for(2.0F);

    REQUIRE(track.g.grounded());
    REQUIRE(track.risen() == Catch::Approx(0.0F).margin(0.02F));
    REQUIRE(track.gone() < static_cast<float32>(wall_at));
}

TEST_CASE("riding its footing the body jumps onto a ledge of two voxels", "[game][walker]") {
    constexpr int32 wall_at = 3;
    riding_track track{[](int32 along) { return along >= wall_at ? 2 : 0; }};

    track.mapper().key(keys::W, true);
    track.g.run_for(0.15F);
    track.g.press_jump();
    track.g.run_for(1.5F);

    REQUIRE(track.g.grounded());
    REQUIRE(track.risen() == Catch::Approx(2.0F).margin(0.02F));
    REQUIRE(track.gone() > static_cast<float32>(wall_at));
}

TEST_CASE("riding its footing the body rises in front of a step, never inside it", "[game][walker]") {
    constexpr int32 step_at = 5;
    riding_track track{[](int32 along) { return along >= step_at ? 1 : 0; }};
    const float32 face = static_cast<float32>(step_at) - (6.0F / track.voxel) - 0.5F;

    float32 risen_at_the_face = -1.0F;
    bool grounded_all_along   = true;
    track.mapper().key(keys::W, true);
    for (int32 tick = 0; tick < 300 && track.gone() < static_cast<float32>(step_at) + 1.0F; ++tick) {
        const float32 gone_before = track.gone();
        track.g.tick();
        grounded_all_along = grounded_all_along && track.g.grounded();
        if (gone_before < face && track.gone() >= face) {
            risen_at_the_face = track.risen();
        }
        if (track.gone() > face + 0.05F) {
            REQUIRE(track.risen() >= 1.0F - 0.02F);
        }
    }

    REQUIRE(grounded_all_along);
    REQUIRE(risen_at_the_face > 0.8F);
    REQUIRE(track.risen() == Catch::Approx(1.0F).margin(0.02F));
}

TEST_CASE("riding its footing the body that stops before a step comes back down", "[game][walker]") {
    constexpr int32 step_at = 5;
    riding_track track{[](int32 along) { return along >= step_at ? 1 : 0; }};

    track.mapper().key(keys::W, true);
    for (int32 tick = 0; tick < 400 && track.risen() < 0.3F; ++tick) {
        track.g.tick();
    }
    track.mapper().key(keys::W, false);
    REQUIRE(track.risen() >= 0.3F);

    track.g.run_for(1.0F);

    REQUIRE(track.g.grounded());
    const float32 rested = track.risen();
    REQUIRE(std::abs(rested - std::round(rested)) < 0.02F);
}

TEST_CASE("over teeth the model dips less than the body and never below it", "[game][walker]") {
    riding_track track{[](int32 along) { return along >= 4 && (along / 2) % 2 == 0 ? 1 : 0; }};
    const auto& fighter = track.g.world.get<ecs::rigid_body_component>(track.g.player);

    float32 body_low   = std::numeric_limits<float32>::max();
    float32 body_high  = std::numeric_limits<float32>::lowest();
    float32 model_low  = std::numeric_limits<float32>::max();
    float32 model_high = std::numeric_limits<float32>::lowest();

    track.mapper().key(keys::W, true);
    for (int32 tick = 0; tick < 600 && track.gone() < 20.0F; ++tick) {
        track.g.tick();
        REQUIRE(fighter.get_model_lift() >= 0.0F);
        if (track.gone() < 7.0F) {
            continue;
        }
        const float32 body = track.at().y;
        body_low   = std::min(body_low, body);
        body_high  = std::max(body_high, body);
        model_low  = std::min(model_low, body + fighter.get_model_lift());
        model_high = std::max(model_high, body + fighter.get_model_lift());
    }

    REQUIRE(body_high - body_low > 0.5F * track.voxel);
    REQUIRE(model_high - model_low < 0.6F * (body_high - body_low));
}

TEST_CASE("falling from a jump the model comes down smoothly at any frame rate", "[game][walker]") {
    const float32 tick = GENERATE(display_tick_seconds, 1.0F / 240.0F, 1.0F / 97.0F);
    grounded_world g{tick, game_units_per_voxel};
    g.world.system<game::player_system>().tuning().ride_footing = true;
    REQUIRE(g.settle());

    const auto& fighter = g.world.get<ecs::rigid_body_component>(g.player);
    const auto drawn    = [&] {
        return g.world.get<ecs::transform_component>(g.player).get_shown_position().y +
               fighter.get_model_lift();
    };

    g.press_jump();
    float32 before   = drawn();
    float32 highest  = before;
    float32 backward = 0.0F;
    bool falling     = false;
    for (float32 elapsed = 0.0F; elapsed < 1.5F; elapsed += tick) {
        g.tick();
        const float32 now = drawn();
        falling           = falling || now < highest - 1.0F;
        if (falling) {
            backward = std::max(backward, now - before);
        }
        highest = std::max(highest, now);
        before  = now;
    }

    REQUIRE(falling);
    REQUIRE(g.grounded());
    REQUIRE(backward < 0.05F);
}

TEST_CASE("a roll goes over a step instead of stopping at it", "[game][walker]") {
    constexpr int32 step_at = 3;
    riding_track track{[](int32 along) { return along >= step_at ? 1 : 0; }};
    const auto& fighter = track.g.world.get<game::player_component>(track.g.player);

    track.mapper().key(keys::W, true);
    track.g.run_for(0.1F);
    track.mapper().key(keys::LEFT_SHIFT, true);
    track.g.tick();
    track.mapper().key(keys::LEFT_SHIFT, false);
    track.mapper().key(keys::W, false);
    REQUIRE(fighter.is_rolling());

    bool grounded_all_along = true;
    for (int32 tick = 0; tick < 120; ++tick) {
        track.g.tick();
        grounded_all_along = grounded_all_along && track.g.grounded();
    }

    REQUIRE(grounded_all_along);
    REQUIRE(track.gone() > static_cast<float32>(step_at));
    REQUIRE(track.risen() == Catch::Approx(1.0F).margin(0.02F));
}

TEST_CASE("the lunges of a sword chain carry the body onto a step", "[game][walker]") {
    constexpr int32 step_at = 2;
    riding_track track{[](int32 along) { return along >= step_at ? 1 : 0; }};
    const auto& fighter = track.g.world.get<game::player_component>(track.g.player);

    track.mapper().key(keys::KEY_1, true);
    track.g.tick();
    track.mapper().key(keys::KEY_1, false);
    track.g.run_for(0.2F);
    REQUIRE(fighter.has_weapon());

    track.mapper().key(keys::W, true);
    track.g.run_for(0.12F);
    track.mapper().key(keys::W, false);
    track.g.run_for(0.3F);

    for (int32 strike = 0; strike < 3; ++strike) {
        track.mapper().button(mouse::buttons::LEFT, true);
        track.g.tick();
        track.mapper().button(mouse::buttons::LEFT, false);
        for (int32 tick = 0; tick < 200 && !fighter.can_cancel(); ++tick) {
            track.g.tick();
        }
    }
    track.g.run_for(1.0F);

    REQUIRE(fighter.get_swing_count() == 3);
    REQUIRE(track.g.grounded());
    REQUIRE(track.gone() > static_cast<float32>(step_at) - 0.4F);
    REQUIRE(track.risen() == Catch::Approx(1.0F).margin(0.02F));
}
