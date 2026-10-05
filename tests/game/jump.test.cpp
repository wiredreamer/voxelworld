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
        for (int32 frame = 0; frame < 200 && !grounded(); ++frame) {
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

TEST_CASE("holding jump does not hop again", "[game][jump]") {
    grounded_world g;
    REQUIRE(g.settle());

    const uint32 before = g.jumps();
    g.world.system<game::input_system>().mapper().key(keys::SPACE, true);
    g.run_for(2.0F);
    REQUIRE(g.jumps() == before + 1);
}
