module vw.world;

import std;
import vw.core;
import vw.asset;

namespace vw::ecs {

namespace {

constexpr int32 chunk_side         = 64;
constexpr float32 trunk_gap_voxels = 5.0F;
constexpr float32 cave_root_margin = 0.15F;

auto placement_hash(uint32 seed, vec2i cell, uint64 salt) -> uint64 {
    uint64 h = (static_cast<uint64>(seed) * 0xD6E8FEB86659FD93ULL) ^ salt ^
               (static_cast<uint64>(static_cast<uint32>(cell.x)) * 0x9E3779B97F4A7C15ULL) ^
               (static_cast<uint64>(static_cast<uint32>(cell.y)) * 0xC2B2AE3D27D4EB4FULL);
    h ^= h >> 30U;
    h *= 0xBF58476D1CE4E5B9ULL;
    h ^= h >> 27U;
    h *= 0x94D049BB133111EBULL;
    return h ^ (h >> 31U);
}

auto unit_of(uint64 h, uint32 lane) -> float64 {
    return static_cast<float64>((h >> (lane * 16U)) & 0xFFFFU) / 65535.0;
}

auto floor_div(int32 a, int32 b) -> int32 {
    return a >= 0 ? a / b : ((a - b) + 1) / b;
}

}  // namespace

auto perlin_terrain_generator::cave_near_root_(
    int32 x, int32 surface, int32 z
) const -> bool {
    if (!params_.caves) {
        return false;
    }
    for (int32 depth = 0; depth <= tree_root_depth; ++depth) {
        const int32 y       = surface - depth;
        const float32 leak  = cave_entrance_leak_at(x, z);
        const float32 field = std::max(cave_field_at(x, y, z, depth), leak * params_.cave_entrance_field);
        if (field <= 0.0F) {
            continue;
        }
        if (cave_openness_at(x, y, z, field, surface, leak) > -cave_root_margin) {
            return true;
        }
    }
    return false;
}

// см. docs/world.md#расстановка
auto perlin_terrain_generator::plant_candidate_(
    vec2i cell
) const -> std::optional<plant_candidate> {
    constexpr int32 side = plant_cell_voxels;

    const uint64 h = placement_hash(params_.seed, cell, 0x7265736964ULL);
    const int32 x  = (cell.x * side) + static_cast<int32>(h % side);
    const int32 z  = (cell.y * side) + static_cast<int32>((h >> 8U) % side);

    const column_shape shape = shape_at(x, z);
    if (shape.is_void()) {
        return std::nullopt;
    }

    const forest& woods = woods_of(biomes_[shape.biome]);
    const float64 f     = woods.patch_frequency;
    const float64 n =
        noise_.fractal((static_cast<float64>(x) * f) + 3301.0, (static_cast<float64>(z) * f) - 3301.0, 2);
    const float64 patch = 0.5 + (0.5 * std::tanh(n * 2.5));
    const bool in_woods = patch > 1.0 - static_cast<float64>(woods.density);

    const float64 tree_chance = in_woods ? woods.fill : woods.lone_trees;
    if (unit_of(h, 1) >= tree_chance) {
        return std::nullopt;
    }

    const float32 slope = slope_between(
        shape_at(x - 1, z).height, shape.height, shape_at(x + 1, z).height, shape_at(x, z - 1).height,
        shape_at(x, z + 1).height
    );
    if (!paint_at_(x, z, shape, slope).fertile || cave_near_root_(x, shape.surface, z)) {
        return std::nullopt;
    }

    return plant_candidate{
        .root  = {x, shape.surface + 1, z},
        .id    = (placement_hash(params_.seed, cell, 0x74726565ULL) | 1U),
        .biome = shape.biome,
        .turns = static_cast<uint8>((h >> 48U) & 3U),
    };
}

auto perlin_terrain_generator::grow_plant_(
    const plant_candidate& candidate
) const -> plant_shape {
    const forest& woods = woods_of(biomes_[candidate.biome]);

    column_facts facts{
        .noise   = &noise_,
        .x       = static_cast<float64>(candidate.root.x),
        .z       = static_cast<float64>(candidate.root.z),
        .surface = candidate.root.y - 1,
    };

    return grow_tree(woods.tree, candidate.id, candidate.turns, tone_at(facts, woods.tree.bark, "bark", 991.0),
                     tone_at(facts, woods.tree.leaves, "leaves", 997.0));
}

auto perlin_terrain_generator::reach_of_(
    const plant_candidate& candidate
) const -> int32 {
    const auto& tree = woods_of(biomes_[candidate.biome]).tree;
    return static_cast<int32>(std::ceil(
               static_cast<float32>(tree.max_height) * ((tree.branch_reach * 1.25F) + (tree.crown_share * 1.15F))
           )) +
           4;
}

auto perlin_terrain_generator::grown_plant_(
    const plant_candidate& candidate
) const -> std::shared_ptr<const plant_shape> {
    constexpr std::size_t kept = 8192;
    {
        std::scoped_lock lock(grown_mutex_);
        if (const auto it = grown_.find(candidate.id); it != grown_.end()) {
            return it->second;
        }
    }
    auto shape = std::make_shared<const plant_shape>(grow_plant_(candidate));
    std::scoped_lock lock(grown_mutex_);
    if (grown_.size() >= kept) {
        grown_.clear();
    }
    grown_.emplace(candidate.id, shape);
    return shape;
}

// см. docs/world.md#расстановка
auto perlin_terrain_generator::plants_near_(
    int32 cx, int32 cz
) const -> std::vector<placed_plant> {
    constexpr int32 s    = chunk_side;
    constexpr int32 side = plant_cell_voxels;

    int32 margin = side;
    for (std::size_t b = 0; b < biomes_.size(); ++b) {
        margin = std::max(margin, reach_of_({.biome = static_cast<uint8>(b)}));
    }

    const int32 gx0   = floor_div((cx * s) - margin, side) - 1;
    const int32 gz0   = floor_div((cz * s) - margin, side) - 1;
    const int32 gx1   = floor_div((cx * s) + s + margin, side) + 1;
    const int32 gz1   = floor_div((cz * s) + s + margin, side) + 1;
    const int32 width = gx1 - gx0 + 1;

    std::vector<std::optional<plant_candidate>> candidates;
    candidates.reserve(static_cast<std::size_t>(width * (gz1 - gz0 + 1)));
    for (int32 gz = gz0; gz <= gz1; ++gz) {
        for (int32 gx = gx0; gx <= gx1; ++gx) {
            candidates.push_back(plant_candidate_({gx, gz}));
        }
    }
    const auto candidate_at = [&](int32 gx, int32 gz) -> const std::optional<plant_candidate>& {
        return candidates[static_cast<std::size_t>(((gz - gz0) * width) + (gx - gx0))];
    };

    std::vector<placed_plant> out;
    for (int32 gz = gz0 + 1; gz < gz1; ++gz) {
        for (int32 gx = gx0 + 1; gx < gx1; ++gx) {
            const auto& candidate = candidate_at(gx, gz);
            if (!candidate) {
                continue;
            }

            const int32 reach = reach_of_(*candidate);
            if (candidate->root.x + reach < cx * s || candidate->root.x - reach >= (cx * s) + s ||
                candidate->root.z + reach < cz * s || candidate->root.z - reach >= (cz * s) + s) {
                continue;
            }

            bool crowded = false;
            for (int32 dz = -1; dz <= 1 && !crowded; ++dz) {
                for (int32 dx = -1; dx <= 1 && !crowded; ++dx) {
                    if (dx == 0 && dz == 0) {
                        continue;
                    }
                    const auto& other = candidate_at(gx + dx, gz + dz);
                    if (!other || other->id < candidate->id) {
                        continue;
                    }
                    const auto ox = static_cast<float32>(other->root.x - candidate->root.x);
                    const auto oz = static_cast<float32>(other->root.z - candidate->root.z);
                    crowded       = ((ox * ox) + (oz * oz)) < trunk_gap_voxels * trunk_gap_voxels;
                }
            }
            if (crowded) {
                continue;
            }

            auto shape     = grown_plant_(*candidate);
            const vec3i lo = candidate->root + shape->min;
            const vec3i hi = candidate->root + shape->max;
            if (hi.x <= cx * s || lo.x >= (cx * s) + s || hi.z <= cz * s || lo.z >= (cz * s) + s) {
                continue;
            }
            out.push_back({.id = candidate->id, .root = candidate->root, .shape = std::move(shape)});
        }
    }
    std::ranges::sort(out, {}, &placed_plant::id);
    return out;
}

auto perlin_terrain_generator::footing_of_(
    int32 cx, int32 cz, const column_profile& profile, std::span<const placed_plant> plants
) -> plant_footing {
    constexpr int32 s = chunk_side;

    plant_footing footing;
    for (const placed_plant& plant : plants) {
        for (const plant_voxel& v : plant.shape->voxels) {
            const vec3i at = plant.root + v.offset;
            const int32 x  = at.x - (cx * s);
            const int32 z  = at.z - (cz * s);
            if (x < 0 || z < 0 || x >= s || z >= s) {
                continue;
            }
            if (at.y == profile.surface[column_profile::ring_index(x, z)] + 1) {
                footing.set(static_cast<std::size_t>((x * s) + z));
            }
        }
    }
    return footing;
}

// см. docs/world.md#хозяин-клетки
auto perlin_terrain_generator::plant_chunk_(
    asset::model_writer& writer, const asset::model& voxels, vec3i base, std::span<const placed_plant> plants
) -> void {
    constexpr int32 s = chunk_side;

    for (const bool wood_pass : {false, true}) {
        for (const placed_plant& plant : plants) {
            const vec3i lo = plant.root + plant.shape->min - base;
            const vec3i hi = plant.root + plant.shape->max - base;
            if (hi.x <= 0 || hi.y <= 0 || hi.z <= 0 || lo.x >= s || lo.y >= s || lo.z >= s) {
                continue;
            }
            for (const plant_voxel& v : plant.shape->voxels) {
                if (voxels::bark.contains(v.look) != wood_pass) {
                    continue;
                }
                const vec3i at = plant.root + v.offset - base;
                if (at.x < 0 || at.y < 0 || at.z < 0 || at.x >= s || at.y >= s || at.z >= s) {
                    continue;
                }
                const voxel here = voxels.get_voxel(at.x, at.y, at.z);
                const bool open  = here.is_empty() || voxels::leaves.contains(here) ||
                                  (wood_pass && voxels::bark.contains(here));
                if (open) {
                    writer.set(at, v.look);
                }
            }
        }
    }
}

}  // namespace vw::ecs
