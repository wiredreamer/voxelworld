#version 450

#include "tonemap.glsl"

layout(push_constant) uniform DebugPush {
    vec4 tonemap;
} push;

layout(location = 0) in vec4 inColor;

layout(location = 0) out vec4 outColor;

void main() {
    outColor = vec4(sceneFromDisplay(inColor.rgb, push.tonemap.xy), inColor.a);
}
