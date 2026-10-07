#version 450

layout(set = 0, binding = 0) uniform sampler2D source;

layout(push_constant) uniform PostPush {
    vec4 params;
} push;

layout(location = 0) in vec2 fragUV;

layout(location = 0) out vec4 outColor;

const float GLOW_CEILING = 64.0;

vec3 glowOf(vec2 uv) {
    vec4 texel = texture(source, uv);
    if (push.params.z < 0.5) {
        return texel.rgb;
    }

    vec3 glow = texel.rgb * texel.a;
    if (any(isnan(glow)) || any(isinf(glow))) {
        return vec3(0.0);
    }
    return clamp(glow, 0.0, GLOW_CEILING);
}

void main() {
    vec2 half_texel = push.params.xy * 0.5;

    vec3 sum = glowOf(fragUV) * 4.0;
    sum += glowOf(fragUV + vec2(-half_texel.x, -half_texel.y));
    sum += glowOf(fragUV + vec2(half_texel.x, -half_texel.y));
    sum += glowOf(fragUV + vec2(-half_texel.x, half_texel.y));
    sum += glowOf(fragUV + vec2(half_texel.x, half_texel.y));

    outColor = vec4(sum / 8.0, 1.0);
}
