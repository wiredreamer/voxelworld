export module vw.gfx:renderer.settings;

import std;

import vw.core;
import vw.world;

export namespace vw::gfx {

enum class render_mode : uint8 { lit, wireframe };

inline constexpr std::array<std::string_view, 2> render_mode_names{"lit", "wireframe"};

inline constexpr uint32 msaa_sample_count = 4;

enum class quality_tier : uint8 { low, medium, high };

inline constexpr std::array<std::string_view, 3> quality_tier_names{"low", "medium", "high"};

inline constexpr quality_tier default_quality = quality_tier::medium;

// см. docs/light-plan.md#пресеты-качества
struct quality_preset {
    uint32 view_distance_columns;
    uint32 lod_base_chunks;
    uint32 msaa_samples;
    int32 grass_radius_columns;
    bool bloom;
};

inline constexpr std::array<quality_preset, quality_tier_names.size()> quality_presets{{
    {
        .view_distance_columns = 6,
        .lod_base_chunks       = 2,
        .msaa_samples          = 2,
        .grass_radius_columns  = 1,
        .bloom                 = false,
    },
    {
        .view_distance_columns = ecs::default_view_distance,
        .lod_base_chunks       = ecs::default_lod_base_chunks,
        .msaa_samples          = msaa_sample_count,
        .grass_radius_columns  = 2,
        .bloom                 = true,
    },
    {
        .view_distance_columns = 16,
        .lod_base_chunks       = 6,
        .msaa_samples          = 4,
        .grass_radius_columns  = 2,
        .bloom                 = true,
    },
}};

[[nodiscard]] constexpr auto preset_of(quality_tier tier) -> const quality_preset& {
    return quality_presets[static_cast<std::size_t>(tier)];
}

[[nodiscard]] constexpr auto name_of(quality_tier tier) -> std::string_view {
    return quality_tier_names[static_cast<std::size_t>(tier)];
}

[[nodiscard]] constexpr auto find_quality(std::string_view name) -> std::optional<quality_tier> {
    for (std::size_t tier = 0; tier < quality_tier_names.size(); ++tier) {
        if (quality_tier_names[tier] == name) {
            return static_cast<quality_tier>(tier);
        }
    }
    return std::nullopt;
}

struct directional_light_settings {
    vec3f direction{0.0f, -1.0f, 0.0f};
    vec3f color{1.f, 1.f, 1.f};
    float32 intensity{1.0f};

    float32 wrap{0.5f};
};

struct ambient_settings {
    vec3f sky{0.34f, 0.42f, 0.52f};
    vec3f ground{0.16f, 0.14f, 0.13f};
    float32 strength{1.15f};

    float32 ao_strength = 0.65f;
    float32 ao_curve    = 1.0f;

    float32 convex_strength = 0.35f;
    float32 convex_curve    = 2.0f;

    vec3f cave{0.05f, 0.055f, 0.07f};

    float32 sky_curve = 1.0f;

    float32 sun_curve = 2.0f;
};

struct tonemap_settings {
    float32 exposure = 1.3f;

    float32 white_point = 1.75f;
};

// см. docs/rendering.md#кадр-в-hdr
[[nodiscard]] inline auto display_from_scene(const vec3f& scene, const tonemap_settings& tonemap)
    -> vec3f {
    const float32 white_squared = tonemap.white_point * tonemap.white_point;

    const auto channel = [&](float32 value) -> float32 {
        const float32 exposed = value * tonemap.exposure;
        return exposed * (1.0f + (exposed / white_squared)) / (1.0f + exposed);
    };

    return {channel(scene.x), channel(scene.y), channel(scene.z)};
}

[[nodiscard]] inline auto scene_from_display(const vec3f& display, const tonemap_settings& tonemap)
    -> vec3f {
    const float32 white_squared = tonemap.white_point * tonemap.white_point;

    const auto channel = [&](float32 value) -> float32 {
        const float32 gap     = 1.0f - value;
        const float32 exposed =
            0.5f * white_squared * (std::sqrt((gap * gap) + (4.0f * value / white_squared)) - gap);
        return exposed / tonemap.exposure;
    };

    return {channel(display.x), channel(display.y), channel(display.z)};
}

struct bloom_settings {
    bool enabled      = true;
    float32 intensity = 0.6f;
};

enum class debug_view : uint32 {
    off = 0,
    ambient_occlusion,
    normals,
    sky_light,
    convexity,
    block_light,
    blob_shadow,

    light_complexity,

    blob_complexity,
};

inline constexpr std::array<std::string_view, 9> debug_view_names{
    "off",         "ambient occlusion", "normals",
    "sky light",   "convexity",         "block light",
    "blob shadow", "light complexity",  "blob complexity",
};

static_assert(
    debug_view_names.size() == static_cast<std::size_t>(debug_view::blob_complexity) + 1
);

struct block_light_settings {
    vec3f color{1.0f, 0.86f, 0.62f};
    float32 intensity{1.0f};

    float32 curve{2.0f};

    float32 glow{1.0f};
};

struct cluster_settings {
    uint32 tile_size = 32;
    uint32 slices    = 24;
    uint32 cap       = 32;

    uint32 blob_cap = 16;
    bool enabled    = true;
};

struct fog_settings {
    vec3f color{0.1f, 0.1f, 0.1f};
    float32 near_distance{256.0f};
    float32 far_distance{512.0f};
    bool enabled{true};
};

struct grass_settings {
    bool enabled         = true;
    int32 radius_columns = 2;
    float32 fade_share   = 0.7F;
};

// см. docs/rendering.md#ветер
struct wind_settings {
    vec2f direction          = {0.8F, 0.6F};
    float32 speed            = 1.6F;
    float32 grass_bend       = 1.5F;
    float32 leaf_sway_voxels = 0.12F;
};

}  // namespace vw::gfx
