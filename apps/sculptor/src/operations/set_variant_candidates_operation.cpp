module vw.sculptor;

import std;

import vw.core;
import vw.asset;
import vw.ecs;
import vw.world;
import vw.platform;
import vw.gfx;

namespace vw::sculptor {

set_variant_candidates_operation::set_variant_candidates_operation(
    engine_type& eng, app_state& st, asset::model_library& library,
    const set_variant_candidates_params& params
)
    : engine_(&eng), state_(&st), library_(&library), params_(params) {}

auto set_variant_candidates_operation::apply_(
    ecs::entity ent, std::size_t index
) const -> void {
    auto& world = engine_->get_world();

    // Кандидатом бывает и поддерево, поэтому через разборщик: объём он передаст
    // системе сам.
    asset::vox_parser_plain parser;
    ecs::vox_deserializer deserializer{world, parser, *library_};
    static_cast<void>(deserializer.put_variant(ent, index));
}

auto set_variant_candidates_operation::execute() -> void {
    if (!state_->scene.name_to_entity.contains(params_.name)) {
        return;
    }

    const auto ent = state_->scene.name_to_entity[params_.name];
    auto& world    = engine_->get_world();
    if (!world.has<ecs::variant_slot_component>(ent)) {
        return;
    }

    auto& variants = world.system<ecs::variant_system>();

    const auto& slot = world.get<ecs::variant_slot_component>(ent);
    previous_        = slot.get_candidates();
    previous_index_  = slot.get_selected();

    const bool moved = variants.modify(ent).set_candidates(params_.candidates);
    state_->file.has_unsaved_changes = true;

    // Выбор переставили — значит, в сцене стоит кандидат, которого в списке уже
    // нет, и его место обязан занять сосед по-настоящему. Пустой список этого не
    // требует: объём — собственность узла, а не слота, и сносить его вместе со
    // списком не за что.
    if (moved && !params_.candidates.empty()) {
        apply_(ent, slot.get_selected());
    }
}

auto set_variant_candidates_operation::undo() -> void {
    if (!state_->scene.name_to_entity.contains(params_.name)) {
        return;
    }

    const auto ent = state_->scene.name_to_entity[params_.name];
    auto& world    = engine_->get_world();
    if (!world.has<ecs::variant_slot_component>(ent)) {
        return;
    }

    auto modifier = world.system<ecs::variant_system>().modify(ent);
    modifier.set_candidates(previous_);
    modifier.select(previous_index_);

    // Правка могла подменить объём — значит, отмена обязана вернуть прежний, а
    // не только список.
    if (!previous_.empty()) {
        apply_(ent, previous_index_);
    }

    state_->file.has_unsaved_changes = true;
}

}  // namespace vw::sculptor
