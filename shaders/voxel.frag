#version 460 core

layout(location = 0) in vec3 fragPos;
layout(location = 1) in vec3 fragNormal;
layout(location = 2) in vec3 fragColor;
layout(location = 2, component = 3) in float fragGlow;
layout(location = 3) in float viewDepth;
layout(location = 7) flat in uint fragConvexMask;
layout(location = 8) flat in vec4 fragInstanceLight;
layout(location = 9) centroid in vec3 fragGridPos;

#define SHADOW_ENABLED 0

#include "tonemap.glsl"

const int SHADOW_CASCADES = 5;

#if SHADOW_ENABLED
const float SHADOW_SLOPE_LIMIT = 3.0;

const float SHADOW_CASCADE_BLEND = 0.7;
#endif

struct DirectionalLightData {
    mat4 light_space_matrices[SHADOW_CASCADES];

    vec4 cascades[SHADOW_CASCADES];

    vec4 shadow_filter;

    vec3 direction;
    vec3 color;
    float intensity;

    float wrap;
};

struct BlobData {
    vec4 position_radius;
    vec4 params;
    vec4 cull_a;
    vec4 cull_b;
};

struct FogData {
    vec3 color;
    float near_distance;
    float far_distance;
    uint enabled;
};

struct CornerShadingData {
    float ao_strength;
    float ao_curve;
    float convex_strength;
    float convex_curve;
};

struct ClusterData {
    float z_scale;
    float z_bias;
    float tile_size;
    float slices;

    uint tiles_x;
    uint tiles_y;
    uint cap;
    uint enabled;
};

layout(set = 0, binding = 0) uniform UniformBufferObject {
    mat4 view;
    mat4 proj;
    vec3 viewPos;
    DirectionalLightData directional_light;
    vec4 ambient_sky;
    vec4 ambient_ground;

    CornerShadingData corner_shading;

    vec4 cave_ambient;

    vec4 sky_params;

    vec4 lamp_params;

    vec4 glow_params;

    vec4 tonemap_params;

    uint point_lights_count;

    uint debug_view;

    FogData fog;

    float blob_strength;

    ClusterData clusters;

    uvec4 blob_dims;

    vec4 occupancy_eye;

    ivec4 occupancy_base;

    vec4 light_grid;

    vec4 light_wrap[4];
} ubo;

layout(set = 6, binding = 0) uniform sampler3D lightCascades[3];
layout(set = 6, binding = 1) uniform sampler3D lightTints[3];

const int LIGHT_LAST_CASCADE  = 2;
const float LIGHT_CELL[3]     = float[3](1.0, 4.0, 8.0);
const float LIGHT_SPAN[3]     = float[3](256.0, 512.0, 1024.0);
const float LIGHT_EDGE[3]     = float[3](96.0, 192.0, 480.0);
const float LIGHT_EDGE_BLEND  = 0.8;

vec4 cascadeLight(int cascade, vec3 fromBase, vec3 normal) {
    vec3 at = ((fromBase + (normal * (0.5 * LIGHT_CELL[cascade]))) / LIGHT_SPAN[cascade]) +
              ubo.light_wrap[cascade].xyz;

    if (cascade == 0) {
        return texture(lightCascades[0], at);
    }
    if (cascade == 1) {
        return texture(lightCascades[1], at);
    }
    return texture(lightCascades[2], at);
}

vec3 cascadeTint(int cascade, vec3 fromBase, vec3 normal) {
    vec3 at = ((fromBase + (normal * (0.5 * LIGHT_CELL[cascade]))) / LIGHT_SPAN[cascade]) +
              ubo.light_wrap[cascade].xyz;

    if (cascade == 0) {
        return textureLod(lightTints[0], at, 0.0).rgb;
    }
    if (cascade == 1) {
        return textureLod(lightTints[1], at, 0.0).rgb;
    }
    return textureLod(lightTints[2], at, 0.0).rgb;
}

const vec4 LIGHT_OPEN_SKY = vec4(0.0, 0.0, 0.0, 1.0);
const float TINT_KNOWN    = 0.02;

// см. docs/lighting.md#цвет-света-ламп
struct CachedLight {
    vec4 levels;
    vec3 tint;
};

