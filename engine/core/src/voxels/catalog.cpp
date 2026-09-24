module vw.core;

import std;

namespace vw {

namespace {

[[nodiscard]] consteval auto catalog_ids_unique() -> bool {
    for (std::size_t i = 0; i < default_voxel_catalog.size(); ++i) {
        for (std::size_t j = i + 1; j < default_voxel_catalog.size(); ++j) {
            if (default_voxel_catalog[i].id == default_voxel_catalog[j].id) {
                return false;
            }
        }
    }
    return true;
}

[[nodiscard]] consteval auto groups_tile_catalog() -> bool {
    uint32 next = 1;
    for (const voxel_group& group : voxels::groups) {
        if (group.first.value != next) {
            return false;
        }
        next += group.count;
    }

    for (const voxel_desc& desc : default_voxel_catalog) {
        if (desc.id != voxels::air && desc.id.value >= next) {
            return false;
        }
    }
    return true;
}

[[nodiscard]] consteval auto catalog_colours_are_palette() -> bool {
    for (const voxel_desc& desc : default_voxel_catalog) {
        if (desc.material.clr == colors::empty) {
            continue;
        }
        if (std::ranges::find(colors::all, desc.material.clr) == colors::all.end()) {
            return false;
        }
    }
    return true;
}

[[nodiscard]] consteval auto catalog_covers_colours() -> bool {
    for (const color& clr : colors::all) {
        bool found = false;
        for (const voxel_desc& desc : default_voxel_catalog) {
            if (desc.material == voxel_material{clr}) {
                found = true;
                break;
            }
        }
        if (!found) {
            return false;
        }
    }
    return true;
}

[[nodiscard]] consteval auto catalog_materials_unique() -> bool {
    for (std::size_t i = 0; i < default_voxel_catalog.size(); ++i) {
        const voxel_desc& left = default_voxel_catalog[i];
        if (left.id == voxels::air) {
            continue;
        }

        for (std::size_t j = i + 1; j < default_voxel_catalog.size(); ++j) {
            const voxel_desc& right = default_voxel_catalog[j];
            if (right.id == voxels::air) {
                continue;
            }
            if (left.material == right.material) {
                return false;
            }
        }
    }
    return true;
}

}  // namespace

static_assert(
    default_voxel_catalog.size() <= voxel_type_capacity,
    "каталог не влезает в байт вокселя"
);
static_assert(catalog_ids_unique(), "в каталоге повторяется идентификатор");
static_assert(groups_tile_catalog(), "разделы не покрывают каталог подряд и без дыр");
static_assert(catalog_colours_are_palette(), "в каталоге цвет мимо палитры");
static_assert(catalog_covers_colours(), "каталог не покрывает палитру целиком");
static_assert(catalog_materials_unique(), "в каталоге повторяется материал");

}  // namespace vw
