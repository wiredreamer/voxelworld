const int OCCUPANCY_LEVELS       = 3;
const int OCCUPANCY_CHUNK_SHIFT  = 6;
const float OCCUPANCY_CHUNK      = 64.0;
const int OCCUPANCY_TEXTURE_MASK = 255;
const int OCCUPANCY_COARSE_MASK  = 127;
const int OCCUPANCY_STEP_LIMIT   = 192;
const float OCCUPANCY_NUDGE      = 1.0e-3;

layout(set = OCCUPANCY_SET, binding = 0) uniform OccupancyParams {
    ivec4 origin[3];
    uvec4 valid[68];
} occupancy;

layout(set = OCCUPANCY_SET, binding = 1) uniform usampler3D occupancyBricks[3];

layout(std430, set = OCCUPANCY_SET, binding = 2) readonly buffer ModelOccupancy {
    uint words[];
} modelOccupancy;

struct ModelVolume {
    ivec3 size;
    uint offset;
    uint rowWords;
};

ModelVolume modelVolumeOf(vec2 packed) {
    uint size = uint(packed.x + 0.5);
    ivec3 dims = ivec3(size & 255u, (size >> 8u) & 255u, (size >> 16u) & 255u);
    return ModelVolume(dims, uint(packed.y + 0.5), uint(dims.x + 31) >> 5u);
}

uint modelSolid(ModelVolume volume, ivec3 cell, uint beyond) {
    if (any(lessThan(cell, ivec3(0))) || any(greaterThanEqual(cell, volume.size))) {
        return beyond;
    }

    uint row  = uint(cell.y + (volume.size.y * cell.z)) * volume.rowWords;
    uint word = modelOccupancy.words[volume.offset + row + (uint(cell.x) >> 5u)];
    return (word >> (uint(cell.x) & 31u)) & 1u;
}

uint modelPatch(ModelVolume volume, ivec3 centre, ivec3 alongU, ivec3 alongV, uint beyond) {
    uint solid = 0u;
    for (int j = 0; j < 3; ++j) {
        for (int i = 0; i < 3; ++i) {
            ivec3 cell = centre + ((i - 1) * alongU) + ((j - 1) * alongV);
            solid |= modelSolid(volume, cell, beyond) << uint((j * 3) + i);
        }
    }
    return solid;
}

struct OccupancyHit {
    bool found;
    ivec3 voxel;
    int level;
    int entryAxis;
    float distance;
    int steps;
};

int occupancyFirstValidBit(int level) {
    return level == 0 ? 0 : (level == 1 ? 512 : 4608);
}

bool occupancyKnows(ivec3 chunk, int level) {
    int side    = level == 0 ? 8 : 16;
    ivec3 local = chunk - occupancy.origin[level].xyz;
    if (any(lessThan(local, ivec3(0))) || any(greaterThanEqual(local, ivec3(side)))) {
        return false;
    }

    ivec3 slot = chunk & (side - 1);
    int bit    = occupancyFirstValidBit(level) + slot.x + (side * (slot.y + (side * slot.z)));
    uint word  = occupancy.valid[bit >> 7][(bit >> 5) & 3];
    return ((word >> uint(bit & 31)) & 1u) != 0u;
}

uint occupancyBrickAt(ivec3 voxel, int level) {
    if (level == 0) {
        return texelFetch(occupancyBricks[0], (voxel >> 1) & OCCUPANCY_TEXTURE_MASK, 0).r;
    }
    if (level == 1) {
        return texelFetch(occupancyBricks[1], (voxel >> 2) & OCCUPANCY_TEXTURE_MASK, 0).r;
    }
    return texelFetch(occupancyBricks[2], (voxel >> 3) & OCCUPANCY_COARSE_MASK, 0).r;
}

uint occupancyBrickBit(ivec3 cell) {
    return uint((cell.x & 1) | ((cell.y & 1) << 1) | ((cell.z & 1) << 2));
}

struct OccupancyBricks {
    uint packed;
    uint parityU;
    uint parityV;
};