CachedLight cachedLight(vec3 normal) {
    vec3 fromBase = (fragPos - ubo.light_grid.xyz) * ubo.light_grid.w;
    vec3 fromEye  = abs(fromBase - ubo.occupancy_eye.xyz);
    float reach   = max(fromEye.x, max(fromEye.y, fromEye.z));

    int cascade = reach < LIGHT_EDGE[0] ? 0 : (reach < LIGHT_EDGE[1] ? 1 : 2);

    float edge  = LIGHT_EDGE[cascade];
    float outer = smoothstep(edge * LIGHT_EDGE_BLEND, edge, reach);

    bool blends = outer > 0.0;
    bool last   = cascade == LIGHT_LAST_CASCADE;

    vec4 levels = cascadeLight(cascade, fromBase, normal);
    if (blends) {
        levels = mix(levels, last ? LIGHT_OPEN_SKY : cascadeLight(cascade + 1, fromBase, normal), outer);
    }
    if (levels.g <= 0.0) {
        return CachedLight(levels, vec3(1.0));
    }

    vec3 tint = cascadeTint(cascade, fromBase, normal);
    if (blends) {
        tint = mix(tint, last ? vec3(0.0) : cascadeTint(cascade + 1, fromBase, normal), outer);
    }

    float brightest = max(tint.r, max(tint.g, tint.b));
    vec3 hue        = brightest < TINT_KNOWN ? vec3(1.0) : tint / brightest;
    return CachedLight(levels, mix(vec3(1.0), hue, ubo.lamp_params.y));
}

#define OCCUPANCY_SET 5
#include "occupancy.glsl"

const uint FLAT_ONLY     = 1u << 8u;
const uint FACE_SHIFT    = 9u;
const uint STATE_SHIFT   = 12u;
const uint MATERIAL_SHIFT = 18u;
const vec3 CORNER_REACH = vec3(416.0, 224.0, 416.0);
const vec3 CORNER_FADE  = vec3(32.0, 16.0, 32.0);

float cornerLevel(uint a, uint b, uint diagonal) {
    return (a + b == 2u) ? 3.0 : float(a + b + diagonal);
}

float cornersAcross(uint around, vec2 at) {
    uint minusU = (around >> 3u) & 1u;
    uint plusU  = (around >> 5u) & 1u;
    uint minusV = (around >> 1u) & 1u;
    uint plusV  = (around >> 7u) & 1u;

    float c00 = cornerLevel(minusU, minusV, around & 1u);
    float c10 = cornerLevel(plusU, minusV, (around >> 2u) & 1u);
    float c11 = cornerLevel(plusU, plusV, (around >> 8u) & 1u);
    float c01 = cornerLevel(minusU, plusV, (around >> 6u) & 1u);

    return mix(mix(c00, c10, at.x), mix(c01, c11, at.x), at.y) * (1.0 / 3.0);
}

