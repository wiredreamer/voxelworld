#include <catch2/catch_test_macros.hpp>

import std;

import vw.core;
import vw.asset;
import vw.ecs;
import vw.world;
import vw.game;

using namespace vw;

namespace {

constexpr int32 units_per_voxel = 8;

struct game_world {
    ecs::world world;
    asset::vox_parser_plain parser;
    asset::model_library library{
        world.resource<asset::model_registry>(), world.voxel_types(), VW_ASSET_DIR
    };
    asset::asset_storage assets{parser, library};
};

auto shallow_params() -> ecs::perlin_terrain_generator::params {
    ecs::perlin_terrain_generator::params p{};
    p.world_bottom_y = -192;
    return p;
}

auto start_streaming(ecs::world& w, job_system& jobs) -> void {
    auto& models = w.resource<asset::model_registry>();
    auto& gs     = w.system<ecs::world_grid_system>();

    gs.set_grid(std::make_unique<ecs::world_grid>(w, units_per_voxel));
    gs.set_loader(
        std::make_unique<ecs::chunk_loader>(
            std::make_unique<ecs::perlin_terrain_generator>(
                models.get_identity_pool(), models.get_page_pool(), shallow_params()
            ),
            jobs
        )
    );

    const auto viewer = w.create()
                            .with<ecs::transform_component>()
                            .with<ecs::world_view_component>()
                            .get_entity();
    gs.modify_view(viewer).set_view_distance(2);
}

auto tick_until_placed(ecs::world& w, ecs::entity ent) -> bool {
    const auto& placement = w.system<game::surface_placement_system>();

    for (int32 frame = 0; frame < 4000 && placement.is_waiting(ent); ++frame) {
        w.update(0.016F);
        w.clear_changed();
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    return !placement.is_waiting(ent);
}

}  // namespace

TEST_CASE("the game installs its systems into a world", "[game]") {
    game_world g;
    auto& w = g.world;
    REQUIRE(w.try_system<game::surface_placement_system>() == nullptr);

    game::install_systems(w, g.assets);

    REQUIRE(w.try_system<game::surface_placement_system>() != nullptr);
    REQUIRE(w.try_system<game::input_system>() != nullptr);
    REQUIRE(w.try_system<game::player_system>() != nullptr);

    const auto timings = w.get_extension_timings();
    REQUIRE(timings.size() == 3);
    REQUIRE(timings[0].name == "surface_place");
    REQUIRE(timings[1].name == "input");
    REQUIRE(timings[2].name == "player");
}

TEST_CASE("an entity waits for the ground when there is no terrain yet", "[game][spawn]") {
    game_world g;
    auto& w = g.world;
    game::install_systems(w, g.assets);

    const auto ent = w.create().with<ecs::transform_component>().get_entity();
    w.system<ecs::transform_system>().modify(ent).set_position({0.0F, 500.0F, 0.0F});
    w.system<game::surface_placement_system>().place(ent, {0.0F, 0.0F});

    w.update(0.016F);

    REQUIRE(w.system<game::surface_placement_system>().is_waiting(ent));
    REQUIRE(w.get<ecs::transform_component>(ent).get_position().y == 500.0F);
}

TEST_CASE("an entity is put above the surface once its column is loaded", "[game][spawn]") {
    job_system jobs;
    game_world g;
    auto& w = g.world;
    game::install_systems(w, g.assets);
    start_streaming(w, jobs);

    constexpr vec2f spot{40.0F, 24.0F};
    constexpr float32 clearance = 3.0F;

    const auto ent = w.create().with<ecs::transform_component>().get_entity();
    w.system<game::surface_placement_system>().place(ent, spot, clearance);

    REQUIRE(tick_until_placed(w, ent));

    const auto* grid   = w.system<ecs::world_grid_system>().grid();
    const auto surface = grid->get_surface_voxel_y(
        static_cast<int32>(spot.x) / units_per_voxel, static_cast<int32>(spot.y) / units_per_voxel
    );
    REQUIRE(surface.has_value());

    const auto at = w.get<ecs::transform_component>(ent).get_position();
    REQUIRE(at.x == spot.x);
    REQUIRE(at.z == spot.y);
    REQUIRE(at.y == (static_cast<float32>(*surface) + clearance) * static_cast<float32>(units_per_voxel));
}
