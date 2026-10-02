export module vw.game;

export import :input;
export import :player.control;
export import :spawn.surface_placement;

import vw.asset;
import vw.world;

export namespace vw::game {

// см. docs/ENGINE.md#игра
auto install_systems(ecs::world& world, asset::asset_storage& assets) -> void;

}  // namespace vw::game
