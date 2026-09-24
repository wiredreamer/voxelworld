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

    auto& jobs = engine.get_jobs();
    auto& gs   = world.system<ecs::world_grid_system>();
    gs.set_grid(std::make_unique<ecs::world_grid>(world, params.world_units_per_voxel));
    gs.set_loader(
        std::make_unique<ecs::chunk_loader>(std::move(generator), jobs), jobs
    );

    const auto chunk_units =
        static_cast<float32>(ecs::chunk::size * params.world_units_per_voxel);

    world.system<ecs::lod_system>().set_default_base_distance(
        static_cast<float32>(ecs::default_lod_base_chunks) * chunk_units
    );

    return {.generator_params = params};
}

}  // namespace vw::arena
