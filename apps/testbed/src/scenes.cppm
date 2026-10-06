export module vw.testbed:scenes;

export import :scenes.terrain;
export import :scenes.voxel_edits;
export import :scenes.lamp_edits;
export import :scenes.standing_lights;
export import :scenes.blob_shadows;
export import :scenes.lod_probe;
export import :scenes.animated_crowd;
export import :scenes.terrain_lab;

import std;

import vw.core;
import :args;
import :scene;

export namespace vw::testbed {

[[nodiscard]] auto find_scene(std::string_view name, const arg_reader& args)
    -> std::optional<scene_factory>;

[[nodiscard]] auto scene_names() -> std::vector<std::string_view>;

}  // namespace vw::testbed