vec3 cornersFromOccupancy() {
    uint face = (fragConvexMask >> FACE_SHIFT) & 7u;

    int axis = int(face >> 1u);
    int u    = (axis + 1) % 3;
    int v    = (axis + 2) % 3;

    ivec3 outward = ivec3(0);
    outward[axis] = (face & 1u) == 0u ? 1 : -1;

    bool wantsExposure = face == 2u && (fragConvexMask & FLAT_ONLY) == 0u;

    vec2 at = vec2(fract(fragGridPos[u]), fract(fragGridPos[v]));

    if (fragInstanceLight.z > 0.5) {
        ModelVolume volume = modelVolumeOf(fragInstanceLight.zw);

        ivec3 alongU = ivec3(0);
        alongU[u]    = 1;
        ivec3 alongV = ivec3(0);
        alongV[v]    = 1;

        ivec3 within = ivec3(floor(fragGridPos - (0.5 * vec3(outward))));

        float shaded = cornersAcross(modelPatch(volume, within + outward, alongU, alongV, 0u), at);
        float raised = 0.0;
        if (wantsExposure) {
            raised = cornersAcross(~modelPatch(volume, within, alongU, alongV, 1u) & 0x1FFu, at);
        }
        return vec3(shaded, raised, 1.0);
    }

    vec3 fromEye = abs(fragGridPos - ubo.occupancy_eye.xyz);
    vec3 fading  = smoothstep(CORNER_REACH - CORNER_FADE, CORNER_REACH, fromEye);
    float weight = 1.0 - max(fading.x, max(fading.y, fading.z));
    if (weight <= 0.0) {
        return vec3(0.0);
    }

    ivec3 host = (ubo.occupancy_base.xyz << OCCUPANCY_CHUNK_SHIFT) +
                 ivec3(floor(fragGridPos - (0.5 * vec3(outward))));
    if (!occupancyKnows(host >> OCCUPANCY_CHUNK_SHIFT, 0)) {
        return vec3(0.0);
    }

    ivec3 front = host + outward;

    OccupancyBricks ahead = occupancyBricksAround(front, u, v);

    float occlusion = 0.0;
    if (ahead.packed != 0u) {
        occlusion = cornersAcross(occupancyPatch(ahead, front[axis], axis, u, v), at);
    }

    float exposure = 0.0;
    if (wantsExposure) {
        OccupancyBricks under = ahead;
        if ((host.y >> 1) != (front.y >> 1)) {
            under = occupancyBricksAround(host, u, v);
        }
        if (under.packed != 0xFFFFFFFFu) {
            uint open = ~occupancyPatch(under, host.y, axis, u, v) & 0x1FFu;
            exposure  = cornersAcross(open, at);
        }
    }

    return vec3(occlusion, exposure, weight);
}

#if SHADOW_ENABLED
layout(set = 2, binding = 0) uniform sampler2DArrayShadow shadowMapArray;
#endif

struct PointLightData {
    vec4 position;
    vec4 color;

    float intensity;
    float range;
};

layout(set = 3, binding = 0, std430) readonly buffer PointLights {
    PointLightData lights[];
} pointLights;

layout(set = 3, binding = 1, std430) readonly buffer ClusterCounts {
    uint counts[];
} clusterCounts;

layout(set = 3, binding = 2, std430) readonly buffer ClusterIndices {
    uint indices[];
} clusterIndices;

layout(set = 3, binding = 3, std430) readonly buffer Blobs {
    BlobData items[];
} blobs;

layout(set = 3, binding = 4, std430) readonly buffer BlobCounts {
    uint counts[];
} blobCounts;

layout(set = 3, binding = 5, std430) readonly buffer BlobIndices {
    uint indices[];
} blobIndices;

#if SHADOW_ENABLED
int selectCascade(float viewDepth) {
    for (int i = 0; i < SHADOW_CASCADES - 1; ++i) {
        if (viewDepth < ubo.directional_light.cascades[i].x) {
            return i;
        }
    }
    return SHADOW_CASCADES - 1;
}

const vec2 PCF_DISK[12] = vec2[12](
    vec2(-0.326, -0.406), vec2(-0.840, -0.074), vec2(-0.696,  0.457),
    vec2(-0.203,  0.621), vec2( 0.962, -0.195), vec2( 0.473, -0.480),
    vec2( 0.519,  0.767), vec2( 0.185, -0.893), vec2( 0.507,  0.064),
    vec2( 0.896,  0.412), vec2(-0.322, -0.933), vec2(-0.792, -0.598)
);

float pcfRotation(vec2 seed) {
    return fract(sin(dot(seed, vec2(12.9898, 78.233))) * 43758.5453) * 6.2831853;
}

