export module vw.world:terrain.perlin;
import :terrain.generator;
import :terrain.biomes;
import :terrain.noise;
import :terrain.regions;

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

        bool island                    = true;
        float32 region_spacing_voxels  = 1000.0F;
        float32 region_jitter          = 0.35F;
        float32 region_warp_voxels     = 60.0F;
        float32 region_warp_frequency  = 0.004F;
        float32 island_taper_voxels    = 128.0F;
        int32 island_min_thickness     = 6;
        float32 island_jag_voxels      = 10.0F;
        float32 island_jag_frequency   = 0.03F;

        float32 relief_warp_frequency  = 0.003F;
        float32 relief_warp_voxels     = 40.0F;
        float32 landscape_frequency    = 0.003F;

        float32 moisture_frequency     = 0.002F;
        float32 biome_blend            = 0.15F;
        float32 tone_blend             = 0.3F;

        std::vector<terrain_biome> biomes = default_biomes();
    };

    perlin_terrain_generator(asset::model_identity_pool& identity_pool, asset::page_pool& pool);
    perlin_terrain_generator(asset::model_identity_pool& identity_pool, asset::page_pool& pool,
                             params p);

    auto generate(terrain_context& ctx) -> void override;

    [[nodiscard]] auto surface_height_at(int32 wx, int32 wz) const -> std::optional<int32>;

    [[nodiscard]] auto get_regions() const -> const region_map& {
        return regions_;
    }

    [[nodiscard]] auto biome_at(int32 wx, int32 wz) const -> const terrain_biome&;
    [[nodiscard]] auto surface_voxel_at(int32 wx, int32 wz) const -> std::optional<voxel>;

private:
    struct column_shape {
        int32 surface      = 0;
        int32 bottom       = 0;
        float32 height     = 0.0F;
        uint8 biome        = 0;
        uint8 neighbour    = 0;
        float32 tone_share = 0.0F;

        [[nodiscard]] auto is_void() const -> bool {
            return bottom > surface;
        }
    };

    [[nodiscard]] auto cave_field_at(int32 wx, int32 wy, int32 wz, int32 depth) const -> float32;

    [[nodiscard]] auto cave_openness_at(
        int32 wx, int32 wy, int32 wz, float32 field, int32 surface, float32 leak
    ) const -> float32;

    [[nodiscard]] auto cave_entrance_leak_at(int32 wx, int32 wz) const -> float32;

    [[nodiscard]] auto shape_at(int32 wx, int32 wz) const -> column_shape;

    struct relief_sample {
        float64 height     = 0.0;
        uint8 biome        = 0;
        uint8 neighbour    = 0;
        float32 tone_share = 0.0F;
    };

    [[nodiscard]] auto climate_at(float64 x, float64 z) const -> biome_point;
    [[nodiscard]] auto relief_at(float64 x, float64 z) const -> relief_sample;
    [[nodiscard]] auto paint_at_(int32 wx, int32 wz, const column_shape& shape, float32 slope) const
        -> column_paint;

    [[nodiscard]] auto rock_voxel_at(int32 wy) const -> voxel;
    [[nodiscard]] auto voxel_at(int32 wy, int32 surface, const column_paint& paint) const -> voxel;

    struct column_profile {
        static constexpr int32 size   = 64;
        static constexpr int32 apron  = 1;
        static constexpr int32 stride = size + (2 * apron);
        static constexpr int32 page   = 8;
        static constexpr int32 pages  = size / page;

        std::array<int32, stride * stride> surface{};
        std::array<float32, stride * stride> height{};
        std::array<int32, size * size> bottom{};
        std::array<column_paint, size * size> paint{};

        std::array<int32, pages * pages> page_min_rock{};
        std::array<int32, pages * pages> page_max_surface{};
        std::array<int32, pages * pages> page_min_bottom{};
        std::array<int32, pages * pages> page_max_bottom{};

        int32 max_surface = 0;
        int32 min_bottom  = 0;

        int32 voxels_per_cell = 1;

        [[nodiscard]] static auto ring_index(int32 x, int32 z) -> int32 {
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

    asset::model_identity_pool* identity_pool_;
    asset::page_pool* page_pool_;
    params params_;
    std::vector<terrain_biome> biomes_;
    perlin_noise noise_;
    region_map regions_;
};

}  // namespace vw::ecs
