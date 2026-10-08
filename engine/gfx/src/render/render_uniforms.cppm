module;

#include <cstddef>

export module vw.gfx:renderer.uniforms;

import std;

import vw.core;
import :render.shadow_map;
import :renderer.settings;

export namespace vw::gfx {

struct directional_light_data {
    alignas(16) mat4f light_space_matrices[shadow_map::cascade_count];

    alignas(16) vec4f cascades[shadow_map::cascade_count];

    alignas(16) vec4f shadow_filter;

    alignas(16) vec3f direction;
    alignas(16) vec3f color;
    alignas(4) float32 intensity;

    alignas(4) float32 wrap;
};

struct fog_data {
    alignas(16) vec3f color;
    alignas(4) float32 near_distance;
    alignas(4) float32 far_distance;
    alignas(4) uint32 enabled;
};

struct corner_shading_data {
    alignas(4) float32 ao_strength;
    alignas(4) float32 ao_curve;
    alignas(4) float32 convex_strength;
    alignas(4) float32 convex_curve;
};

struct cluster_data {
    alignas(4) float32 z_scale;
    alignas(4) float32 z_bias;
    alignas(4) float32 tile_size;
    alignas(4) float32 slices;

    alignas(4) uint32 tiles_x;
    alignas(4) uint32 tiles_y;
    alignas(4) uint32 cap;
    alignas(4) uint32 enabled;
};

static_assert(sizeof(corner_shading_data) == 16);
static_assert(offsetof(cluster_data, tiles_x) == 16);
static_assert(sizeof(cluster_data) == 32);

struct uniform_buffer_object {
    alignas(16) float32 view[16]{};
    alignas(16) float32 projection[16]{};
    alignas(16) vec3f view_pos;

    alignas(16) directional_light_data directional_light;

    alignas(16) vec4f ambient_sky;
    alignas(16) vec4f ambient_ground;

    alignas(16) corner_shading_data corner_shading;

    alignas(16) vec4f cave_ambient;

    alignas(16) vec4f sky_params;

    alignas(16) vec4f lamp_params;

    alignas(16) vec4f glow_params;

    alignas(16) vec4f tonemap_params;

    alignas(4) uint32 point_lights_count{0};

    alignas(4) uint32 debug_view{0};

    alignas(16) fog_data fog;

    alignas(4) float32 blob_strength{1.0f};

    alignas(16) cluster_data clusters{};

    alignas(16) vec4<uint32> blob_dims{};

    alignas(16) vec4f occupancy_eye;

    alignas(16) vec4<int32> occupancy_base{};

    alignas(16) vec4f light_grid;

    alignas(16) vec4f light_wrap[4];

    alignas(16) vec4<uint32> shading_skips{};
};

static_assert(offsetof(uniform_buffer_object, corner_shading) == 640);
static_assert(offsetof(uniform_buffer_object, cave_ambient) == 656);
static_assert(offsetof(uniform_buffer_object, sky_params) == 672);
static_assert(offsetof(uniform_buffer_object, lamp_params) == 688);
static_assert(offsetof(uniform_buffer_object, glow_params) == 704);
static_assert(offsetof(uniform_buffer_object, tonemap_params) == 720);
static_assert(offsetof(uniform_buffer_object, point_lights_count) == 736);
static_assert(offsetof(uniform_buffer_object, debug_view) == 740);
static_assert(offsetof(uniform_buffer_object, fog) == 752);
static_assert(offsetof(uniform_buffer_object, blob_strength) == 784);
static_assert(offsetof(uniform_buffer_object, clusters) == 800);
static_assert(offsetof(uniform_buffer_object, blob_dims) == 832);
static_assert(offsetof(uniform_buffer_object, occupancy_eye) == 848);
static_assert(offsetof(uniform_buffer_object, occupancy_base) == 864);
static_assert(offsetof(uniform_buffer_object, light_grid) == 880);
static_assert(offsetof(uniform_buffer_object, light_wrap) == 896);
static_assert(offsetof(uniform_buffer_object, shading_skips) == 960);
static_assert(sizeof(uniform_buffer_object) == 976);

struct shadow_push_constant_data {
    alignas(16) vec4f wind;
    alignas(4) uint32 cascade_index = 0;
};

static_assert(offsetof(shadow_push_constant_data, cascade_index) == 16);
static_assert(sizeof(shadow_push_constant_data) == 32);

// см. docs/rendering.md#ветер
struct world_push_constant_data {
    alignas(16) vec4f wind;
    alignas(16) vec4f grid;
};

static_assert(offsetof(world_push_constant_data, grid) == 16);
static_assert(sizeof(world_push_constant_data) == 32);

struct shadow_uniform_buffer_object {
    alignas(16) mat4f light_space_matrices[shadow_map::cascade_count];
};

struct push_constant_data {
    alignas(16) float32 matrix[16]{};
};

}  // namespace vw::gfx
