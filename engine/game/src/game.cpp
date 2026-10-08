module vw.game;

import std;
import vw.core;
import vw.asset;
import vw.ecs;
import vw.world;

namespace vw::game {

auto install_systems(ecs::world& world, asset::asset_storage& assets) -> void {
    world.add_system<surface_placement_system>(ecs::tick_stage::before_engine);
    world.add_system<input_system>(ecs::tick_stage::before_engine);
    world.add_system<player_system>(ecs::tick_stage::before_engine, assets);
    world.add_system<projectile_system>(ecs::tick_stage::before_engine);
}

}  // namespace vw::game
