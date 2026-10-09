export module vw.world:grid.footing;

import std;

import vw.core;

// см. docs/world.md#опора-под-телом
export namespace vw::ecs {

struct footprint {
    float32 x      = 0.0F;
    float32 z      = 0.0F;
    float32 half_x = 0.0F;
    float32 half_z = 0.0F;
};

struct footing {
    float32 stand = 0.0F;
    bool found    = false;
};

inline constexpr float32 footing_skin         = 0.01F;
inline constexpr float32 footing_reach_slack  = 2.0F * footing_skin;

template <typename Solid>
concept solid_voxel_query = std::predicate<const Solid&, int32, int32, int32>;

template <solid_voxel_query Solid>
[[nodiscard]] auto footing_under(
    const Solid& solid, float32 voxel_size, const footprint& under, float32 feet, float32 step_height
) -> footing {
    const float32 reach    = feet + step_height + footing_reach_slack;
    const auto highest     = static_cast<int32>(std::floor(reach / voxel_size)) - 1;
    const auto lowest      = static_cast<int32>(std::floor((feet - (2.0F * step_height)) / voxel_size)) - 1;
    const auto first_x     = static_cast<int32>(std::floor((under.x - under.half_x) / voxel_size));
    const auto last_x      = static_cast<int32>(std::ceil((under.x + under.half_x) / voxel_size)) - 1;
    const auto first_z     = static_cast<int32>(std::floor((under.z - under.half_z) / voxel_size));
    const auto last_z      = static_cast<int32>(std::ceil((under.z + under.half_z) / voxel_size)) - 1;

    footing seen{};

    for (int32 vx = first_x; vx <= last_x; ++vx) {
        const float32 across =
            std::min(under.x + under.half_x, static_cast<float32>(vx + 1) * voxel_size) -
            std::max(under.x - under.half_x, static_cast<float32>(vx) * voxel_size);
        if (across <= 0.0F) {
            continue;
        }
        for (int32 vz = first_z; vz <= last_z; ++vz) {
            const float32 along =
                std::min(under.z + under.half_z, static_cast<float32>(vz + 1) * voxel_size) -
                std::max(under.z - under.half_z, static_cast<float32>(vz) * voxel_size);
            if (along <= 0.0F) {
                continue;
            }
            int32 vy   = highest;
            bool above = solid(vx, vy + 1, vz);
            while (vy >= lowest) {
                const bool here = solid(vx, vy, vz);
                if (here && !above) {
                    break;
                }
                above = here;
                --vy;
            }
            if (vy < lowest) {
                continue;
            }
            const float32 top = static_cast<float32>(vy + 1) * voxel_size;
            seen.stand        = seen.found ? std::max(seen.stand, top) : top;
            seen.found        = true;
        }
    }
    return seen;
}

template <solid_voxel_query Solid>
[[nodiscard]] auto body_fits(
    const Solid& solid, float32 voxel_size, const footprint& under, float32 from_height,
    float32 to_height
) -> bool {
    const auto first = [voxel_size](float32 v) {
        return static_cast<int32>(std::floor((v + footing_skin) / voxel_size));
    };
    const auto last = [voxel_size](float32 v) {
        return static_cast<int32>(std::ceil((v - footing_skin) / voxel_size)) - 1;
    };

    for (int32 vx = first(under.x - under.half_x); vx <= last(under.x + under.half_x); ++vx) {
        for (int32 vz = first(under.z - under.half_z); vz <= last(under.z + under.half_z); ++vz) {
            for (int32 vy = first(from_height); vy <= last(to_height); ++vy) {
                if (solid(vx, vy, vz)) {
                    return false;
                }
            }
        }
    }
    return true;
}

}  // namespace vw::ecs
