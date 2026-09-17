export module vw.core:voxels.catalog;

import std;

import :types;
import :color;
import :voxels;

export namespace vw::voxels {

namespace palette {
inline constexpr auto category = voxel_category{0};

inline constexpr auto blue   = voxel_span{category, 1, 6};
inline constexpr auto green  = voxel_span{category, 7, 6};
inline constexpr auto brown  = voxel_span{category, 13, 6};
inline constexpr auto amber  = voxel_span{category, 19, 6};
inline constexpr auto red    = voxel_span{category, 25, 6};
inline constexpr auto purple = voxel_span{category, 31, 6};
inline constexpr auto gray   = voxel_span{category, 37, 10};
inline constexpr auto white  = voxel{category, 47};
inline constexpr auto black  = voxel{category, 48};

inline constexpr auto glow_blue   = voxel{category, 49};
inline constexpr auto glow_green  = voxel{category, 50};
inline constexpr auto glow_amber  = voxel{category, 51};
inline constexpr auto glow_red    = voxel{category, 52};
inline constexpr auto glow_purple = voxel{category, 53};
inline constexpr auto glow_white  = voxel{category, 54};

inline constexpr std::array groups = {
    voxel_group{"blue", blue[0], 6},
    voxel_group{"green", green[0], 6},
    voxel_group{"brown", brown[0], 6},
    voxel_group{"amber", amber[0], 6},
    voxel_group{"red", red[0], 6},
    voxel_group{"purple", purple[0], 6},
    voxel_group{"gray", gray[0], 10},
    voxel_group{"mono", white, 2},
    voxel_group{"glow", glow_blue, 6},
};
}  // namespace palette

namespace world {
inline constexpr auto category = voxel_category{1};

inline constexpr auto grass      = voxel_span{category, 1, 3};
inline constexpr auto grass_dark = voxel_span{category, 4, 3};
inline constexpr auto grass_dry  = voxel_span{category, 7, 3};
inline constexpr auto leaves     = voxel_span{category, 10, 3};
inline constexpr auto dirt       = voxel_span{category, 13, 3};
inline constexpr auto sand       = voxel_span{category, 16, 3};
inline constexpr auto wood       = voxel_span{category, 19, 3};
inline constexpr auto clay       = voxel_span{category, 22, 3};
inline constexpr auto stone      = voxel_span{category, 25, 3};
inline constexpr auto stone_deep = voxel_span{category, 28, 3};
inline constexpr auto bedrock    = voxel{category, 31};
inline constexpr auto gravel     = voxel_span{category, 32, 3};
inline constexpr auto snow       = voxel_span{category, 35, 3};
inline constexpr auto ice        = voxel_span{category, 38, 3};
inline constexpr auto ash        = voxel_span{category, 41, 3};
inline constexpr auto ore_gold   = voxel_span{category, 44, 3};
inline constexpr auto ore_iron   = voxel_span{category, 47, 3};
inline constexpr auto crystal    = voxel_span{category, 50, 3};
inline constexpr auto water      = voxel{category, 53};
inline constexpr auto lava       = voxel{category, 54};
inline constexpr auto magma      = voxel{category, 55};
inline constexpr auto glowstone  = voxel{category, 56};

inline constexpr std::array groups = {
    voxel_group{"plants", grass[0], 12},
    voxel_group{"soil", dirt[0], 6},
    voxel_group{"wood", wood[0], 3},
    voxel_group{"stone", clay[0], 13},
    voxel_group{"ice", snow[0], 6},
    voxel_group{"ash", ash[0], 3},
    voxel_group{"ore", ore_gold[0], 9},
    voxel_group{"liquids", water, 3},
    voxel_group{"light", glowstone, 1},
};
}  // namespace world

}  // namespace vw::voxels

