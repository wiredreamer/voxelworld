export module vw.world:terrain.perlin;
import :terrain.generator;

import std;

import vw.core;
import vw.asset;

export namespace vw::ecs {

class perlin_terrain_generator final : public terrain_generator {
public:
    struct params {
        uint32 seed       = 42;
        int32 world_units_per_voxel = 8;

        int32 world_bottom_y = -448;

        int32 soil_depth_max      = 5;
        float32 soil_frequency    = 0.012F;
        float32 soil_slope_limit  = 1.4F;
        int32 soil_altitude_start = 55;
        int32 soil_altitude_end   = 78;
        int32 snow_line           = 82;

        int32 rock_skin     = 4;
        int32 rock_deep_y   = -64;
        int32 rock_bottom_y = -256;

        bool caves = true;

        float32 cave_field_frequency = 0.0090F;
        float32 cave_field_squash    = 0.55F;
        float32 cave_field_threshold = 0.26F;
        float32 cave_field_falloff   = 0.30F;

        float32 cave_field_surface_bias = 0.10F;
        int32 cave_field_surface_reach  = 150;

        float32 cave_cheese_frequency = 0.0135F;
        float32 cave_cheese_squash    = 2.4F;
        float32 cave_cheese_width     = 0.090F;

        float32 cave_tunnel_frequency = 0.019F;
        float32 cave_tunnel_width     = 0.045F;

        int32 cave_level_spacing    = 46;
        float32 cave_level_contrast = 0.55F;

        float32 cave_entrance_frequency = 0.0210F;
        float32 cave_entrance_threshold = 0.21F;
        float32 cave_entrance_falloff   = 0.12F;
        float32 cave_entrance_field     = 0.85F;

        int32 cave_entrance_depth  = 110;
        float32 cave_entrance_lift = 0.11F;

        int32 cave_surface_margin = 7;
        int32 cave_surface_fade   = 22;
        int32 bedrock_thickness   = 3;

        int32 cave_sample_stride = 4;

        float32 continent_frequency = 0.003F;
        float32 terrain_frequency   = 0.02F;
        int32 octaves               = 4;
        float32 lacunarity          = 2.0F;
        float32 persistence         = 0.5F;

        int32 plains_height    = 20;
        int32 hills_height     = 35;
        int32 mountains_height = 55;

        float32 ridge_frequency = 0.015F;
        float32 ridge_weight    = 0.6F;

        float32 warp_frequency = 0.01F;
        float32 warp_strength  = 30.0F;
    };

    perlin_terrain_generator(asset::model_identity_pool& identity_pool, asset::page_pool& pool);
    perlin_terrain_generator(asset::model_identity_pool& identity_pool, asset::page_pool& pool,
                             params p);

    auto generate(terrain_context& ctx) -> void override;

    [[nodiscard]] auto surface_height_at(int32 wx, int32 wz) const -> int32;

private:
    [[nodiscard]] auto noise2d(float64 x, float64 y) const -> float64;

    [[nodiscard]] auto noise3d(float64 x, float64 y, float64 z) const -> float64;

    [[nodiscard]] auto cave_field_at(int32 wx, int32 wy, int32 wz, int32 depth) const -> float32;

    [[nodiscard]] auto cave_openness_at(
        int32 wx, int32 wy, int32 wz, float32 field, int32 surface, float32 leak
    ) const -> float32;

    [[nodiscard]] auto cave_entrance_leak_at(int32 wx, int32 wz) const -> float32;

    [[nodiscard]] auto octave_noise(float64 x, float64 y) const -> float64;
    [[nodiscard]] auto ridged_noise(float64 x, float64 y) const -> float64;
    [[nodiscard]] auto continent_at(float64 nx, float64 nz) const -> float64;

    [[nodiscard]] auto stone_height_at(int32 wx, int32 wz) const -> int32;
    [[nodiscard]] auto soil_depth_at(int32 wx, int32 wz, int32 stone, float32 slope) const -> int32;

    [[nodiscard]] auto rock_voxel_at(int32 wy) const -> voxel;
    [[nodiscard]] auto voxel_at(int32 wy, int32 stone_top, int32 surface_top) const -> voxel;

    struct column_profile {
        static constexpr int32 size   = 64;
        static constexpr int32 apron  = 1;
        static constexpr int32 stride = size + (2 * apron);
        static constexpr int32 page   = 8;
        static constexpr int32 pages  = size / page;

        std::array<int32, stride * stride> stone{};
        std::array<int32, size * size> surface{};

        std::array<int32, pages * pages> page_min_stone{};
        std::array<int32, pages * pages> page_max_surface{};

        int32 min_stone   = 0;
        int32 max_surface = 0;

        int32 voxels_per_cell = 1;

        [[nodiscard]] static auto stone_index(int32 x, int32 z) -> int32 {
            return ((x + apron) * stride) + (z + apron);
        }

        [[nodiscard]] auto cell_of(int32 world_y) const -> int32 {
            return world_y >= 0 ? world_y / voxels_per_cell
                                : (world_y - voxels_per_cell + 1) / voxels_per_cell;
        }
    };

    [[nodiscard]] auto sample_column_(int32 cx, int32 cz, int32 voxels_per_cell) const
        -> column_profile;

    auto carve_caves_(asset::model_writer& writer, terrain_context& ctx, int32 chunk_y,
                      const column_profile& profile) const -> void;

    auto generate_chunk(terrain_context& ctx, int32 chunk_y, const column_profile& profile) -> void;

    static auto fade(float64 t) -> float64;
    static auto lerp(float64 t, float64 a, float64 b) -> float64;
    static auto grad(int32 hash, float64 x, float64 y) -> float64;

    asset::model_identity_pool* identity_pool_;
    asset::page_pool* page_pool_;
    params params_;
    std::array<int32, 512> perm_;
};

}  // namespace vw::ecs