float calculateShadowForCascade(int cascadeIndex, vec3 normal) {
    float ndot = dot(normal, normalize(-ubo.directional_light.direction));
    if (ndot < 0.01) {
        return 1.0;
    }

    float texel  = ubo.directional_light.cascades[cascadeIndex].y;
    float radius = ubo.directional_light.shadow_filter.x;
    float ndotl  = clamp(ndot, 0.05, 1.0);
    float slope  = sqrt(1.0 - ndotl * ndotl) / ndotl;

    float bias = ubo.directional_light.shadow_filter.y +
                 (radius * ubo.directional_light.shadow_filter.z *
                  min(slope, SHADOW_SLOPE_LIMIT));

    vec3 newFragPos = fragPos + fragNormal * (texel * bias);

    vec4 fragPosLightSpace =
        ubo.directional_light.light_space_matrices[cascadeIndex] * vec4(newFragPos, 1.0);

    vec3 projCoords = fragPosLightSpace.xyz / fragPosLightSpace.w;

    projCoords.xy = projCoords.xy * 0.5 + 0.5;

    if (
        projCoords.x < 0.0 || projCoords.x > 1.0 ||
        projCoords.y < 0.0 || projCoords.y > 1.0 ||
        projCoords.z < 0.0 || projCoords.z > 1.0
    ) {
        return 1.0;
    }

    vec2 mapSize = vec2(textureSize(shadowMapArray, 0).xy);
    vec2 step    = vec2(1.0) / mapSize;

    float angle = pcfRotation(floor(projCoords.xy * mapSize));
    float sa    = sin(angle);
    float ca    = cos(angle);
    mat2 turn   = mat2(ca, -sa, sa, ca);

    float shadow = 0.0;
    for (int i = 0; i < 12; ++i) {
        vec2 offset = turn * PCF_DISK[i] * step * radius;
        shadow += texture(
            shadowMapArray,
            vec4(projCoords.xy + offset, float(cascadeIndex), projCoords.z)
        );
    }

    return shadow * (1.0 / 12.0);
}

float calculateShadow(vec3 normal, float viewDepth) {
    int cascadeIndex = selectCascade(viewDepth);

    float shadow = calculateShadowForCascade(cascadeIndex, normal);

    if (cascadeIndex < SHADOW_CASCADES - 1) {
        float nextSplit = ubo.directional_light.cascades[cascadeIndex].x;
        float blendStart = nextSplit * SHADOW_CASCADE_BLEND;
        float blendEnd = nextSplit;

        if (viewDepth > blendStart && viewDepth < blendEnd) {
            int nextCascadeIndex = cascadeIndex + 1;
            float nextShadow = calculateShadowForCascade(nextCascadeIndex, normal);

            float blendFactor = smoothstep(blendStart, blendEnd, viewDepth);
            shadow = mix(shadow, nextShadow, blendFactor);
        }
    }

    return shadow;
}
#endif

vec3 calculateDirectionalLight(vec3 normal, float shadow) {
    float sunElevation = -ubo.directional_light.direction.y;

    float dayFactor = smoothstep(0.2, 0.4, sunElevation);
    float twilightFactor = smoothstep(0.0, 0.2, sunElevation) * (1.0 - dayFactor);

    vec3 lightDir = normalize(-ubo.directional_light.direction);

    float wrap = ubo.directional_light.wrap;
    float diff = max((dot(lightDir, normal) + wrap) / (1.0 + wrap), 0.0);

    vec3 sunColor = ubo.directional_light.color * ubo.directional_light.intensity;
    vec3 twilightColor = vec3(1.0, 0.5, 0.2) * ubo.directional_light.intensity * 0.3;

    return diff * shadow * (sunColor * dayFactor + twilightColor * twilightFactor);
}

uint clusterOf(vec2 pixel, float depth) {
    float raw   = (log(max(depth, 1e-6)) * ubo.clusters.z_scale) + ubo.clusters.z_bias;
    uint slices = uint(ubo.clusters.slices);
    uint slice  = uint(clamp(int(floor(raw)), 0, int(slices) - 1));

    uint tile_x = min(uint(pixel.x / ubo.clusters.tile_size), ubo.clusters.tiles_x - 1u);
    uint tile_y = min(uint(pixel.y / ubo.clusters.tile_size), ubo.clusters.tiles_y - 1u);

    return (((slice * ubo.clusters.tiles_y) + tile_y) * ubo.clusters.tiles_x) + tile_x;
}

vec3 clusterHeat(uint count, uint cap) {
    if (count == 0u) {
        return vec3(0.0);
    }
    if (count > cap) {
        return vec3(1.0);
    }

    float t = clamp(float(count) / float(max(cap, 1u)), 0.0, 1.0);

    vec3 cold = vec3(0.10, 0.20, 0.80);
    vec3 mid  = vec3(0.10, 0.80, 0.20);
    vec3 warm = vec3(0.90, 0.80, 0.10);
    vec3 hot  = vec3(0.90, 0.10, 0.10);

    if (t < 0.33) {
        return mix(cold, mid, t / 0.33);
    }
    if (t < 0.66) {
        return mix(mid, warm, (t - 0.33) / 0.33);
    }
    return mix(warm, hot, (t - 0.66) / 0.34);
}

