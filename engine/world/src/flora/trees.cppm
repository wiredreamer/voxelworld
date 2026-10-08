export module vw.world:flora.trees;
import :terrain.biomes;

import std;

import vw.core;

export namespace vw::ecs {

inline constexpr int32 tree_root_depth = 2;

struct plant_voxel {
    vec3i offset{};
    matter look;
};

// см. docs/world.md#деревья
struct plant_shape {
    std::vector<plant_voxel> voxels;
    vec3i min{};
    vec3i max{};
};

[[nodiscard]] auto grow_tree(const tree_species& species, uint64 seed, uint8 quarter_turns, matter bark,
                             matter leaves) -> plant_shape;

}  // namespace vw::ecs
