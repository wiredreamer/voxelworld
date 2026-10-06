export module vw.world:terrain.palette;

import std;

import vw.core;

export namespace vw::ecs::terrain {

inline constexpr auto grass      = voxel_span{voxels::green[4].value, 5};
inline constexpr auto grass_dark = voxel_span{voxels::green[0].value, 5};
inline constexpr auto grass_dry  = voxel_span{voxels::green[6].value, 5};
inline constexpr auto leaves     = voxel_span{voxels::green[2].value, 5};
inline constexpr auto dirt       = voxel_span{voxels::brown[0].value, 5};
inline constexpr auto sand       = voxel_span{voxels::brown[6].value, 5};
inline constexpr auto wood       = voxel_span{voxels::amber[0].value, 5};
inline constexpr auto clay       = voxel_span{voxels::amber[4].value, 5};
inline constexpr auto stone      = voxel_span{voxels::gray[8].value, 5};
inline constexpr auto stone_deep = voxel_span{voxels::gray[2].value, 5};
inline constexpr auto gravel     = voxel_span{voxels::gray[6].value, 5};
inline constexpr auto ash        = voxel_span{voxels::gray[0].value, 5};
inline constexpr auto snow       = voxel_span{voxels::gray[16].value, 5};
inline constexpr auto ice        = voxel_span{voxels::blue[6].value, 5};
inline constexpr auto ore_gold   = voxel_span{voxels::amber[6].value, 5};
inline constexpr auto ore_iron   = voxel_span{voxels::gray[10].value, 5};

inline constexpr auto bedrock   = voxels::gray[0];
inline constexpr auto water     = voxels::blue[4];
inline constexpr auto crystal   = voxels::glow_blue;
inline constexpr auto glowstone = voxels::lamp_amber;
inline constexpr auto magma     = voxels::lamp_red;
inline constexpr auto lava      = voxels::fire_red;

}  // namespace vw::ecs::terrain
