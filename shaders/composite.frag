#version 450

#include "tonemap.glsl"

layout(set = 0, binding = 0) uniform sampler2D scene;
layout(set = 1, binding = 0) uniform sampler2D bloom;

layout(push_constant) uniform PostPush {
    vec4 params;
} push;

layout(location = 0) in vec2 fragUV;

layout(location = 0) out vec4 outColor;

void main() {
    vec3 color = texture(scene, fragUV).rgb;

    if (push.params.z > 0.0) {
        color += texture(bloom, fragUV).rgb * push.params.z;
    }

    outColor = vec4(displayFromScene(color, push.params.xy), 1.0);
}