OccupancyBricks occupancyBricksAround(ivec3 centre, int u, int v) {
    ivec3 corner = centre;
    corner[u] -= 1;
    corner[v] -= 1;

    ivec3 brick   = corner >> 1;
    ivec3 brickU  = brick;
    brickU[u]    += 1;
    ivec3 brickV  = brick;
    brickV[v]    += 1;
    ivec3 brickUV = brickU;
    brickUV[v]   += 1;

    uint low       = texelFetch(occupancyBricks[0], brick & OCCUPANCY_TEXTURE_MASK, 0).r;
    uint alongU    = texelFetch(occupancyBricks[0], brickU & OCCUPANCY_TEXTURE_MASK, 0).r;
    uint alongV    = texelFetch(occupancyBricks[0], brickV & OCCUPANCY_TEXTURE_MASK, 0).r;
    uint alongBoth = texelFetch(occupancyBricks[0], brickUV & OCCUPANCY_TEXTURE_MASK, 0).r;

    return OccupancyBricks(
        low | (alongU << 8u) | (alongV << 16u) | (alongBoth << 24u),
        uint(corner[u] & 1), uint(corner[v] & 1)
    );
}

uint occupancyPatch(OccupancyBricks held, int layer, int axis, int u, int v) {
    uint shiftU = uint(u);
    uint shiftV = uint(v);
    uint inLayer = uint(layer & 1) << uint(axis);

    uint solid = 0u;
    for (uint j = 0u; j < 3u; ++j) {
        uint alongV = held.parityV + j;
        uint rowBit = ((alongV >> 1u) << 4u) | ((alongV & 1u) << shiftV) | inLayer;

        for (uint i = 0u; i < 3u; ++i) {
            uint alongU = held.parityU + i;
            uint bit    = rowBit + ((alongU >> 1u) << 3u) + ((alongU & 1u) << shiftU);

            solid |= ((held.packed >> bit) & 1u) << ((j * 3u) + i);
        }
    }
    return solid;
}

OccupancyHit marchOccupancy(
    ivec3 baseChunk, vec3 origin, vec3 direction, float maxDistance, int finestLevel
) {
    const float never = 1.0e30;

    ivec3 baseVoxel = baseChunk << OCCUPANCY_CHUNK_SHIFT;

    float travelled = 0.0;
    int entryAxis   = -1;

    for (int taken = 0; taken < OCCUPANCY_STEP_LIMIT; ++taken) {
        vec3 at     = origin + (direction * travelled);
        ivec3 voxel = baseVoxel + ivec3(floor(at));

        ivec3 chunk = voxel >> OCCUPANCY_CHUNK_SHIFT;

        bvec3 known = bvec3(
            finestLevel <= 0 && occupancyKnows(chunk, 0),
            finestLevel <= 1 && occupancyKnows(chunk, 1), occupancyKnows(chunk, 2)
        );
        int finest = known.x ? 0 : (known.y ? 1 : (known.z ? 2 : -1));

        float box = OCCUPANCY_CHUNK;
        if (finest >= 0) {
            bool crossed = false;
            for (int level = OCCUPANCY_LEVELS - 1; level > finest; --level) {
                if (known[level] && occupancyBrickAt(voxel, level) == 0u) {
                    box     = float(2 << level);
                    crossed = true;
                    break;
                }
            }

            if (!crossed) {
                uint brick = occupancyBrickAt(voxel, finest);
                ivec3 cell = voxel >> finest;
                if (brick == 0u) {
                    box = float(2 << finest);
                } else if (((brick >> occupancyBrickBit(cell)) & 1u) != 0u) {
                    return OccupancyHit(true, voxel, finest, entryAxis, travelled, taken);
                } else {
                    box = float(1 << finest);
                }
            }
        }

        float leavesAt = never;
        for (int axis = 0; axis < 3; ++axis) {
            float along = direction[axis];
            if (along == 0.0) {
                continue;
            }

            float low  = floor(at[axis] / box) * box;
            float wall = along > 0.0 ? low + box : low;

            float reaches = (wall - origin[axis]) / along;
            if (reaches < leavesAt) {
                leavesAt  = reaches;
                entryAxis = axis;
            }
        }

        travelled = max(leavesAt, travelled) + OCCUPANCY_NUDGE;
        if (travelled > maxDistance) {
            break;
        }
    }

    return OccupancyHit(false, ivec3(0), 0, -1, maxDistance, OCCUPANCY_STEP_LIMIT);
}
