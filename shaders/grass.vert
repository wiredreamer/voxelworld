#version 460 core

layout(set = 0, binding = 0) uniform UniformBufferObject {
    mat4 view;
    mat4 proj;
} ubo;

struct GrassInstance {
    vec4 place;
    vec4 light;
};

layout(set = 1, binding = 0, std430) readonly buffer Instances {
    GrassInstance instances[];
};

struct Quad {
    uint data0;
    uint data1;
};

layout(set = 1, binding = 1, std430) readonly buffer Quads {
    Quad quads[];
};

struct PaletteEntry {
    vec4 color;
};

layout(set = 4, binding = 0, std430) readonly buffer PaletteBuffer {
    PaletteEntry palette[];
};

struct MaterialEntry {
    vec4 look;
};

layout(set = 4, binding = 1, std430) readonly buffer MaterialBuffer {
    MaterialEntry materials[];
};

layout(push_constant) uniform GrassPush {
    vec4 wind;
    vec4 eye;
    vec4 shape;
} grass;

layout(location = 0) out vec3 fragPos;
layout(location = 1) out vec3 fragNormal;
layout(location = 2) out vec3 fragColor;
layout(location = 2, component = 3) out float fragGlow;
layout(location = 3) out float viewDepth;
layout(location = 7) flat out uint fragConvexMask;
layout(location = 8) flat out vec4 fragInstanceLight;
layout(location = 9) centroid out vec3 fragGridPos;

const vec3 NORMALS[6] = vec3[6](
    vec3( 1,  0,  0),
    vec3(-1,  0,  0),
    vec3( 0,  1,  0),
    vec3( 0, -1,  0),
    vec3( 0,  0,  1),
    vec3( 0,  0, -1)
);

const uint FACE_SHIFT = 9u;

const uint TANGENT_U_AXIS[6] = uint[6](2u, 2u, 0u, 0u, 0u, 0u);
const uint TANGENT_V_AXIS[6] = uint[6](1u, 1u, 2u, 2u, 1u, 1u);

const uvec3 FACE_VERTS[6][4] = uvec3[6][4](
    uvec3[4](uvec3(1, 0, 0), uvec3(1, 0, 1), uvec3(1, 1, 1), uvec3(1, 1, 0)),
    uvec3[4](uvec3(0, 0, 0), uvec3(0, 1, 0), uvec3(0, 1, 1), uvec3(0, 0, 1)),
    uvec3[4](uvec3(0, 1, 0), uvec3(1, 1, 0), uvec3(1, 1, 1), uvec3(0, 1, 1)),
    uvec3[4](uvec3(0, 0, 0), uvec3(0, 0, 1), uvec3(1, 0, 1), uvec3(1, 0, 0)),
    uvec3[4](uvec3(0, 0, 1), uvec3(0, 1, 1), uvec3(1, 1, 1), uvec3(1, 0, 1)),
    uvec3[4](uvec3(1, 0, 0), uvec3(1, 1, 0), uvec3(0, 1, 0), uvec3(0, 0, 0))
);

// см. docs/rendering.md#трава
void main() {
    Quad q = quads[uint(gl_VertexIndex) / 4u];
    uint corner_id = uint(gl_VertexIndex) % 4u;

    uvec3 mn = uvec3(q.data0 & 0x7Fu, (q.data0 >> 7) & 0x7Fu, (q.data0 >> 14) & 0x7Fu);
    uint normal_id = (q.data0 >> 21) & 0x7u;

    uvec3 mx = mn;
    mx[normal_id >> 1u] += 1u;
    mx[TANGENT_U_AXIS[normal_id]] += (q.data1 & 0x7Fu) + 1u;
    mx[TANGENT_V_AXIS[normal_id]] += ((q.data1 >> 7u) & 0x7Fu) + 1u;

    GrassInstance inst = instances[gl_InstanceIndex];
    vec3 base = inst.place.xyz;

    float spread = distance(base.xz, grass.eye.xz);
    float fade   = 1.0 - smoothstep(grass.eye.w, grass.shape.x, spread);

    vec3 local  = vec3(mix(mn, mx, bvec3(FACE_VERTS[normal_id][corner_id])));
    fragGridPos = local;
    local.xz -= vec2(grass.shape.y);
    float rise = clamp(local.y / grass.shape.z, 0.0, 1.0);
    local *= inst.light.w * fade;

    float c = cos(inst.place.w);
    float s = sin(inst.place.w);
    vec3 turned = vec3(c * local.x + s * local.z, local.y, -s * local.x + c * local.z);

    vec2 stalk  = base.xz + turned.xz;
    float phase = (0.35 * inst.light.z) + dot(stalk, vec2(0.031, 0.023));
    float t     = grass.wind.w * grass.shape.w;
    float gust  = 0.6 * sin(t + phase) + 0.4 * sin((t * 2.3) + (phase * 1.7));
    float bend  = rise * rise * grass.wind.z * fade * (0.55 + 0.45 * gust);
    turned.xz += grass.wind.xy * bend;

    vec4 worldPos = vec4(base + turned, 1.0);
    fragPos = worldPos.xyz;

    vec3 n = NORMALS[normal_id];
    fragNormal = vec3(c * n.x + s * n.z, n.y, -s * n.x + c * n.z);
    fragInstanceLight = vec4(0.0, 0.0, inst.light.xy);

    uint palette_idx = (q.data1 >> 14) & 0xFFu;
    fragColor = palette[palette_idx].color.rgb;
    fragGlow  = materials[q.data0 >> 24].look.x;

    fragConvexMask = normal_id << FACE_SHIFT;

    viewDepth = -(ubo.view * worldPos).z;
    gl_Position = ubo.proj * ubo.view * worldPos;
}
