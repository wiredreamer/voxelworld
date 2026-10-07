const int OCCUPANCY_LEVELS       = 3;
const int OCCUPANCY_CHUNK_SHIFT  = 6;
const float OCCUPANCY_CHUNK      = 64.0;
const int OCCUPANCY_TEXTURE_MASK = 127;
const int OCCUPANCY_STEP_LIMIT   = 192;
const float OCCUPANCY_NUDGE      = 1.0e-3;

layout(set = OCCUPANCY_SET, binding = 0) uniform OccupancyParams {
    ivec4 origin[3];
    uvec4 valid[37];
} occupancy;

layout(set = OCCUPANCY_SET, binding = 1) uniform usampler3D occupancyBricks[3];

struct OccupancySample {
    int level;
    uint brick;
};

struct OccupancyHit {
    bool found;
    ivec3 voxel;
    int level;
    int entryAxis;
    float distance;
    int steps;
};

int occupancyFirstValidBit(int level) {
    return level == 0 ? 0 : (level == 1 ? 64 : 576);
}

bool occupancyKnows(ivec3 chunk, int level) {
    int side    = 4 << level;
    ivec3 local = chunk - occupancy.origin[level].xyz;
    if (any(lessThan(local, ivec3(0))) || any(greaterThanEqual(local, ivec3(side)))) {
        return false;
    }

    ivec3 slot = chunk & (side - 1);
    int bit    = occupancyFirstValidBit(level) + slot.x + (side * (slot.y + (side * slot.z)));
    uint word  = occupancy.valid[bit >> 7][(bit >> 5) & 3];
    return ((word >> uint(bit & 31)) & 1u) != 0u;
}

OccupancySample occupancyAt(ivec3 voxel) {
    ivec3 chunk = voxel >> OCCUPANCY_CHUNK_SHIFT;

    if (occupancyKnows(chunk, 0)) {
        return OccupancySample(0, texelFetch(occupancyBricks[0], (voxel >> 1) & OCCUPANCY_TEXTURE_MASK, 0).r);
    }
    if (occupancyKnows(chunk, 1)) {
        return OccupancySample(1, texelFetch(occupancyBricks[1], (voxel >> 2) & OCCUPANCY_TEXTURE_MASK, 0).r);
    }
    if (occupancyKnows(chunk, 2)) {
        return OccupancySample(2, texelFetch(occupancyBricks[2], (voxel >> 3) & OCCUPANCY_TEXTURE_MASK, 0).r);
    }
    return OccupancySample(-1, 0u);
}

uint occupancyBrickBit(ivec3 cell) {
    return uint((cell.x & 1) | ((cell.y & 1) << 1) | ((cell.z & 1) << 2));
}

OccupancyHit marchOccupancy(ivec3 baseChunk, vec3 origin, vec3 direction, float maxDistance) {
    const float never = 1.0e30;

    ivec3 baseVoxel = baseChunk << OCCUPANCY_CHUNK_SHIFT;

    float travelled = 0.0;
    int entryAxis   = -1;

    for (int taken = 0; taken < OCCUPANCY_STEP_LIMIT; ++taken) {
        vec3 at     = origin + (direction * travelled);
        ivec3 voxel = baseVoxel + ivec3(floor(at));

        OccupancySample found = occupancyAt(voxel);

        float box = OCCUPANCY_CHUNK;
        if (found.level >= 0) {
            ivec3 cell = voxel >> found.level;
            if (found.brick == 0u) {
                box = float(2 << found.level);
            } else if (((found.brick >> occupancyBrickBit(cell)) & 1u) != 0u) {
                return OccupancyHit(true, voxel, found.level, entryAxis, travelled, taken);
            } else {
                box = float(1 << found.level);
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
