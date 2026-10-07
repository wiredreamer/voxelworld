layout(set = 1, binding = 0, rgba8) uniform image3D cascadeNear;
layout(set = 1, binding = 1, rgba8) uniform image3D cascadeMid;
layout(set = 1, binding = 2, rgba8) uniform image3D cascadeFar;

layout(push_constant) uniform LightPush {
    ivec4 base_chunk;
    ivec4 window[3];
} push;

const int LAST_CASCADE = 2;
const int CASCADE_BITS = 7;

const int CELL_SHIFT[3] = int[3](0, 2, 3);
const int SIDE_MASK[3]  = int[3](255, 127, 127);

const float FULL_LEVEL = 15.0;

vec4 readCascade(int cascade, ivec3 texel) {
    if (cascade == 0) {
        return imageLoad(cascadeNear, texel);
    }
    if (cascade == 1) {
        return imageLoad(cascadeMid, texel);
    }
    return imageLoad(cascadeFar, texel);
}

void writeCascade(int cascade, ivec3 texel, vec4 light) {
    if (cascade == 0) {
        imageStore(cascadeNear, texel, light);
    } else if (cascade == 1) {
        imageStore(cascadeMid, texel, light);
    } else {
        imageStore(cascadeFar, texel, light);
    }
}
