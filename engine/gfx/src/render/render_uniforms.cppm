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

struct uniform_buffer_object {
    alignas(16) float32 view[16]{};
    alignas(16) float32 projection[16]{};
    alignas(16) vec3f view_pos;

    alignas(16) directional_light_data directional_light;

    alignas(16) vec4f ambient_sky;
    alignas(16) vec4f ambient_ground;

    // x: насколько проседает полностью закрытый угол, y: кривая затенения;
    // z: насколько поднимается торчащий угол, w: его кривая.
    alignas(16) vec4f ao_params;

    alignas(16) vec4f cave_ambient;

    alignas(16) vec4f sky_params;

    alignas(16) vec4f lamp_params;

    alignas(16) vec4f glow_params;

    alignas(16) vec4f tonemap_params;

    alignas(4) uint32 point_lights_count{0};

    alignas(4) uint32 debug_view{0};

    alignas(16) fog_data fog;

    alignas(4) float32 blob_strength{1.0f};

    // Фроксельная сетка; в конце блока по причине, изложенной ниже. x: z_scale,
    // y: z_bias, z: размер тайла в пикселях, w: число срезов.
    alignas(16) vec4f cluster_params{};

    // x: тайлов по горизонтали, y: по вертикали, z: предел списка одного кластера,
    // w: 1, когда фрагмент читает этот список, и 0, когда обходит все источники.
    alignas(16) vec4<uint32> cluster_dims{};

    alignas(16) vec4<uint32> blob_dims{};
};

static_assert(offsetof(uniform_buffer_object, sky_params) == 672);
static_assert(offsetof(uniform_buffer_object, lamp_params) == 688);
static_assert(offsetof(uniform_buffer_object, glow_params) == 704);
static_assert(offsetof(uniform_buffer_object, tonemap_params) == 720);
static_assert(offsetof(uniform_buffer_object, point_lights_count) == 736);
static_assert(offsetof(uniform_buffer_object, debug_view) == 740);
static_assert(offsetof(uniform_buffer_object, fog) == 752);
static_assert(offsetof(uniform_buffer_object, blob_strength) == 784);
static_assert(offsetof(uniform_buffer_object, cluster_params) == 800);
static_assert(offsetof(uniform_buffer_object, cluster_dims) == 816);
static_assert(offsetof(uniform_buffer_object, blob_dims) == 832);
static_assert(sizeof(uniform_buffer_object) == 848);

struct shadow_push_constant_data {
    alignas(4) uint32 cascade_index = 0;
};

struct shadow_uniform_buffer_object {
    alignas(16) mat4f light_space_matrices[shadow_map::cascade_count];
};

struct push_constant_data {
    alignas(16) float32 matrix[16]{};
};

}  // namespace vw::gfx
