export module vw.world:flora.grass;
import :grid.cell;
import :terrain.generator;

import std;

import vw.core;
import vw.asset;

export namespace vw::ecs {

inline constexpr int32 grass_tuft_footprint  = default_world_units_per_voxel;
inline constexpr int32 grass_tuft_min_height = 2;
inline constexpr int32 grass_tuft_height_step = 2;
inline constexpr int32 grass_tuft_max_height = 7;
inline constexpr int32 grass_stalk_width     = 2;
inline constexpr int32 flower_min_height     = 6;
inline constexpr int32 flower_max_height     = grass_tuft_max_height;

// см. docs/world.md#колоски
[[nodiscard]] auto grow_grass_tuft(asset::model_registry& models, uint8 form, voxel look)
    -> std::shared_ptr<asset::model>;

}  // namespace vw::ecs