vec3 calculatePointLight(uint lightIndex, vec3 fragPos) {
    PointLightData light = pointLights.lights[lightIndex];

    vec3 offset = light.position.xyz - fragPos;

    // см. docs/lighting.md#динамические-источники
    float reach = length(offset);

    float raw = light.intensity * max(1.0 - (reach / max(light.range, 0.001)), 0.0);
    if (raw <= 0.0) {
        return vec3(0.0);
    }

    return light.color.xyz * pow(raw, ubo.lamp_params.w);
}

float blobShadow(vec3 fragPos, vec3 normal) {
    if (normal.y < -0.5) {
        return 1.0;
    }

    bool clustered = ubo.clusters.enabled == 1u;
    uint cap       = max(ubo.blob_dims.x, 1u);
    uint cluster   = 0u;
    uint count     = ubo.blob_dims.y;

    if (clustered) {
        cluster = clusterOf(gl_FragCoord.xy, viewDepth);
        count   = min(blobCounts.counts[cluster], cap);
    }

    if (count == 0u) {
        return 1.0;
    }

    float darkest = 0.0;

    for (uint n = 0u; n < count; ++n) {
        uint i = clustered ? blobIndices.indices[(cluster * cap) + n] : n;

        vec4 body = blobs.items[i].position_radius;
        vec4 p    = blobs.items[i].params;

        float fall   = max(p.x, 0.001);
        float height = max(p.z, 0.001);

        float rise   = max(body.y - fragPos.y, 0.0);
        float shrink = 1.0 / (1.0 + (rise / fall));

        float d = length(fragPos.xz - body.xz) / max(body.w * shrink, 0.001);

        // см. docs/lighting.md#пятна-под-телами
        vec3  toBody = vec3(body.x, body.y + (0.5 * height), body.z) - fragPos;
        vec3  dir    = toBody * inversesqrt(max(dot(toBody, toBody), 0.0001));

        float above = max(dir.y, 0.0);
        float lean  = max(dot(normal, dir), 0.0);

        float reach = max(p.w, 0.001);
        float tail  = 1.0 - smoothstep(0.75, 1.0, rise / reach);

        float a = (1.0 - smoothstep(0.6, 1.0, d)) * shrink * shrink * above * lean * tail * p.y;

        darkest = max(darkest, a);
    }

    return 1.0 - clamp(darkest * ubo.blob_strength, 0.0, 1.0);
}

vec3 calculateHemisphereAmbient(vec3 normal) {
    return mix(ubo.ambient_ground.rgb, ubo.ambient_sky.rgb, normal.y * 0.5 + 0.5);
}

layout(location = 0) out vec4 outColor;

vec4 shown(vec3 display) {
    return vec4(sceneFromDisplay(display, ubo.tonemap_params.xy), 0.0);
}

