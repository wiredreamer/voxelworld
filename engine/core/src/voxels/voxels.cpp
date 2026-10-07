module vw.core;

import std;

namespace vw {

namespace {

constexpr auto missing_material = voxel_material{color{0xFF00FFFF}};

constexpr std::string_view missing_name = "missing";

}  // namespace

voxel_registry::voxel_registry()
    : voxel_registry(std::span<const voxel_desc>{}) {}

voxel_registry::voxel_registry(
    std::span<const voxel_desc> extra
) {
    by_value_.reserve(voxel_type_capacity);
    for (uint32 value = 0; value < voxel_type_capacity; ++value) {
        by_value_.push_back(voxel_type{
            voxel{static_cast<uint8>(value)},
            missing_name,
            missing_material,
            voxel_surface::opaque,
        });
    }

    for (const voxel_desc& desc : default_voxel_catalog) {
        add_(desc);
    }
    for (const voxel_desc& desc : extra) {
        add_(desc);
    }

    groups_.assign(voxels::groups.begin(), voxels::groups.end());
}

auto voxel_registry::add_(
    const voxel_desc& desc
) -> void {
    by_value_[desc.id.value] = voxel_type{desc.id, desc.name, desc.material, desc.surface, desc.kind};
    known_.set(desc.id.value);
    by_name_.insert_or_assign(desc.name, desc.id);
}

auto default_voxel_registry() -> const voxel_registry& {
    static const voxel_registry registry;
    return registry;
}

auto voxel_registry::of_kind(
    voxel_kind kind
) const -> voxel_set {
    voxel_set out;
    for (const voxel_type& type : by_value_) {
        if (known_[type.id.value] && type.kind == kind) {
            out.set(type.id.value);
        }
    }
    return out;
}

auto voxel_registry::find(
    std::string_view name
) const -> std::optional<voxel> {
    const auto it = by_name_.find(name);
    if (it == by_name_.end()) {
        return std::nullopt;
    }
    return it->second;
}

}  // namespace vw
