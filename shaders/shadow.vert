#version 460 core

const int SHADOW_CASCADES = 5;

layout(location = 2) in uint inInstanceIndex;

layout(set = 0, binding = 0) uniform ShadowUniformBufferObject {
    mat4 light_space_matrices[SHADOW_CASCADES];
} shadowUbo;

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

layout(push_constant) uniform ShadowPushConstants {
    uint cascadeIndex;
} pushConstants;

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

void main() {
    Quad q = quads[uint(gl_VertexIndex) / 4u];
    uint corner_id = uint(gl_VertexIndex) % 4u;

    uvec3 mn = uvec3(q.data0 & 0x7Fu, (q.data0 >> 7) & 0x7Fu, (q.data0 >> 14) & 0x7Fu);

    uint normal_id = (q.data0 >> 21) & 0x7u;
    uvec3 mx       = unpackMax(q.data1, mn, normal_id);
    uvec3 pick     = FACE_VERTS[normal_id][corner_id];

    mat4 model = modelMatrices.models[inInstanceIndex];

    vec3 localPos = vec3(mix(mn, mx, bvec3(pick)));
    vec4 worldPos = model * vec4(localPos, 1.0);

    mat4 lightSpaceMatrix = shadowUbo.light_space_matrices[pushConstants.cascadeIndex];
    gl_Position = lightSpaceMatrix * worldPos;
}
