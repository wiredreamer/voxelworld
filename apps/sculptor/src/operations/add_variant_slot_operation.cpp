module vw.sculptor;

import std;

import vw.core;
import vw.asset;
import vw.ecs;
import vw.world;
import vw.platform;
import vw.gfx;

namespace vw::sculptor {

add_variant_slot_operation::add_variant_slot_operation(
    engine_type& eng, app_state& st, const add_variant_slot_params& params
)
    : engine_(&eng), state_(&st), params_(params) {}

auto add_variant_slot_operation::execute() -> void {
    if (!state_->scene.name_to_entity.contains(params_.name)) {
        return;
    }

    const auto ent = state_->scene.name_to_entity[params_.name];
    auto& world    = engine_->get_world();

    world.modify(ent).with<ecs::variant_slot_component>();

    world.system<ecs::variant_system>().modify(ent).set_name(params_.name);

    state_->file.has_unsaved_changes = true;
}

auto add_variant_slot_operation::undo() -> void {
    if (!state_->scene.name_to_entity.contains(params_.name)) {
        return;
    }

    const auto ent = state_->scene.name_to_entity[params_.name];
    engine_->get_world().modify(ent).without<ecs::variant_slot_component>();

    state_->file.has_unsaved_changes = true;
}

}  // namespace vw::sculptor