void main() {
    vec3 normal = normalize(fragNormal);

#if SHADOW_ENABLED
    float sunElevation = -ubo.directional_light.direction.y;
    float shadowReliability = smoothstep(0.15, 0.3, sunElevation);
    float shadow = mix(1.0, calculateShadow(normal, viewDepth), shadowReliability);
#else
    float shadow = 1.0;
#endif

    vec3 corners = fragInstanceLight.z > -0.5 ? cornersFromOccupancy() : vec3(0.0);

    float occlusion = corners.x * corners.z;
    float exposure  = corners.y * corners.z;

    occlusion = pow(occlusion, ubo.corner_shading.ao_curve);
    exposure  = pow(exposure, ubo.corner_shading.convex_curve);

    if (ubo.debug_view == 4u) {
        outColor = shown(vec3(exposure));
        return;
    }

    float aoFactor     = 1.0 - (occlusion * ubo.corner_shading.ao_strength);
    float convexFactor = 1.0 + (exposure * ubo.corner_shading.convex_strength);

    if (ubo.debug_view == 1u) {
        outColor = shown(vec3(aoFactor));
        return;
    }
    if (ubo.debug_view == 2u) {
        outColor = shown((normal * 0.5) + 0.5);
        return;
    }
    if (ubo.debug_view == 11u) {
        uint code   = (fragConvexMask >> STATE_SHIFT) & 63u;
        float facet = 0.7 + (0.3 * normal.y);
        vec3 marked = 0.5 + (0.5 * cos(6.2832 * ((float(code) * 0.381966) + vec3(0.0, 0.33, 0.67))));
        outColor    = shown((code == 0u ? vec3(0.5) : marked) * facet);
        return;
    }
    if (ubo.debug_view == 12u) {
        uint row    = (fragConvexMask >> MATERIAL_SHIFT) & 255u;
        float facet = 0.7 + (0.3 * normal.y);
        vec3 marked = 0.5 + (0.5 * cos(6.2832 * ((float(row) * 0.381966) + vec3(0.0, 0.33, 0.67))));
        outColor    = shown((row == 0u ? vec3(0.5) : marked) * facet);
        return;
    }

    CachedLight cached = cachedLight(normal);
    vec4 cacheLight    = cached.levels;

    float skyReach = pow(cacheLight.a, ubo.sky_params.x);
    float sunReach = pow(cacheLight.a, ubo.sky_params.y);

    if (ubo.debug_view == 10u) {
        outColor = shown(vec3(sunReach));
        return;
    }

    if (ubo.debug_view == 3u) {
        outColor = shown(vec3(skyReach));
        return;
    }

    float lampReach = pow(cacheLight.g, ubo.lamp_params.w);

    if (ubo.debug_view == 5u) {
        outColor = shown(cached.tint * lampReach);
        return;
    }

    if (ubo.debug_view == 6u) {
        outColor = shown(vec3(blobShadow(fragPos, normal)));
        return;
    }

    if (ubo.debug_view == 7u) {
        uint cluster = clusterOf(gl_FragCoord.xy, viewDepth);
        outColor = shown(clusterHeat(clusterCounts.counts[cluster], ubo.clusters.cap));
        return;
    }

    if (ubo.debug_view == 8u) {
        uint cluster = clusterOf(gl_FragCoord.xy, viewDepth);
        outColor = shown(clusterHeat(blobCounts.counts[cluster], ubo.blob_dims.x));
        return;
    }

    float blob = blobShadow(fragPos, normal);

    vec3 sky = mix(ubo.cave_ambient.rgb, calculateHemisphereAmbient(normal), skyReach);
    vec3 ambient = sky * aoFactor;

    vec3 directional = calculateDirectionalLight(normal, shadow) * sunReach;

    vec3 lamp = cached.tint * (ubo.lamp_params.x * lampReach * aoFactor);

    vec3 pointLighting = vec3(0.0);

    if (ubo.clusters.enabled == 0u) {
        for (uint i = 0; i < ubo.point_lights_count; i++) {
            pointLighting += calculatePointLight(i, fragPos);
        }
    } else {
        uint cluster = clusterOf(gl_FragCoord.xy, viewDepth);
        uint cap     = ubo.clusters.cap;
        uint count   = min(clusterCounts.counts[cluster], cap);

        for (uint i = 0; i < count; i++) {
            pointLighting += calculatePointLight(clusterIndices.indices[(cluster * cap) + i], fragPos);
        }
    }

    vec3 lighting = (ambient + directional + lamp + pointLighting) * convexFactor * blob;
    vec3 result = lighting * fragColor;

    vec3 glow = fragColor * fragGlow * ubo.glow_params.x;
    result += glow;

    if (ubo.fog.enabled != 0u) {
        float fogFactor = clamp(
            (ubo.fog.far_distance - viewDepth) / (ubo.fog.far_distance - ubo.fog.near_distance),
            0.0, 1.0
        );
        result = mix(ubo.fog.color, result, fogFactor);
        glow *= fogFactor;
    }

    const vec3 luma = vec3(0.2126, 0.7152, 0.0722);
    float glowShare = clamp(dot(glow, luma) / max(dot(result, luma), 1e-4), 0.0, 1.0);

    outColor = vec4(result, glowShare);
}
