module vw.world;

import std;
import vw.core;

namespace vw::ecs {

namespace {

constexpr int32 search_reach = 2;

auto cell_hash(vec2i cell, uint32 seed, uint32 salt) -> float64 {
    uint32 h = (static_cast<uint32>(cell.x) * 0x27d4eb2dU) ^ (static_cast<uint32>(cell.y) * 0x165667b1U) ^
               (salt * 0x9e3779b9U) ^ (seed * 0x85ebca6bU);
    h ^= h >> 15U;
    h *= 0x2c1b3c6dU;
    h ^= h >> 12U;
    h *= 0x297a2d39U;
    h ^= h >> 15U;
    return static_cast<float64>(h) / 4294967296.0;
}

}  // namespace

region_map::region_map(
    params p
)
    : params_{p}, warp_{p.seed ^ 0x5bd1e995U} {}

auto region_map::site_of(
    vec2i cell
) const -> std::pair<float64, float64> {
    const float64 spread = is_home(cell) ? 0.0 : params_.jitter * 2.0;
    const float64 ox     = (cell_hash(cell, params_.seed, 1) - 0.5) * spread;
    const float64 oz     = (cell_hash(cell, params_.seed, 2) - 0.5) * spread;
    return {
        (static_cast<float64>(cell.x) + ox) * params_.spacing_voxels,
        (static_cast<float64>(cell.y) + oz) * params_.spacing_voxels,
    };
}

auto region_map::sample(
    float64 x, float64 z
) const -> region_sample {
    const float64 f  = params_.warp_frequency;
    const float64 wx = x + (warp_.fractal((x * f) + 11.0, (z * f) + 3.0, 3) * params_.warp_voxels);
    const float64 wz = z + (warp_.fractal((x * f) + 23.0, (z * f) + 41.0, 3) * params_.warp_voxels);

    const vec2i around{
        static_cast<int32>(std::lround(wx / params_.spacing_voxels)),
        static_cast<int32>(std::lround(wz / params_.spacing_voxels)),
    };

    constexpr int32 side  = (search_reach * 2) + 1;
    constexpr int32 count = side * side;

    std::array<vec2i, count> cells{};
    std::array<std::pair<float64, float64>, count> sites{};

    int32 nearest       = 0;
    float64 nearest_sq  = std::numeric_limits<float64>::max();

    for (int32 i = 0; i < count; ++i) {
        cells[i] = around + vec2i{(i % side) - search_reach, (i / side) - search_reach};
        sites[i] = site_of(cells[i]);

        const float64 dx = wx - sites[i].first;
        const float64 dz = wz - sites[i].second;
        const float64 sq = (dx * dx) + (dz * dz);
        if (sq < nearest_sq) {
            nearest_sq = sq;
            nearest    = i;
        }
    }

    const auto [ax, az] = sites[nearest];

    float64 edge = std::numeric_limits<float64>::max();
    for (int32 i = 0; i < count; ++i) {
        if (i == nearest) {
            continue;
        }
        const auto [bx, bz] = sites[i];
        const float64 gap   = std::sqrt(((bx - ax) * (bx - ax)) + ((bz - az) * (bz - az)));
        const float64 to_b  = ((wx - bx) * (wx - bx)) + ((wz - bz) * (wz - bz));
        edge = std::min(edge, (to_b - nearest_sq) / (2.0 * gap));
    }

    return region_sample{
        .cell          = cells[nearest],
        .site_x        = ax,
        .site_z        = az,
        .edge_distance = edge,
    };
}

}  // namespace vw::ecs
