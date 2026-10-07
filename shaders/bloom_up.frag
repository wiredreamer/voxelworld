#version 450

layout(set = 0, binding = 0) uniform sampler2D source;

layout(push_constant) uniform PostPush {
    vec4 params;
} push;

layout(location = 0) in vec2 fragUV;

layout(location = 0) out vec4 outColor;

void main() {
    vec2 half_texel = push.params.xy * 0.5;

    vec3 sum = texture(source, fragUV + vec2(-half_texel.x * 2.0, 0.0)).rgb;
    sum += texture(source, fragUV + vec2(half_texel.x * 2.0, 0.0)).rgb;
    sum += texture(source, fragUV + vec2(0.0, -half_texel.y * 2.0)).rgb;
    sum += texture(source, fragUV + vec2(0.0, half_texel.y * 2.0)).rgb;
    sum += texture(source, fragUV + vec2(-half_texel.x, half_texel.y)).rgb * 2.0;
    sum += texture(source, fragUV + vec2(half_texel.x, half_texel.y)).rgb * 2.0;
    sum += texture(source, fragUV + vec2(-half_texel.x, -half_texel.y)).rgb * 2.0;
    sum += texture(source, fragUV + vec2(half_texel.x, -half_texel.y)).rgb * 2.0;

    outColor = vec4(sum / 12.0, 1.0);
}
