#version 450

layout(binding = 0) uniform UniformBufferObject {
    mat4 view;
    mat4 proj;
} ubo;

layout(location = 0) in vec3 inPosition;
layout(location = 1) in uint inColor;

layout(location = 0) out vec4 outColor;

vec4 unpackColor(uint packedColor) {
    float r = float((packedColor >> 24) & uint(0xFF)) / 255.0;
    float g = float((packedColor >> 16) & uint(0xFF)) / 255.0;
    float b = float((packedColor >> 8) & uint(0xFF)) / 255.0;
    float a = float(packedColor & uint(0xFF)) / 255.0;
    return vec4(r, g, b, a);
}

void main() {
    gl_Position = ubo.proj * ubo.view * vec4(inPosition, 1.0);
    outColor = unpackColor(inColor);
}