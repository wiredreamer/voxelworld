#version 460 core

layout(location = 2) in uint inInstanceIndex;

layout(set = 0, binding = 0) uniform UniformBufferObject {
    mat4 view;
    mat4 proj;
} ubo;

layout(set = 1, binding = 0, std430) readonly buffer ModelMatrices {
    mat4 models[];
} modelMatrices;

layout(set = 1, binding = 1, std430) readonly buffer NormalMatrices {
    mat4 normals[];
} normalMatrices;

struct Quad {
    uint data0;
    uint data1;
    uint data2;
};

layout(set = 1, binding = 2, std430) readonly buffer Quads {
    Quad quads[];
};

struct PaletteEntry {
    vec3 color;
    float glow;
};

layout(set = 4, binding = 0, std430) readonly buffer PaletteBuffer {
    PaletteEntry palette[];
};

layout(push_constant) uniform WorldPush {
    vec4 wind;
    vec4 grid;
} world;

layout(location = 0) out vec3 fragPos;
layout(location = 1) out vec3 fragNormal;
layout(location = 2) out vec3 fragColor;
layout(location = 2, component = 3) out float fragGlow;
layout(location = 3) out float viewDepth;
layout(location = 4) centroid out vec2 fragUV;
layout(location = 5) flat out uint fragCornersMask;
layout(location = 6) flat out uint fragLightMask;
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

const uint FLAT_ONLY  = 1u << 8u;
const uint FACE_SHIFT = 9u;

const uint TANGENT_U_AXIS[6] = uint[6](2u, 2u, 0u, 0u, 0u, 0u);
const uint TANGENT_V_AXIS[6] = uint[6](1u, 1u, 2u, 2u, 1u, 1u);

uvec3 unpackMax(uint data1, uvec3 mn, uint normal_id) {
    uvec3 mx = mn;
    mx[normal_id >> 1u] += 1u;
    mx[TANGENT_U_AXIS[normal_id]] += (data1 & 0x7Fu) + 1u;
    mx[TANGENT_V_AXIS[normal_id]] += ((data1 >> 7u) & 0x7Fu) + 1u;
    return mx;
}

const uvec3 FACE_VERTS[6][4] = uvec3[6][4](
    uvec3[4](uvec3(1, 0, 0), uvec3(1, 0, 1), uvec3(1, 1, 1), uvec3(1, 1, 0)),
    uvec3[4](uvec3(0, 0, 0), uvec3(0, 1, 0), uvec3(0, 1, 1), uvec3(0, 0, 1)),
    uvec3[4](uvec3(0, 1, 0), uvec3(1, 1, 0), uvec3(1, 1, 1), uvec3(0, 1, 1)),
    uvec3[4](uvec3(0, 0, 0), uvec3(0, 0, 1), uvec3(1, 0, 1), uvec3(1, 0, 0)),
    uvec3[4](uvec3(0, 0, 1), uvec3(0, 1, 1), uvec3(1, 1, 1), uvec3(1, 0, 1)),
    uvec3[4](uvec3(1, 0, 0), uvec3(1, 1, 0), uvec3(0, 1, 0), uvec3(0, 0, 0))
);

const uint SWAY_FLAG = 1u << 22u;

// см. docs/rendering.md#качание-листвы
vec3 leafSway(vec3 worldPos, vec4 wind, float unitsPerVoxel, float weight) {
    float phase   = dot(worldPos.xz, vec2(0.031, 0.023));
    float t       = wind.w;
    float gust    = 0.6 * sin(t + phase) + 0.4 * sin((t * 2.3) + (phase * 1.7));
    float flutter = sin((t * 3.7) + dot(worldPos, vec3(0.037, 0.029, 0.023)));
    float lean    = 0.3 + 0.7 * gust;
    vec3 offset   = vec3(wind.x * lean, 0.35 * flutter, wind.y * lean);
    return offset * (wind.z * unitsPerVoxel * weight);
}

void main() {
    Quad q = quads[uint(gl_VertexIndex) / 4u];
    uint corner_id = uint(gl_VertexIndex) % 4u;

    uvec3 mn = uvec3(q.data0 & 0x7Fu, (q.data0 >> 7) & 0x7Fu, (q.data0 >> 14) & 0x7Fu);

    uint normal_id      = (q.data0 >> 21) & 0x7u;
    uint corners_ao     = (q.data0 >> 24) & 0xFFu;
    uint palette_idx    = (q.data1 >> 14) & 0xFFu;
    bool sways          = (q.data1 & SWAY_FLAG) != 0u;
    uint corners_shape  = (q.data1 >> 24) & 0xFFu;
    uint corners_convex = sways ? 0u : corners_shape;

    uvec3 mx = unpackMax(q.data1, mn, normal_id);

    uvec3 pick = FACE_VERTS[normal_id][corner_id];

    mat4 model = modelMatrices.models[inInstanceIndex];

    vec3 localPos = vec3(mix(mn, mx, bvec3(pick)));
    vec4 worldPos = model * vec4(localPos, 1.0);

    mat4 normalMatrix = normalMatrices.normals[inInstanceIndex];
    fragGridPos = normalMatrix[3].z > 0.5 ? localPos
                                          : (worldPos.xyz - world.grid.xyz) * world.grid.w;
    if (sways) {
        float weight = float((corners_shape >> (corner_id * 2u)) & 0x3u) / 3.0;
        worldPos.xyz += leafSway(worldPos.xyz, world.wind, length(model[0].xyz), weight);
    }
    fragPos = worldPos.xyz;

    fragNormal = normalize(mat3(normalMatrix) * NORMALS[normal_id]);
    fragInstanceLight = normalMatrix[3];

    fragColor = palette[palette_idx].color;
    fragGlow  = palette[palette_idx].glow;

    vec2 corner_uvs[4] = vec2[4](
        vec2(0.0, 0.0),
        vec2(1.0, 0.0),
        vec2(1.0, 1.0),
        vec2(0.0, 1.0)
    );
    fragUV = corner_uvs[corner_id];
    fragCornersMask = corners_ao;
    fragLightMask = q.data2;
    fragConvexMask = corners_convex | (sways ? FLAT_ONLY : 0u) | (normal_id << FACE_SHIFT);

    viewDepth = -(ubo.view * worldPos).z;

    gl_Position = ubo.proj * ubo.view * worldPos;
}
