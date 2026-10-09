export module vw.world:terrain.settings;
import :terrain.perlin;
import :terrain.biomes;

import std;

import vw.core;

export namespace vw::ecs {

using terrain_params = perlin_terrain_generator::params;

struct terrain_number_slot {
    std::string_view key;
    std::variant<float32*, int32*> value;
    float32 min = 0.0F;
    float32 max = 1.0F;
};

[[nodiscard]] auto terrain_number_count() -> std::size_t;
[[nodiscard]] auto terrain_number(terrain_params& params, std::size_t index) -> terrain_number_slot;
[[nodiscard]] auto terrain_flag_count() -> std::size_t;
[[nodiscard]] auto terrain_flag_key(std::size_t index) -> std::string_view;
[[nodiscard]] auto terrain_flag(terrain_params& params, std::size_t index) -> bool&;

[[nodiscard]] auto parse_terrain_settings(
    std::string_view text, const voxel_registry& voxels, terrain_params base
) -> std::expected<terrain_params, std::string>;

[[nodiscard]] auto load_terrain_settings(
    const std::filesystem::path& file, const voxel_registry& voxels, terrain_params base
) -> std::expected<terrain_params, std::string>;

[[nodiscard]] auto dump_terrain_settings(const terrain_params& params, const voxel_registry& voxels)
    -> std::string;

[[nodiscard]] auto save_terrain_settings(
    const std::filesystem::path& file, const terrain_params& params, const voxel_registry& voxels
) -> std::expected<void, std::string>;

}  // namespace vw::ecs
