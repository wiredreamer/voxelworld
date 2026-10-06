module vw.world;

import std;
import vw.core;

namespace vw::ecs {

namespace {

constexpr int32 draft_side   = 64;
constexpr int32 draft_centre = draft_side / 2;

class tree_dice {
public:
    explicit tree_dice(uint64 seed) : state_{seed ^ 0xD1B54A32D192ED03ULL} {}

    auto next() -> uint64 {
        state_ += 0x9E3779B97F4A7C15ULL;
        uint64 z = state_;
        z        = (z ^ (z >> 30U)) * 0xBF58476D1CE4E5B9ULL;
        z        = (z ^ (z >> 27U)) * 0x94D049BB133111EBULL;
        return z ^ (z >> 31U);
    }

    auto roll(int32 low, int32 high) -> int32 {
        if (high <= low) {
            return low;
        }
        return low + static_cast<int32>(next() % static_cast<uint64>(high - low + 1));
    }

    auto unit() -> float32 {
        return static_cast<float32>(next() >> 40U) / static_cast<float32>(1U << 24U);
    }

private:
    uint64 state_;
};

auto shade_of(voxel look, int32 step) -> voxel {
    for (const auto& group : voxels::groups) {
        const int32 first = group.first.value;
        const int32 last  = first + group.count - 1;
        if (look.value >= first && look.value <= last) {
            return voxel{static_cast<uint8>(std::clamp(look.value + step, first, last))};
        }
    }
    return look;
}

auto cell_noise(vec3i at, uint64 seed) -> uint64 {
    uint64 h = seed ^ (static_cast<uint64>(static_cast<uint32>(at.x)) * 0x9E3779B97F4A7C15ULL) ^
               (static_cast<uint64>(static_cast<uint32>(at.y)) * 0xC2B2AE3D27D4EB4FULL) ^
               (static_cast<uint64>(static_cast<uint32>(at.z)) * 0x165667B19E3779F9ULL);
    h ^= h >> 31U;
    h *= 0x94D049BB133111EBULL;
    return h ^ (h >> 29U);
}

auto draft_storage() -> std::vector<uint8>& {
    thread_local std::vector<uint8> looks(static_cast<std::size_t>(draft_side * draft_side * draft_side), 0);
    return looks;
}

auto turned(vec3i offset, uint8 quarter_turns) -> vec3i {
    switch (quarter_turns & 3U) {
        case 1: return {-offset.z, offset.y, offset.x};
        case 2: return {-offset.x, offset.y, -offset.z};
        case 3: return {offset.z, offset.y, -offset.x};
        default: return offset;
    }
}

class draft {
public:
    draft() : looks_{draft_storage()} {}

    ~draft() {
        if (hi_.x < lo_.x) {
            return;
        }
        for (int32 z = lo_.z; z <= hi_.z; ++z) {
            for (int32 y = lo_.y; y <= hi_.y; ++y) {
                for (int32 x = lo_.x; x <= hi_.x; ++x) {
                    looks_[index_({x, y, z})] = 0;
                }
            }
        }
    }

    draft(const draft&)                    = delete;
    auto operator=(const draft&) -> draft& = delete;
    draft(draft&&)                         = delete;
    auto operator=(draft&&) -> draft&      = delete;

    auto put(vec3i at, voxel look) -> void {
        if (at.x < 0 || at.y < 0 || at.z < 0 || at.x >= draft_side || at.y >= draft_side || at.z >= draft_side) {
            return;
        }
        const auto i = index_(at);
        if (voxels::bark.contains(voxel{looks_[i]}) && !voxels::bark.contains(look)) {
            return;
        }
        looks_[i] = look.value;
        lo_       = {std::min(lo_.x, at.x), std::min(lo_.y, at.y), std::min(lo_.z, at.z)};
        hi_       = {std::max(hi_.x, at.x), std::max(hi_.y, at.y), std::max(hi_.z, at.z)};
    }

    auto blob(vec3f centre, float32 radius, float32 roughness, uint64 seed, voxel look) -> void {
        const int32 r = static_cast<int32>(std::ceil(radius)) + 1;
        const vec3i c{
            static_cast<int32>(std::floor(centre.x)), static_cast<int32>(std::floor(centre.y)),
            static_cast<int32>(std::floor(centre.z))
        };
        for (int32 dy = -r; dy <= r; ++dy) {
            for (int32 dz = -r; dz <= r; ++dz) {
                for (int32 dx = -r; dx <= r; ++dx) {
                    const vec3i at = c + vec3i{dx, dy, dz};
                    const float32 fx = static_cast<float32>(at.x) + 0.5F - centre.x;
                    const float32 fy = static_cast<float32>(at.y) + 0.5F - centre.y;
                    const float32 fz = static_cast<float32>(at.z) + 0.5F - centre.z;
                    const float32 d  = std::sqrt((fx * fx) + (fy * fy) + (fz * fz));
                    const uint64 h   = cell_noise(at, seed);
                    const float32 bite =
                        roughness * static_cast<float32>(h & 0xFFFFU) / 65535.0F;
                    if (d > radius * (1.0F - (bite * 0.5F))) {
                        continue;
                    }
                    const int32 tint = ((h >> 20U) % 4U) == 0 ? -1 : (((h >> 20U) % 7U) == 1 ? 1 : 0);
                    put(at, shade_of(look, tint));
                }
            }
        }
    }

