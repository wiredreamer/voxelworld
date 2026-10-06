export module vw.world:terrain.settings;
import :terrain.perlin;
import :terrain.biomes;

import std;

import vw.core;

export namespace vw::ecs {

using terrain_params = perlin_terrain_generator::params;

struct terrain_number_field {
    std::string_view key;
    std::variant<float32 terrain_params::*, int32 terrain_params::*> member;
    float32 min = 0.0F;
    float32 max = 1.0F;
};

struct terrain_flag_field {
    std::string_view key;
    bool terrain_params::* member;
};

[[nodiscard]] auto terrain_number_fields() -> std::span<const terrain_number_field>;
[[nodiscard]] auto terrain_flag_fields() -> std::span<const terrain_flag_field>;

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
