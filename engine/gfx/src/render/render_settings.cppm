export module vw.gfx:renderer.settings;

import std;

import vw.core;

export namespace vw::gfx {

enum class render_mode : uint8 { lit, wireframe };

inline constexpr std::array<std::string_view, 2> render_mode_names{"lit", "wireframe"};

inline constexpr uint32 msaa_sample_count = 4;

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

}  // namespace vw::gfx
