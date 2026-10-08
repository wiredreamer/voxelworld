#version 450

#include "tonemap.glsl"

layout(set = 0, binding = 0) uniform sampler2D scene;
layout(set = 1, binding = 0) uniform sampler2D bloom;

layout(push_constant) uniform PostPush {
    vec4 params;
} push;

layout(location = 0) in vec2 fragUV;

layout(location = 0) out vec4 outColor;

// см. docs/rendering.md#приборы-кадра
vec3 heat(float share) {
    const vec3 steps[6] = vec3[6](
        vec3(0.0, 0.0, 0.0), vec3(0.05, 0.1, 0.55), vec3(0.0, 0.65, 0.7), vec3(0.35, 0.8, 0.1),
        vec3(1.0, 0.8, 0.0), vec3(1.0, 0.1, 0.05)
    );

    float at = clamp(share, 0.0, 1.0) * 5.0;
    int low  = min(int(at), 4);
    return mix(steps[low], steps[low + 1], at - float(low));
}

void main() {
    vec3 color = texture(scene, fragUV).rgb;

    if (push.params.w > 0.0) {
        outColor = vec4(color.r > push.params.w ? vec3(1.0) : heat(color.r / push.params.w), 1.0);
        return;
    }

    if (push.params.z > 0.0) {
        color += texture(bloom, fragUV).rgb * push.params.z;
    }

    outColor = vec4(displayFromScene(color, push.params.xy), 1.0);
}
