module vw.world;

import std;

import vw.core;
import vw.asset;
import vw.ecs;

namespace vw::ecs {

variant_system::variant_system(
    world& w
)
    : world_(&w) {}

variant_system::variant_modifier::variant_modifier(
    variant_slot_component* component
)
    : component_(component) {}

auto variant_system::variant_modifier::set_name(
    std::string name
) const -> void {
    component_->name_ = std::move(name);
}

auto variant_system::variant_modifier::add_candidate(
    asset::asset_ref ref
) const -> void {
    component_->candidates_.push_back(std::move(ref));
}

auto variant_system::variant_modifier::set_candidates(
    std::vector<asset::asset_ref> candidates
) const -> void {
    component_->candidates_ = std::move(candidates);
}

auto variant_system::variant_modifier::set_required_targets(
    std::vector<std::string> targets
) const -> void {
    component_->required_targets_ = std::move(targets);
}

auto variant_system::variant_modifier::set_required_sockets(
    std::vector<std::string> sockets
) const -> void {
    component_->required_sockets_ = std::move(sockets);
}

auto variant_system::variant_modifier::select(
    std::size_t index
) const -> void {
    component_->selected_ = index;
}

auto variant_system::modify(
    entity ent
) -> variant_modifier {
    return variant_modifier(&world_->registry().get<variant_slot_component>(ent));
}

auto variant_system::apply(
    entity ent, asset::model_library& library, std::size_t index
) -> std::expected<void, variant_error> {
    auto& registry = world_->registry();
    if (!registry.has<variant_slot_component>(ent)) {
        return std::unexpected(variant_error::no_slot);
    }

    auto& slot = registry.get<variant_slot_component>(ent);
    if (index >= slot.get_candidates().size()) {
        return std::unexpected(variant_error::out_of_range);
    }

    const auto& ref = slot.get_candidates()[index];
    if (ref.extension() != ".voxm") {
        return std::unexpected(variant_error::unsupported_kind);
    }

    auto loaded = library.load(ref);
    if (!loaded.has_value()) {
        return std::unexpected(variant_error::load_failed);
    }

    // Компонент объёма мог и не стоять: узел-слот законно заводят пустым, а
    // объём приезжает с первым выбранным кандидатом.
    world_->modify(ent).with<model_component>();
    world_->system<model_system>().modify(ent).set_model(*loaded, ref);

    slot.selected_ = index;

    return {};
}

auto variant_system::update(
    float32 /*dt*/
) -> void {}

}  // namespace vw::ecs
