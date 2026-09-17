module vw.arena;

import std;

import vw.core;
import vw.asset;
import vw.ecs;
import vw.world;
import vw.platform;
import vw.gfx;

namespace vw::arena {

auto setup_world_grid(gfx::engine& engine) -> world_setup_result {
    auto& world    = engine.get_world();
    auto& registry = world.resource<asset::model_registry>();

    ecs::perlin_terrain_generator::params params{
        .world_units_per_voxel = 16,
    };

    auto generator = std::make_unique<ecs::perlin_terrain_generator>(
        registry.get_identity_pool(), registry.get_page_pool(), params
    );

    auto& gs = world.system<ecs::world_grid_system>();
    gs.set_grid(std::make_unique<ecs::world_grid>(world, params.world_units_per_voxel));
    gs.set_loader(std::make_unique<ecs::chunk_loader>(std::move(generator)));

    return {.generator_params = params};
}

}  // namespace vw::arena
