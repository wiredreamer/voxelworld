export module vw.core:voxels.catalog;

import std;

import :types;
import :color;
import :voxels;

export namespace vw::voxels {

inline constexpr auto blue   = voxel_span{1, 6};
inline constexpr auto green  = voxel_span{7, 6};
inline constexpr auto brown  = voxel_span{13, 6};
inline constexpr auto amber  = voxel_span{19, 6};
inline constexpr auto red    = voxel_span{25, 6};
inline constexpr auto purple = voxel_span{31, 6};
inline constexpr auto gray   = voxel_span{37, 10};
inline constexpr auto white  = voxel{47};
inline constexpr auto black  = voxel{48};

inline constexpr auto glow_blue    = voxel{49};
inline constexpr auto glow_green   = voxel{50};
inline constexpr auto glow_amber   = voxel{51};
inline constexpr auto glow_red     = voxel{52};
inline constexpr auto glow_purple  = voxel{53};
inline constexpr auto glow_white   = voxel{54};

inline constexpr auto lamp_blue    = voxel{55};
inline constexpr auto lamp_green   = voxel{56};
inline constexpr auto lamp_amber   = voxel{57};
inline constexpr auto lamp_red     = voxel{58};
inline constexpr auto lamp_purple  = voxel{59};
inline constexpr auto lamp_white   = voxel{60};

inline constexpr auto fire_blue    = voxel{61};
inline constexpr auto fire_green   = voxel{62};
inline constexpr auto fire_amber   = voxel{63};
inline constexpr auto fire_red     = voxel{64};
inline constexpr auto fire_purple  = voxel{65};
inline constexpr auto fire_white   = voxel{66};

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
    voxel_group{"lamp", lamp_blue, 6},
    voxel_group{"fire", fire_blue, 6},
};

}  // namespace vw::voxels

