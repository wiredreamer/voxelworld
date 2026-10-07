#version 450

#define OCCUPANCY_SET 0

#include "occupancy.glsl"
#include "tonemap.glsl"

layout(push_constant) uniform OccupancyViewPush {
    vec4 eye;
    vec4 corners[4];
    vec4 tonemap;
    ivec4 base_chunk;
} push;

layout(location = 0) in vec2 fragUV;

layout(location = 0) out vec4 outColor;

const vec3 LEVEL_TINT[3] = vec3[3](
    vec3(0.92, 0.90, 0.84),
    vec3(0.62, 0.86, 0.66),
    vec3(0.58, 0.72, 0.94)
);

const vec3 AXIS_SHADE = vec3(0.72, 1.0, 0.86);

void main() {
    vec3 top    = mix(push.corners[0].xyz, push.corners[1].xyz, fragUV.x);
    vec3 bottom = mix(push.corners[3].xyz, push.corners[2].xyz, fragUV.x);
    vec3 ray    = normalize(mix(top, bottom, fragUV.y));

    OccupancyHit hit = marchOccupancy(push.base_chunk.xyz, push.eye.xyz, ray, push.eye.w, 0);

    vec3 shown = vec3(0.40, 0.60, 0.90);
    if (hit.found) {
        float shade = hit.entryAxis < 0 ? 0.3 : AXIS_SHADE[hit.entryAxis];
        float haze  = clamp(hit.distance / push.eye.w, 0.0, 1.0);
        shown       = mix(LEVEL_TINT[hit.level] * shade, shown, haze * haze);
    }

    outColor = vec4(sceneFromDisplay(shown, push.tonemap.xy), 0.0);
}
