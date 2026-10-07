export module vw.core:spatial.occupancy;

import std;

import :types;
import :vector;

export namespace vw::spatial {

// см. docs/rendering.md#занятость-на-gpu
struct occupancy_clipmap_layout {
    static constexpr int32 level_count        = 3;
    static constexpr int32 chunk_voxels       = 64;
    static constexpr int32 chunk_shift        = 6;
    static constexpr int32 base_window_chunks = 4;
    static constexpr int32 texture_side       = 128;

    [[nodiscard]] static constexpr auto window_chunks(int32 level) -> int32 {
        return base_window_chunks << level;
    }

    [[nodiscard]] static constexpr auto bricks_per_chunk(int32 level) -> int32 {
        return (chunk_voxels / 2) >> level;
    }

    [[nodiscard]] static constexpr auto cell_voxels(int32 level) -> int32 {
        return 1 << level;
    }

    [[nodiscard]] static constexpr auto slot_count(int32 level) -> int32 {
        const int32 side = window_chunks(level);
        return side * side * side;
    }

    [[nodiscard]] static constexpr auto first_valid_bit(int32 level) -> int32 {
        int32 bit = 0;
        for (int32 finer = 0; finer < level; ++finer) {
            bit += slot_count(finer);
        }
        return bit;
    }

    static constexpr int32 valid_bit_count  = 64 + 512 + 4096;
    static constexpr int32 valid_word_count = valid_bit_count / 32;
};

static_assert(
    occupancy_clipmap_layout::first_valid_bit(occupancy_clipmap_layout::level_count) ==
    occupancy_clipmap_layout::valid_bit_count
);
static_assert(
    occupancy_clipmap_layout::window_chunks(2) * occupancy_clipmap_layout::bricks_per_chunk(2) ==
    occupancy_clipmap_layout::texture_side
);

[[nodiscard]] constexpr auto occupancy_chunk_of(vec3i voxel) -> vec3i {
    constexpr int32 shift = occupancy_clipmap_layout::chunk_shift;
    return {voxel.x >> shift, voxel.y >> shift, voxel.z >> shift};
}

[[nodiscard]] constexpr auto occupancy_window_origin(vec3i centre_voxel, int32 level) -> vec3i {
    constexpr int32 half_chunk = occupancy_clipmap_layout::chunk_voxels / 2;
    const int32 half_window    = occupancy_clipmap_layout::window_chunks(level) / 2;

    const vec3i nearest_corner = occupancy_chunk_of(
        {centre_voxel.x + half_chunk, centre_voxel.y + half_chunk, centre_voxel.z + half_chunk}
    );
    return {
        nearest_corner.x - half_window, nearest_corner.y - half_window,
        nearest_corner.z - half_window
    };
}

[[nodiscard]] constexpr auto occupancy_window_holds(vec3i chunk, vec3i origin, int32 level)
    -> bool {
    const int32 side = occupancy_clipmap_layout::window_chunks(level);
    return chunk.x >= origin.x && chunk.x < origin.x + side && chunk.y >= origin.y &&
           chunk.y < origin.y + side && chunk.z >= origin.z && chunk.z < origin.z + side;
}

[[nodiscard]] constexpr auto occupancy_slot_of(vec3i chunk, int32 level) -> vec3i {
    const int32 mask = occupancy_clipmap_layout::window_chunks(level) - 1;
    return {chunk.x & mask, chunk.y & mask, chunk.z & mask};
}

[[nodiscard]] constexpr auto occupancy_slot_index(vec3i slot, int32 level) -> int32 {
    const int32 side = occupancy_clipmap_layout::window_chunks(level);
    return slot.x + (side * (slot.y + (side * slot.z)));
}

[[nodiscard]] constexpr auto occupancy_brick_bit(vec3i cell) -> uint32 {
    return static_cast<uint32>((cell.x & 1) | ((cell.y & 1) << 1) | ((cell.z & 1) << 2));
}

struct occupancy_sample {
    int32 level = -1;
    uint8 brick = 0;

    [[nodiscard]] constexpr auto is_known() const -> bool {
        return level >= 0;
    }
};

struct occupancy_hit {
    vec3i voxel{};
    int32 level      = 0;
    int32 entry_axis = -1;
    float32 distance = 0.0F;
    uint32 steps     = 0;
};

inline constexpr uint32 occupancy_march_step_limit = 192;
inline constexpr float32 occupancy_march_nudge     = 1.0e-3F;

template <typename Sample>
[[nodiscard]] auto march_occupancy(
    vec3i base_chunk, const vec3f& origin, const vec3f& direction, float32 max_distance,
    Sample&& sample_at
) -> std::optional<occupancy_hit> {
    constexpr float32 never = 1.0e30F;

    const vec3i base_voxel{
        base_chunk.x << occupancy_clipmap_layout::chunk_shift,
        base_chunk.y << occupancy_clipmap_layout::chunk_shift,
        base_chunk.z << occupancy_clipmap_layout::chunk_shift,
    };

    float32 travelled = 0.0F;
    int32 entry_axis  = -1;

    for (uint32 step = 0; step < occupancy_march_step_limit; ++step) {
        const vec3f at{
            origin.x + (direction.x * travelled),
            origin.y + (direction.y * travelled),
            origin.z + (direction.z * travelled),
        };
        const vec3i local{
            static_cast<int32>(std::floor(at.x)),
            static_cast<int32>(std::floor(at.y)),
            static_cast<int32>(std::floor(at.z)),
        };
        const vec3i voxel{base_voxel.x + local.x, base_voxel.y + local.y, base_voxel.z + local.z};

        const occupancy_sample found = sample_at(voxel);

        float32 box = static_cast<float32>(occupancy_clipmap_layout::chunk_voxels);
        if (found.is_known()) {
            const vec3i cell{
                voxel.x >> found.level, voxel.y >> found.level, voxel.z >> found.level
            };
            if (found.brick == 0) {
                box = static_cast<float32>(2 << found.level);
            } else if (((found.brick >> occupancy_brick_bit(cell)) & 1U) != 0) {
                return occupancy_hit{
                    .voxel      = voxel,
                    .level      = found.level,
                    .entry_axis = entry_axis,
                    .distance   = travelled,
                    .steps      = step,
                };
            } else {
                box = static_cast<float32>(1 << found.level);
            }
        }

        float32 leaves_at = never;
        for (int32 axis = 0; axis < 3; ++axis) {
            const float32 along = direction[static_cast<std::size_t>(axis)];
            if (along == 0.0F) {
                continue;
            }

            const float32 low  = std::floor(at[static_cast<std::size_t>(axis)] / box) * box;
            const float32 wall = along > 0.0F ? low + box : low;

            const float32 reaches =
                (wall - origin[static_cast<std::size_t>(axis)]) / along;
            if (reaches < leaves_at) {
                leaves_at  = reaches;
                entry_axis = axis;
            }
        }

        travelled = std::max(leaves_at, travelled) + occupancy_march_nudge;
        if (travelled > max_distance) {
            return std::nullopt;
        }
    }

    return std::nullopt;
}

}  // namespace vw::spatial
