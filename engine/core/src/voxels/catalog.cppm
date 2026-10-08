export module vw.core:voxels.catalog;

import std;

import :types;
import :color;
import :voxels;

export namespace vw::voxels {

inline constexpr auto blue   = voxel_span{1, 11};
inline constexpr auto green  = voxel_span{12, 11};
inline constexpr auto brown  = voxel_span{23, 11};
inline constexpr auto amber  = voxel_span{34, 11};
inline constexpr auto red    = voxel_span{45, 11};
inline constexpr auto purple = voxel_span{56, 11};
inline constexpr auto gray   = voxel_span{67, 19};
inline constexpr auto white  = voxel{86};
inline constexpr auto black  = voxel{87};

inline constexpr std::array groups = {
    voxel_group{"blue", blue[0], 11},
    voxel_group{"green", green[0], 11},
    voxel_group{"brown", brown[0], 11},
    voxel_group{"amber", amber[0], 11},
    voxel_group{"red", red[0], 11},
    voxel_group{"purple", purple[0], 11},
    voxel_group{"gray", gray[0], 19},
    voxel_group{"mono", white, 2},
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
    voxel_desc{voxels::blue[6], "blue_6", {colors::blue_6}},
    voxel_desc{voxels::blue[7], "blue_7", {colors::blue_7}},
    voxel_desc{voxels::blue[8], "blue_8", {colors::blue_8}},
    voxel_desc{voxels::blue[9], "blue_9", {colors::blue_9}},
    voxel_desc{voxels::blue[10], "blue_10", {colors::blue_10}},
    voxel_desc{voxels::green[0], "green_0", {colors::green_0}},
    voxel_desc{voxels::green[1], "green_1", {colors::green_1}},
    voxel_desc{voxels::green[2], "green_2", {colors::green_2}},
    voxel_desc{voxels::green[3], "green_3", {colors::green_3}},
    voxel_desc{voxels::green[4], "green_4", {colors::green_4}},
    voxel_desc{voxels::green[5], "green_5", {colors::green_5}},
    voxel_desc{voxels::green[6], "green_6", {colors::green_6}},
    voxel_desc{voxels::green[7], "green_7", {colors::green_7}},
    voxel_desc{voxels::green[8], "green_8", {colors::green_8}},
    voxel_desc{voxels::green[9], "green_9", {colors::green_9}},
    voxel_desc{voxels::green[10], "green_10", {colors::green_10}},
    voxel_desc{voxels::brown[0], "brown_0", {colors::brown_0}},
    voxel_desc{voxels::brown[1], "brown_1", {colors::brown_1}},
    voxel_desc{voxels::brown[2], "brown_2", {colors::brown_2}},
    voxel_desc{voxels::brown[3], "brown_3", {colors::brown_3}},
    voxel_desc{voxels::brown[4], "brown_4", {colors::brown_4}},
    voxel_desc{voxels::brown[5], "brown_5", {colors::brown_5}},
    voxel_desc{voxels::brown[6], "brown_6", {colors::brown_6}},
    voxel_desc{voxels::brown[7], "brown_7", {colors::brown_7}},
    voxel_desc{voxels::brown[8], "brown_8", {colors::brown_8}},
    voxel_desc{voxels::brown[9], "brown_9", {colors::brown_9}},
    voxel_desc{voxels::brown[10], "brown_10", {colors::brown_10}},
    voxel_desc{voxels::amber[0], "amber_0", {colors::amber_0}},
    voxel_desc{voxels::amber[1], "amber_1", {colors::amber_1}},
    voxel_desc{voxels::amber[2], "amber_2", {colors::amber_2}},
    voxel_desc{voxels::amber[3], "amber_3", {colors::amber_3}},
    voxel_desc{voxels::amber[4], "amber_4", {colors::amber_4}},
    voxel_desc{voxels::amber[5], "amber_5", {colors::amber_5}},
    voxel_desc{voxels::amber[6], "amber_6", {colors::amber_6}},
    voxel_desc{voxels::amber[7], "amber_7", {colors::amber_7}},
    voxel_desc{voxels::amber[8], "amber_8", {colors::amber_8}},
    voxel_desc{voxels::amber[9], "amber_9", {colors::amber_9}},
    voxel_desc{voxels::amber[10], "amber_10", {colors::amber_10}},
    voxel_desc{voxels::red[0], "red_0", {colors::red_0}},
    voxel_desc{voxels::red[1], "red_1", {colors::red_1}},
    voxel_desc{voxels::red[2], "red_2", {colors::red_2}},
    voxel_desc{voxels::red[3], "red_3", {colors::red_3}},
    voxel_desc{voxels::red[4], "red_4", {colors::red_4}},
    voxel_desc{voxels::red[5], "red_5", {colors::red_5}},
    voxel_desc{voxels::red[6], "red_6", {colors::red_6}},
    voxel_desc{voxels::red[7], "red_7", {colors::red_7}},
    voxel_desc{voxels::red[8], "red_8", {colors::red_8}},
    voxel_desc{voxels::red[9], "red_9", {colors::red_9}},
    voxel_desc{voxels::red[10], "red_10", {colors::red_10}},
    voxel_desc{voxels::purple[0], "purple_0", {colors::purple_0}},
    voxel_desc{voxels::purple[1], "purple_1", {colors::purple_1}},
    voxel_desc{voxels::purple[2], "purple_2", {colors::purple_2}},
    voxel_desc{voxels::purple[3], "purple_3", {colors::purple_3}},
    voxel_desc{voxels::purple[4], "purple_4", {colors::purple_4}},
    voxel_desc{voxels::purple[5], "purple_5", {colors::purple_5}},
    voxel_desc{voxels::purple[6], "purple_6", {colors::purple_6}},
    voxel_desc{voxels::purple[7], "purple_7", {colors::purple_7}},
    voxel_desc{voxels::purple[8], "purple_8", {colors::purple_8}},
    voxel_desc{voxels::purple[9], "purple_9", {colors::purple_9}},
    voxel_desc{voxels::purple[10], "purple_10", {colors::purple_10}},
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
    voxel_desc{voxels::gray[10], "gray_10", {colors::gray_10}},
    voxel_desc{voxels::gray[11], "gray_11", {colors::gray_11}},
    voxel_desc{voxels::gray[12], "gray_12", {colors::gray_12}},
    voxel_desc{voxels::gray[13], "gray_13", {colors::gray_13}},
    voxel_desc{voxels::gray[14], "gray_14", {colors::gray_14}},
    voxel_desc{voxels::gray[15], "gray_15", {colors::gray_15}},
    voxel_desc{voxels::gray[16], "gray_16", {colors::gray_16}},
    voxel_desc{voxels::gray[17], "gray_17", {colors::gray_17}},
    voxel_desc{voxels::gray[18], "gray_18", {colors::gray_18}},
    voxel_desc{voxels::white, "white", {colors::white}},
    voxel_desc{voxels::black, "black", {colors::black}},
};

}  // namespace vw
