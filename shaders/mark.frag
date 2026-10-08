#version 450

layout(location = 0) in vec3 fragPos;
layout(location = 1) in vec3 fragNormal;
layout(location = 2) in vec3 fragColor;
layout(location = 2, component = 3) in float fragGlow;
layout(location = 3) in float viewDepth;
layout(location = 7) flat in uint fragConvexMask;
layout(location = 8) flat in vec4 fragInstanceLight;
layout(location = 9) centroid in vec3 fragGridPos;

layout(set = 0, binding = 0) uniform UniformBufferObject {
    mat4 view;
    mat4 proj;
    layout(offset = 848) vec4 occupancy_eye;
    layout(offset = 864) ivec4 occupancy_base;
} ubo;

#define OCCUPANCY_SET 5
#include "occupancy.glsl"

layout(location = 0) out vec4 outColor;

const float CLEARANCE = 3.0;
const float REACH     = 420.0;

// см. docs/rendering.md#отсев-по-заслонам
void main() {
    if (fragInstanceLight.z <= 0.5) {
        vec3 eye   = ubo.occupancy_eye.xyz;
        vec3 to    = fragGridPos - eye;
        float away = length(to);

        OccupancyHit hit = marchOccupancy(
            ubo.occupancy_base.xyz, eye, to / away, min(away - CLEARANCE, REACH), 0
        );
        if (hit.found && hit.level == 0) {
            discard;
        }
    }

    float lit = 0.75 + (0.25 * abs(fragNormal.y)) + (0.0 * (fragPos.x + fragColor.x + fragGlow + viewDepth + float(fragConvexMask)));
    outColor  = vec4(4.0 * lit, 0.0, 4.0 * lit, 1.0);
}
