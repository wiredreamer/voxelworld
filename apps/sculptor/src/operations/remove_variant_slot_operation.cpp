module vw.sculptor;

import std;

import vw.core;
import vw.asset;
import vw.ecs;
import vw.world;
import vw.platform;
import vw.gfx;

namespace vw::sculptor {

remove_variant_slot_operation::remove_variant_slot_operation(
    engine_type& eng, app_state& st, asset::model_library& library,
    const remove_variant_slot_params& params
)
    : engine_(&eng), state_(&st), library_(&library), params_(params) {}

auto remove_variant_slot_operation::execute() -> void {
    if (!state_->scene.name_to_entity.contains(params_.name)) {
        return;
    }

    const auto ent = state_->scene.name_to_entity[params_.name];
    auto& world    = engine_->get_world();
    if (!world.has<ecs::variant_slot_component>(ent)) {
        return;
    }

    const auto& slot = world.get<ecs::variant_slot_component>(ent);
    saved_name_      = slot.get_name();
    saved_           = slot.get_candidates();
    saved_index_     = slot.get_selected();
    saved_targets_   = slot.required_targets();
    saved_sockets_   = slot.required_sockets();

    for (const auto content : slot.get_content()) {
        world.destroy(content);
    }

    world.modify(ent).without<ecs::variant_slot_component>();

    state_->file.has_unsaved_changes = true;
}

auto remove_variant_slot_operation::undo() -> void {
    if (!state_->scene.name_to_entity.contains(params_.name)) {
        return;
    }

    const auto ent = state_->scene.name_to_entity[params_.name];
    auto& world    = engine_->get_world();

    world.modify(ent).with<ecs::variant_slot_component>();

    auto modifier = world.system<ecs::variant_system>().modify(ent);
    modifier.set_name(saved_name_);
    modifier.set_candidates(saved_);
    modifier.set_required_targets(saved_targets_);
    modifier.set_required_sockets(saved_sockets_);
    modifier.select(saved_index_);

    if (!saved_.empty()) {
        asset::vox_parser_plain parser;
        ecs::vox_deserializer deserializer{world, parser, *library_};
        static_cast<void>(deserializer.put_variant(ent, saved_index_));
    }

    state_->file.has_unsaved_changes = true;
}

}  // namespace vw::sculptor
