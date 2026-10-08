#ifndef LEAF_SWAY_GLSL
#define LEAF_SWAY_GLSL

const float SWAY_LATTICE = 8.0;

vec3 windAt(vec3 worldPos, vec4 wind) {
    float phase   = dot(worldPos.xz, vec2(0.031, 0.023));
    float t       = wind.w;
    float gust    = 0.6 * sin(t + phase) + 0.4 * sin((t * 2.3) + (phase * 1.7));
    float flutter = sin((t * 3.7) + dot(worldPos, vec3(0.037, 0.029, 0.023)));
    float lean    = 0.3 + 0.7 * gust;
    return vec3(wind.x * lean, 0.35 * flutter, wind.y * lean);
}

// см. docs/rendering.md#качание-листвы
vec3 leafSway(vec3 localPos, mat4 model, vec4 wind) {
    vec3 cell  = floor(localPos / SWAY_LATTICE) * SWAY_LATTICE;
    vec3 share = (localPos - cell) / SWAY_LATTICE;

    vec3 along_x[4];
    for (int i = 0; i < 4; ++i) {
        vec3 node  = cell + vec3(0.0, float(i & 1), float(i >> 1)) * SWAY_LATTICE;
        vec3 near  = windAt((model * vec4(node, 1.0)).xyz, wind);
        vec3 far   = windAt((model * vec4(node + vec3(SWAY_LATTICE, 0.0, 0.0), 1.0)).xyz, wind);
        along_x[i] = mix(near, far, share.x);
    }

    vec3 offset = mix(mix(along_x[0], along_x[1], share.y), mix(along_x[2], along_x[3], share.y), share.z);
    return offset * (wind.z * length(model[0].xyz));
}

#endif
