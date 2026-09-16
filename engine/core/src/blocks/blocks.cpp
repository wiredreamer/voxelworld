module vw.core;

import std;

namespace vw {

namespace {

// Слот ноль занят до всякого каталога: он отвечает за блок, которого в реестре
// нет, и обязан быть заметным. Идентификатор у записи нулевой и в таблицу слотов
// она не попадает — иначе воздух забрал бы её слот себе.
constexpr auto missing_desc = block_desc{
    block_id{},
    "missing",
    block_material{color{0xFF00FFFF}},
    block_surface::opaque,
};

}  // namespace

block_registry::block_registry()
    : block_registry(std::span<const block_desc>{}) {}

block_registry::block_registry(
    std::span<const block_desc> extra
) {
    by_slot_.reserve(default_block_catalog.size() + extra.size() + 1);
    by_slot_.push_back(block_type{
        missing_desc.id,
        missing_block_slot,
        missing_desc.name,
        missing_desc.material,
        missing_desc.surface,
    });

    for (const block_desc& desc : default_block_catalog) {
        add_(desc);
    }
    for (const block_desc& desc : extra) {
        add_(desc);
    }

    sets_.assign(default_block_sets.begin(), default_block_sets.end());
}

auto block_registry::set_of(
    block_category category
) const -> const block_set* {
    const auto it = std::ranges::find(sets_, category, &block_set::category);
    return it == sets_.end() ? nullptr : &*it;
}

auto block_registry::first_set(
    block_set_kind kind
) const -> const block_set* {
    const auto it = std::ranges::find(sets_, kind, &block_set::kind);
    return it == sets_.end() ? nullptr : &*it;
}

auto block_registry::add_(
    const block_desc& desc
) -> void {
    const block_slot known = slot_of(desc.id);

    // Расширение вправе переопределить блок каталога — цвет, свечение,
    // поверхность. Слот при этом остаётся прежним: заводить второй значило бы
    // держать в палитре устройства запись, к которой уже никто не обратится.
    if (known != missing_block_slot) {
        by_slot_[known.value] =
            block_type{desc.id, known, desc.name, desc.material, desc.surface};
        by_name_.insert_or_assign(desc.name, desc.id);
        return;
    }

    if (by_slot_.size() >= block_slot_capacity) {
        throw std::runtime_error{"block catalog does not fit the quad's ten slot bits"};
    }

    const auto slot = block_slot{static_cast<uint16>(by_slot_.size())};
    by_slot_.push_back(block_type{desc.id, slot, desc.name, desc.material, desc.surface});
    slots_.set(desc.id, slot.value);
    by_name_.insert_or_assign(desc.name, desc.id);
}

auto default_block_registry() -> const block_registry& {
    static const block_registry registry;
    return registry;
}

auto block_registry::find(
    std::string_view name
) const -> std::optional<block_id> {
    const auto it = by_name_.find(name);
    if (it == by_name_.end()) {
        return std::nullopt;
    }
    return it->second;
}

}  // namespace vw