export namespace vw {

inline constexpr std::array default_voxel_sets = {
    voxel_set{
        .category = voxels::palette::category,
        .name     = "palette",
        .kind     = voxel_set_kind::palette,
        .groups   = voxels::palette::groups
    },
    voxel_set{
        .category = voxels::world::category,
        .name     = "world",
        .kind     = voxel_set_kind::materials,
        .groups   = voxels::world::groups
    },
};

inline constexpr std::array default_voxel_catalog = {
    voxel_desc{voxels::air, "air", {}, voxel_surface::invisible},
    voxel_desc{voxels::palette::blue[0], "palette.blue_0", {colors::blue_0}},
    voxel_desc{voxels::palette::blue[1], "palette.blue_1", {colors::blue_1}},
    voxel_desc{voxels::palette::blue[2], "palette.blue_2", {colors::blue_2}},
    voxel_desc{voxels::palette::blue[3], "palette.blue_3", {colors::blue_3}},
    voxel_desc{voxels::palette::blue[4], "palette.blue_4", {colors::blue_4}},
    voxel_desc{voxels::palette::blue[5], "palette.blue_5", {colors::blue_5}},
    voxel_desc{voxels::palette::green[0], "palette.green_0", {colors::green_0}},
    voxel_desc{voxels::palette::green[1], "palette.green_1", {colors::green_1}},
    voxel_desc{voxels::palette::green[2], "palette.green_2", {colors::green_2}},
    voxel_desc{voxels::palette::green[3], "palette.green_3", {colors::green_3}},
    voxel_desc{voxels::palette::green[4], "palette.green_4", {colors::green_4}},
    voxel_desc{voxels::palette::green[5], "palette.green_5", {colors::green_5}},
    voxel_desc{voxels::palette::brown[0], "palette.brown_0", {colors::brown_0}},
    voxel_desc{voxels::palette::brown[1], "palette.brown_1", {colors::brown_1}},
    voxel_desc{voxels::palette::brown[2], "palette.brown_2", {colors::brown_2}},
    voxel_desc{voxels::palette::brown[3], "palette.brown_3", {colors::brown_3}},
    voxel_desc{voxels::palette::brown[4], "palette.brown_4", {colors::brown_4}},
    voxel_desc{voxels::palette::brown[5], "palette.brown_5", {colors::brown_5}},
    voxel_desc{voxels::palette::amber[0], "palette.amber_0", {colors::amber_0}},
    voxel_desc{voxels::palette::amber[1], "palette.amber_1", {colors::amber_1}},
    voxel_desc{voxels::palette::amber[2], "palette.amber_2", {colors::amber_2}},
    voxel_desc{voxels::palette::amber[3], "palette.amber_3", {colors::amber_3}},
    voxel_desc{voxels::palette::amber[4], "palette.amber_4", {colors::amber_4}},
    voxel_desc{voxels::palette::amber[5], "palette.amber_5", {colors::amber_5}},
    voxel_desc{voxels::palette::red[0], "palette.red_0", {colors::red_0}},
    voxel_desc{voxels::palette::red[1], "palette.red_1", {colors::red_1}},
    voxel_desc{voxels::palette::red[2], "palette.red_2", {colors::red_2}},
    voxel_desc{voxels::palette::red[3], "palette.red_3", {colors::red_3}},
    voxel_desc{voxels::palette::red[4], "palette.red_4", {colors::red_4}},
    voxel_desc{voxels::palette::red[5], "palette.red_5", {colors::red_5}},
    voxel_desc{voxels::palette::purple[0], "palette.purple_0", {colors::purple_0}},
    voxel_desc{voxels::palette::purple[1], "palette.purple_1", {colors::purple_1}},
    voxel_desc{voxels::palette::purple[2], "palette.purple_2", {colors::purple_2}},
    voxel_desc{voxels::palette::purple[3], "palette.purple_3", {colors::purple_3}},
    voxel_desc{voxels::palette::purple[4], "palette.purple_4", {colors::purple_4}},
    voxel_desc{voxels::palette::purple[5], "palette.purple_5", {colors::purple_5}},
    voxel_desc{voxels::palette::gray[0], "palette.gray_0", {colors::gray_0}},
    voxel_desc{voxels::palette::gray[1], "palette.gray_1", {colors::gray_1}},
    voxel_desc{voxels::palette::gray[2], "palette.gray_2", {colors::gray_2}},
    voxel_desc{voxels::palette::gray[3], "palette.gray_3", {colors::gray_3}},
    voxel_desc{voxels::palette::gray[4], "palette.gray_4", {colors::gray_4}},
    voxel_desc{voxels::palette::gray[5], "palette.gray_5", {colors::gray_5}},
    voxel_desc{voxels::palette::gray[6], "palette.gray_6", {colors::gray_6}},
    voxel_desc{voxels::palette::gray[7], "palette.gray_7", {colors::gray_7}},
    voxel_desc{voxels::palette::gray[8], "palette.gray_8", {colors::gray_8}},
    voxel_desc{voxels::palette::gray[9], "palette.gray_9", {colors::gray_9}},
    voxel_desc{voxels::palette::white, "palette.white", {colors::white}},
    voxel_desc{voxels::palette::black, "palette.black", {colors::black}},
    voxel_desc{voxels::palette::glow_blue, "palette.glow_blue", {colors::blue_4, 0, 200}},
    voxel_desc{voxels::palette::glow_green, "palette.glow_green", {colors::green_4, 0, 200}},
    voxel_desc{voxels::palette::glow_amber, "palette.glow_amber", {colors::amber_5, 0, 200}},
    voxel_desc{voxels::palette::glow_red, "palette.glow_red", {colors::red_4, 0, 200}},
    voxel_desc{voxels::palette::glow_purple, "palette.glow_purple", {colors::purple_4, 0, 200}},
    voxel_desc{voxels::palette::glow_white, "palette.glow_white", {colors::white, 0, 200}},
    voxel_desc{voxels::world::grass[0], "world.grass_0", {colors::green_2}},
    voxel_desc{voxels::world::grass[1], "world.grass_1", {colors::green_3}},
    voxel_desc{voxels::world::grass[2], "world.grass_2", {colors::green_4}},
    voxel_desc{voxels::world::grass_dark[0], "world.grass_dark_0", {colors::green_0}},
    voxel_desc{voxels::world::grass_dark[1], "world.grass_dark_1", {colors::green_1}},
    voxel_desc{voxels::world::grass_dark[2], "world.grass_dark_2", {colors::green_2}},
    voxel_desc{voxels::world::grass_dry[0], "world.grass_dry_0", {colors::green_3}},
    voxel_desc{voxels::world::grass_dry[1], "world.grass_dry_1", {colors::green_4}},
    voxel_desc{voxels::world::grass_dry[2], "world.grass_dry_2", {colors::green_5}},
    voxel_desc{voxels::world::leaves[0], "world.leaves_0", {colors::green_1}},
    voxel_desc{voxels::world::leaves[1], "world.leaves_1", {colors::green_2}},
    voxel_desc{voxels::world::leaves[2], "world.leaves_2", {colors::green_3}},
    voxel_desc{voxels::world::dirt[0], "world.dirt_0", {colors::brown_0}},
    voxel_desc{voxels::world::dirt[1], "world.dirt_1", {colors::brown_1}},
    voxel_desc{voxels::world::dirt[2], "world.dirt_2", {colors::brown_2}},
    voxel_desc{voxels::world::sand[0], "world.sand_0", {colors::brown_3}},
    voxel_desc{voxels::world::sand[1], "world.sand_1", {colors::brown_4}},
    voxel_desc{voxels::world::sand[2], "world.sand_2", {colors::brown_5}},
    voxel_desc{voxels::world::wood[0], "world.wood_0", {colors::amber_0}},
    voxel_desc{voxels::world::wood[1], "world.wood_1", {colors::amber_1}},
    voxel_desc{voxels::world::wood[2], "world.wood_2", {colors::amber_2}},
    voxel_desc{voxels::world::clay[0], "world.clay_0", {colors::amber_2}},
    voxel_desc{voxels::world::clay[1], "world.clay_1", {colors::amber_3}},
    voxel_desc{voxels::world::clay[2], "world.clay_2", {colors::amber_4}},
    voxel_desc{voxels::world::stone[0], "world.stone_0", {colors::gray_4}},
    voxel_desc{voxels::world::stone[1], "world.stone_1", {colors::gray_5}},
    voxel_desc{voxels::world::stone[2], "world.stone_2", {colors::gray_6}},
    voxel_desc{voxels::world::stone_deep[0], "world.stone_deep_0", {colors::gray_1}},
    voxel_desc{voxels::world::stone_deep[1], "world.stone_deep_1", {colors::gray_2}},
    voxel_desc{voxels::world::stone_deep[2], "world.stone_deep_2", {colors::gray_3}},
    voxel_desc{voxels::world::bedrock, "world.bedrock", {colors::gray_0}},
    voxel_desc{voxels::world::gravel[0], "world.gravel_0", {colors::gray_3}},
    voxel_desc{voxels::world::gravel[1], "world.gravel_1", {colors::gray_4}},
    voxel_desc{voxels::world::gravel[2], "world.gravel_2", {colors::gray_5}},
    voxel_desc{voxels::world::snow[0], "world.snow_0", {colors::gray_8}},
    voxel_desc{voxels::world::snow[1], "world.snow_1", {colors::gray_9}},
    voxel_desc{voxels::world::snow[2], "world.snow_2", {colors::white}},
    voxel_desc{voxels::world::ice[0], "world.ice_0", {colors::blue_3}},
    voxel_desc{voxels::world::ice[1], "world.ice_1", {colors::blue_4}},
    voxel_desc{voxels::world::ice[2], "world.ice_2", {colors::blue_5}},
    voxel_desc{voxels::world::ash[0], "world.ash_0", {colors::gray_0}},
    voxel_desc{voxels::world::ash[1], "world.ash_1", {colors::gray_1}},
    voxel_desc{voxels::world::ash[2], "world.ash_2", {colors::gray_2}},
    voxel_desc{voxels::world::ore_gold[0], "world.ore_gold_0", {colors::amber_3}},
    voxel_desc{voxels::world::ore_gold[1], "world.ore_gold_1", {colors::amber_4}},
    voxel_desc{voxels::world::ore_gold[2], "world.ore_gold_2", {colors::amber_5}},
    voxel_desc{voxels::world::ore_iron[0], "world.ore_iron_0", {colors::gray_5}},
    voxel_desc{voxels::world::ore_iron[1], "world.ore_iron_1", {colors::gray_6}},
    voxel_desc{voxels::world::ore_iron[2], "world.ore_iron_2", {colors::gray_7}},
    voxel_desc{voxels::world::crystal[0], "world.crystal_0", {colors::blue_3, 0, 160}},
    voxel_desc{voxels::world::crystal[1], "world.crystal_1", {colors::blue_4, 0, 160}},
    voxel_desc{voxels::world::crystal[2], "world.crystal_2", {colors::blue_5, 0, 160}},
    voxel_desc{voxels::world::water, "world.water", {colors::blue_2}},
    voxel_desc{voxels::world::lava, "world.lava", {colors::red_5, 15, 255}},
    voxel_desc{voxels::world::magma, "world.magma", {colors::amber_2, 8, 120}},
    voxel_desc{voxels::world::glowstone, "world.glowstone", {colors::amber_5, 14, 200}},
};

}  // namespace vw
