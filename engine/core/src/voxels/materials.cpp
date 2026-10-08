module vw.core;

import std;

namespace vw {

material_table::material_table(
    const voxel_registry& registry
) {
    rows_.reserve(material_capacity);
    rows_.push_back(material_type{});

    for (const voxel_type& type : registry.all()) {
        const material_type wanted{
            .glow     = type.material.glow,
            .emission = type.material.emission,
            .sways    = type.kind == voxel_kind::leaf,
        };

        auto row = std::ranges::find(rows_, wanted);
        if (row == rows_.end()) {
            if (rows_.size() == static_cast<std::size_t>(material_capacity)) {
                throw std::length_error("the voxel catalog needs more materials than a byte holds");
            }
            rows_.push_back(wanted);
            row = rows_.end() - 1;
        }

        by_voxel_[type.id.value] = material{static_cast<uint8>(row - rows_.begin())};
    }
}

auto default_material_table() -> const material_table& {
    static const material_table table{default_voxel_registry()};
    return table;
}

}  // namespace vw