export namespace vw {

inline constexpr std::array default_voxel_catalog = {
    voxel_desc{voxels::air, "air", {}, voxel_surface::invisible},
    voxel_desc{voxels::blue[0], "blue_0", {colors::blue_0}},
    voxel_desc{voxels::blue[1], "blue_1", {colors::blue_1}},
    voxel_desc{voxels::blue[2], "blue_2", {colors::blue_2}},
    voxel_desc{voxels::blue[3], "blue_3", {colors::blue_3}},
    voxel_desc{voxels::blue[4], "blue_4", {colors::blue_4}},
    voxel_desc{voxels::blue[5], "blue_5", {colors::blue_5}},
    voxel_desc{voxels::green[0], "green_0", {colors::green_0}},
    voxel_desc{voxels::green[1], "green_1", {colors::green_1}},
    voxel_desc{voxels::green[2], "green_2", {colors::green_2}},
    voxel_desc{voxels::green[3], "green_3", {colors::green_3}},
    voxel_desc{voxels::green[4], "green_4", {colors::green_4}},
    voxel_desc{voxels::green[5], "green_5", {colors::green_5}},
    voxel_desc{voxels::brown[0], "brown_0", {colors::brown_0}},
    voxel_desc{voxels::brown[1], "brown_1", {colors::brown_1}},
    voxel_desc{voxels::brown[2], "brown_2", {colors::brown_2}},
    voxel_desc{voxels::brown[3], "brown_3", {colors::brown_3}},
    voxel_desc{voxels::brown[4], "brown_4", {colors::brown_4}},
    voxel_desc{voxels::brown[5], "brown_5", {colors::brown_5}},
    voxel_desc{voxels::amber[0], "amber_0", {colors::amber_0}},
    voxel_desc{voxels::amber[1], "amber_1", {colors::amber_1}},
    voxel_desc{voxels::amber[2], "amber_2", {colors::amber_2}},
    voxel_desc{voxels::amber[3], "amber_3", {colors::amber_3}},
    voxel_desc{voxels::amber[4], "amber_4", {colors::amber_4}},
    voxel_desc{voxels::amber[5], "amber_5", {colors::amber_5}},
    voxel_desc{voxels::red[0], "red_0", {colors::red_0}},
    voxel_desc{voxels::red[1], "red_1", {colors::red_1}},
    voxel_desc{voxels::red[2], "red_2", {colors::red_2}},
    voxel_desc{voxels::red[3], "red_3", {colors::red_3}},
    voxel_desc{voxels::red[4], "red_4", {colors::red_4}},
    voxel_desc{voxels::red[5], "red_5", {colors::red_5}},
    voxel_desc{voxels::purple[0], "purple_0", {colors::purple_0}},
    voxel_desc{voxels::purple[1], "purple_1", {colors::purple_1}},
    voxel_desc{voxels::purple[2], "purple_2", {colors::purple_2}},
    voxel_desc{voxels::purple[3], "purple_3", {colors::purple_3}},
    voxel_desc{voxels::purple[4], "purple_4", {colors::purple_4}},
    voxel_desc{voxels::purple[5], "purple_5", {colors::purple_5}},
    voxel_desc{voxels::gray[0], "gray_0", {colors::gray_0}},
    voxel_desc{voxels::gray[1], "gray_1", {colors::gray_1}},
    voxel_desc{voxels::gray[2], "gray_2", {colors::gray_2}},
    voxel_desc{voxels::gray[3], "gray_3", {colors::gray_3}},
    voxel_desc{voxels::gray[4], "gray_4", {colors::gray_4}},
    voxel_desc{voxels::gray[5], "gray_5", {colors::gray_5}},
    voxel_desc{voxels::gray[6], "gray_6", {colors::gray_6}},
    voxel_desc{voxels::gray[7], "gray_7", {colors::gray_7}},
    voxel_desc{voxels::gray[8], "gray_8", {colors::gray_8}},
    voxel_desc{voxels::gray[9], "gray_9", {colors::gray_9}},
    voxel_desc{voxels::white, "white", {colors::white}},
    voxel_desc{voxels::black, "black", {colors::black}},
    voxel_desc{voxels::glow_blue, "glow_blue", {colors::blue_4, 0, 200}},
    voxel_desc{voxels::glow_green, "glow_green", {colors::green_4, 0, 200}},
    voxel_desc{voxels::glow_amber, "glow_amber", {colors::amber_4, 0, 200}},
    voxel_desc{voxels::glow_red, "glow_red", {colors::red_4, 0, 200}},
    voxel_desc{voxels::glow_purple, "glow_purple", {colors::purple_4, 0, 200}},
    voxel_desc{voxels::glow_white, "glow_white", {colors::white, 0, 200}},
    voxel_desc{voxels::lamp_blue, "lamp_blue", {colors::blue_4, 14, 200}},
    voxel_desc{voxels::lamp_green, "lamp_green", {colors::green_4, 14, 200}},
    voxel_desc{voxels::lamp_amber, "lamp_amber", {colors::amber_4, 14, 200}},
    voxel_desc{voxels::lamp_red, "lamp_red", {colors::red_4, 14, 200}},
    voxel_desc{voxels::lamp_purple, "lamp_purple", {colors::purple_4, 14, 200}},
    voxel_desc{voxels::lamp_white, "lamp_white", {colors::white, 14, 200}},
    voxel_desc{voxels::fire_blue, "fire_blue", {colors::blue_4, 15, 255}},
    voxel_desc{voxels::fire_green, "fire_green", {colors::green_4, 15, 255}},
    voxel_desc{voxels::fire_amber, "fire_amber", {colors::amber_4, 15, 255}},
    voxel_desc{voxels::fire_red, "fire_red", {colors::red_4, 15, 255}},
    voxel_desc{voxels::fire_purple, "fire_purple", {colors::purple_4, 15, 255}},
    voxel_desc{voxels::fire_white, "fire_white", {colors::white, 15, 255}},
};

}  // namespace vw
