module vw.core;

import std;

namespace vw {

namespace {

constexpr auto missing_desc = voxel_desc{
    voxel{},
    "missing",
    voxel_material{color{0xFF00FFFF}},
    voxel_surface::opaque,
};

}  // namespace

voxel_registry::voxel_registry()
    : voxel_registry(std::span<const voxel_desc>{}) {}

voxel_registry::voxel_registry(
    std::span<const voxel_desc> extra
) {
    by_slot_.reserve(default_voxel_catalog.size() + extra.size() + 1);
    by_slot_.push_back(voxel_type{
        missing_desc.id,
        missing_voxel_slot,
        missing_desc.name,
        missing_desc.material,
        missing_desc.surface,
    });

    for (const voxel_desc& desc : default_voxel_catalog) {
        add_(desc);
    }
    for (const voxel_desc& desc : extra) {
        add_(desc);
    }

    sets_.assign(default_voxel_sets.begin(), default_voxel_sets.end());
}

auto voxel_registry::set_of(
    voxel_category category
) const -> const voxel_set* {
    const auto it = std::ranges::find(sets_, category, &voxel_set::category);
    return it == sets_.end() ? nullptr : &*it;
}

auto voxel_registry::first_set(
    voxel_set_kind kind
) const -> const voxel_set* {
    const auto it = std::ranges::find(sets_, kind, &voxel_set::kind);
    return it == sets_.end() ? nullptr : &*it;
}

auto voxel_registry::add_(
    const voxel_desc& desc
) -> void {
    const voxel_slot known = slot_of(desc.id);

    if (known != missing_voxel_slot) {
        by_slot_[known.value] =
            voxel_type{desc.id, known, desc.name, desc.material, desc.surface};
        by_name_.insert_or_assign(desc.name, desc.id);
        return;
    }

    if (by_slot_.size() >= voxel_slot_capacity) {
        throw std::runtime_error{"voxel catalog does not fit the quad's ten slot bits"};
    }

    const auto slot = voxel_slot{static_cast<uint16>(by_slot_.size())};
    by_slot_.push_back(voxel_type{desc.id, slot, desc.name, desc.material, desc.surface});
    slots_.set(desc.id, slot.value);
    by_name_.insert_or_assign(desc.name, desc.id);
}

auto default_voxel_registry() -> const voxel_registry& {
    static const voxel_registry registry;
    return registry;
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