    auto line(vec3f from, vec3f to, voxel look) -> void {
        const vec3f d     = to - from;
        const float32 len = std::max({std::abs(d.x), std::abs(d.y), std::abs(d.z), 1.0F});
        const auto steps  = static_cast<int32>(std::ceil(len));
        for (int32 i = 0; i <= steps; ++i) {
            const float32 t = static_cast<float32>(i) / static_cast<float32>(steps);
            const vec3f p   = from + (d * t);
            put({static_cast<int32>(std::floor(p.x)), static_cast<int32>(std::floor(p.y)),
                 static_cast<int32>(std::floor(p.z))},
                look);
        }
    }

    [[nodiscard]] auto harvest(vec3i root, uint8 quarter_turns) const -> plant_shape {
        plant_shape out;
        if (hi_.x < lo_.x) {
            return out;
        }
        out.min = vec3i{std::numeric_limits<int32>::max(), std::numeric_limits<int32>::max(),
                        std::numeric_limits<int32>::max()};
        out.max = vec3i{std::numeric_limits<int32>::lowest(), std::numeric_limits<int32>::lowest(),
                        std::numeric_limits<int32>::lowest()};
        for (int32 z = lo_.z; z <= hi_.z; ++z) {
            for (int32 y = lo_.y; y <= hi_.y; ++y) {
                for (int32 x = lo_.x; x <= hi_.x; ++x) {
                    const uint8 look = looks_[index_({x, y, z})];
                    if (look == 0) {
                        continue;
                    }
                    const vec3i offset = turned(vec3i{x, y, z} - root, quarter_turns);
                    out.voxels.push_back({.offset = offset, .look = voxel{look}});
                    out.min = {std::min(out.min.x, offset.x), std::min(out.min.y, offset.y),
                               std::min(out.min.z, offset.z)};
                    out.max = {std::max(out.max.x, offset.x + 1), std::max(out.max.y, offset.y + 1),
                               std::max(out.max.z, offset.z + 1)};
                }
            }
        }
        return out;
    }

private:
    [[nodiscard]] static auto index_(vec3i at) -> std::size_t {
        return static_cast<std::size_t>(at.x + (draft_side * (at.y + (draft_side * at.z))));
    }

    std::vector<uint8>& looks_;
    vec3i lo_{draft_side, draft_side, draft_side};
    vec3i hi_{-1, -1, -1};
};

}  // namespace

// см. docs/world.md#деревья
auto grow_tree(
    const tree_species& species, uint64 seed, uint8 quarter_turns, voxel bark, voxel leaves
) -> plant_shape {
    tree_dice dice{seed};
    draft shape;

    const int32 height   = dice.roll(species.min_height, std::max(species.min_height, species.max_height));
    const int32 base     = height >= 16 ? 3 : 2;
    const int32 fork     = std::max(2, static_cast<int32>(std::lround(static_cast<float32>(height) * species.fork_share)));
    const vec3i root{draft_centre, tree_root_depth, draft_centre};

    for (int32 y = 0; y < tree_root_depth + fork; ++y) {
        const int32 above = y - tree_root_depth;
        const float32 share = static_cast<float32>(std::max(above, 0)) / static_cast<float32>(fork);
        const int32 width =
            base == 3 ? (share < 0.4F ? 3 : (share < 0.75F ? 2 : 1)) : (share < 0.6F ? 2 : 1);
        for (int32 dz = 0; dz < width; ++dz) {
            for (int32 dx = 0; dx < width; ++dx) {
                shape.put(vec3i{root.x + dx, y, root.z + dz}, shade_of(bark, above < 2 ? -1 : 0));
            }
        }
    }

    const vec3f fork_point{
        static_cast<float32>(root.x) + 0.5F, static_cast<float32>(tree_root_depth + fork) - 0.5F,
        static_cast<float32>(root.z) + 0.5F
    };
    const float32 reach  = static_cast<float32>(height) * species.branch_reach;
    const float32 radius = std::max(1.5F, static_cast<float32>(height) * species.crown_share);

    const vec3f top{fork_point.x, static_cast<float32>(tree_root_depth + height) - radius, fork_point.z};
    shape.line(fork_point, top, bark);
    shape.blob(top, radius, species.roughness, seed ^ 0x11U, leaves);

    const int32 branches = dice.roll(2, std::max(2, species.branches));
    const float32 spin   = dice.unit() * 6.2831853F;
    for (int32 b = 0; b < branches; ++b) {
        const float32 angle = spin + ((static_cast<float32>(b) + (dice.unit() * 0.5F)) *
                                      6.2831853F / static_cast<float32>(branches));
        const float32 rise  = 0.45F + (dice.unit() * 0.35F);
        const float32 len   = reach * (0.75F + (dice.unit() * 0.5F));
        const float32 out   = std::sqrt(std::max(0.0F, 1.0F - (rise * rise)));
        const vec3f start{fork_point.x, fork_point.y - (dice.unit() * 2.0F), fork_point.z};
        vec3f tip = start + (vec3f{std::cos(angle) * out, rise, std::sin(angle) * out} * len);
        const float32 crown = radius * (0.7F + (dice.unit() * 0.25F));
        tip.y = std::min(tip.y, static_cast<float32>(tree_root_depth + height) - crown);
        shape.line(start, tip, bark);
        shape.blob(tip, crown, species.roughness, seed ^ (0x100U + static_cast<uint64>(b)), leaves);
    }

    return shape.harvest(root, quarter_turns);
}

}  // namespace vw::ecs
