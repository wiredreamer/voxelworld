#version 460 core

layout(location = 2) in uint inInstanceIndex;

const int SHADOW_CASCADES = 5;

// Only view and proj are read here, but a member missing from the middle of the
// block shifts every offset after it, so the whole thing has to match.
struct DirectionalLightData {
    mat4 light_space_matrices[SHADOW_CASCADES];
    vec4 cascades[SHADOW_CASCADES];
    vec4 shadow_filter;
    vec3 direction;
    vec3 color;
    float intensity;
    float wrap;
};

layout(set = 0, binding = 0) uniform UniformBufferObject {
    mat4 view;
    mat4 proj;
    vec3 viewPos;
    DirectionalLightData directional_light;
    vec4 ambient_sky;
    vec4 ambient_ground;
    vec4 ao_params;
    uint point_lights_count;
} ubo;

layout(set = 1, binding = 0, std430) readonly buffer ModelMatrices {
    mat4 models[];
} modelMatrices;

layout(set = 1, binding = 1, std430) readonly buffer NormalMatrices {
    mat4 normals[];
} normalMatrices;

// One record per greedy rectangle; six vertices are unrolled out of each.
// Packing mirrors gfx::quad::pack.
struct Quad {
    uint data0;
    uint data1;
    uint data2;
};

layout(set = 1, binding = 2, std430) readonly buffer Quads {
    Quad quads[];
};

// Albedo, already decoded: gfx::palette_buffer runs the palette through its
// gamma once on the way in rather than making every vertex do it. How much
// decoding is a setting, not a constant -- see the note there.
//
// Indexed by the block's palette slot, not by its id: an id is a category and
// an index within it, sixteen bits, and the quad has ten. The registry hands
// every live block a dense slot and the quad carries that.
layout(set = 4, binding = 0, std430) readonly buffer PaletteBuffer {
    vec4 palette[];
};

layout(location = 0) out vec3 fragPos;
layout(location = 1) out vec3 fragNormal;
// Alpha is how brightly the block draws itself; see gfx::palette_buffer.
// A vec4 and not a vec3 plus a float: a location is vec4-sized either way,
// so the fourth component rides along for nothing.
layout(location = 2) out vec4 fragColor;
layout(location = 3) out float viewDepth;
layout(location = 4) out vec2 fragUV;
layout(location = 5) flat out uint fragCornersMask;
layout(location = 6) flat out uint fragLightMask;
layout(location = 7) flat out uint fragConvexMask;

const vec3 NORMALS[6] = vec3[6](
    vec3( 1,  0,  0),
    vec3(-1,  0,  0),
    vec3( 0,  1,  0),
    vec3( 0, -1,  0),
    vec3( 0,  0,  1),
    vec3( 0,  0, -1)
);

// Which world axis each face's two tangents run along. gfx::quad::pack carries
// the same two tables and packs the extents in this order.
const uint TANGENT_U_AXIS[6] = uint[6](2u, 2u, 0u, 0u, 0u, 0u);
const uint TANGENT_V_AXIS[6] = uint[6](1u, 1u, 2u, 2u, 1u, 1u);

// data1 keeps the two tangent extents, one less than the cell count, instead of
// the far corner: along the face axis the far corner is always the near one
// plus a cell, so storing it said nothing the normal had not already said.
uvec3 unpackMax(uint data1, uvec3 mn, uint normal_id) {
    uvec3 mx = mn;
    mx[normal_id >> 1u] += 1u;
    mx[TANGENT_U_AXIS[normal_id]] += (data1 & 0x7Fu) + 1u;
    mx[TANGENT_V_AXIS[normal_id]] += ((data1 >> 7u) & 0x7Fu) + 1u;
    return mx;
}

// Which corner of the rectangle each of the six vertices takes, and which end
// of the box each corner takes its components from. Both tables are the ones
// detail::add_quad used to bake into the vertex buffer.
const uvec3 FACE_VERTS[6][4] = uvec3[6][4](
    uvec3[4](uvec3(1, 0, 0), uvec3(1, 0, 1), uvec3(1, 1, 1), uvec3(1, 1, 0)),  // +X
    uvec3[4](uvec3(0, 0, 0), uvec3(0, 1, 0), uvec3(0, 1, 1), uvec3(0, 0, 1)),  // -X
    uvec3[4](uvec3(0, 1, 0), uvec3(1, 1, 0), uvec3(1, 1, 1), uvec3(0, 1, 1)),  // +Y
    uvec3[4](uvec3(0, 0, 0), uvec3(0, 0, 1), uvec3(1, 0, 1), uvec3(1, 0, 0)),  // -Y
    uvec3[4](uvec3(0, 0, 1), uvec3(0, 1, 1), uvec3(1, 1, 1), uvec3(1, 0, 1)),  // +Z
    uvec3[4](uvec3(1, 0, 0), uvec3(1, 1, 0), uvec3(0, 1, 0), uvec3(0, 0, 0))   // -Z
);

void main() {
    Quad q = quads[uint(gl_VertexIndex) / 4u];
    uint corner_id = uint(gl_VertexIndex) % 4u;

    uvec3 mn = uvec3(q.data0 & 0x7Fu, (q.data0 >> 7) & 0x7Fu, (q.data0 >> 14) & 0x7Fu);

    uint normal_id      = (q.data0 >> 21) & 0x7u;
    uint corners_ao     = (q.data0 >> 24) & 0xFFu;
    uint palette_idx    = (q.data1 >> 14) & 0x3FFu;
    uint corners_convex = (q.data1 >> 24) & 0xFFu;

    uvec3 mx = unpackMax(q.data1, mn, normal_id);

    uvec3 pick = FACE_VERTS[normal_id][corner_id];

    mat4 model = modelMatrices.models[inInstanceIndex];

    vec3 localPos = vec3(mix(mn, mx, bvec3(pick)));
    vec4 worldPos = model * vec4(localPos, 1.0);
    fragPos = worldPos.xyz;

    fragNormal = normalize(mat3(normalMatrices.normals[inInstanceIndex]) * NORMALS[normal_id]);

    fragColor = palette[palette_idx];

    vec2 corner_uvs[4] = vec2[4](
        vec2(0.0, 0.0),
        vec2(1.0, 0.0),
        vec2(1.0, 1.0),
        vec2(0.0, 1.0)
    );
    fragUV = corner_uvs[corner_id];
    fragCornersMask = corners_ao;
    // The whole word, both channels, unpacked in the fragment. Passing them
    // as two interpolants would cost one more for nothing: data2 is sky in the
    // low half and block light in the high one, and neither is interpolated.
    fragLightMask = q.data2;
    fragConvexMask = corners_convex;

    viewDepth = -(ubo.view * worldPos).z;

    gl_Position = ubo.proj * ubo.view * worldPos;
}
