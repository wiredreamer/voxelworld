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

[[nodiscard]] consteval auto groups_tile_sets() -> bool {
    for (const voxel_set& set : default_voxel_sets) {
        uint32 next = 1;
        for (const voxel_group& group : set.groups) {
            if (group.first.category() != set.category || group.first.index() != next) {
                return false;
            }
            next += group.count;
        }

        for (const voxel_desc& desc : default_voxel_catalog) {
            const bool mine = desc.id.category() == set.category && desc.id != voxels::air;
            if (mine && desc.id.index() >= next) {
                return false;
            }
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

[[nodiscard]] consteval auto palette_covers_colours() -> bool {
    for (const voxel_set& set : default_voxel_sets) {
        if (set.kind != voxel_set_kind::palette) {
            continue;
        }

        for (const color& clr : colors::all) {
            bool found = false;
            for (const voxel_desc& desc : default_voxel_catalog) {
                if (desc.id.category() == set.category && desc.material.clr == clr) {
                    found = true;
                    break;
                }
            }
            if (!found) {
                return false;
            }
        }
    }
    return true;
}

[[nodiscard]] consteval auto palette_materials_unique() -> bool {
    for (const voxel_set& set : default_voxel_sets) {
        if (set.kind != voxel_set_kind::palette) {
            continue;
        }

        for (std::size_t i = 0; i < default_voxel_catalog.size(); ++i) {
            const voxel_desc& left = default_voxel_catalog[i];
            if (left.id.category() != set.category || left.id == voxels::air) {
                continue;
            }

            for (std::size_t j = i + 1; j < default_voxel_catalog.size(); ++j) {
                const voxel_desc& right = default_voxel_catalog[j];
                if (right.id.category() != set.category || right.id == voxels::air) {
                    continue;
                }
                if (left.material == right.material) {
                    return false;
                }
            }
        }
    }
    return true;
}

}  // namespace

static_assert(
    default_voxel_catalog.size() <= voxel_slot_capacity,
    "каталог не влезает в десять бит слота в кваде"
);
static_assert(catalog_ids_unique(), "в каталоге повторяется идентификатор");
static_assert(groups_tile_sets(), "разделы не покрывают набор подряд и без дыр");
static_assert(catalog_colours_are_palette(), "в каталоге цвет мимо палитры");
static_assert(palette_covers_colours(), "палитра не покрывает палитру целиком");
static_assert(palette_materials_unique(), "в палитре повторяется материал");

}  // namespace vw
