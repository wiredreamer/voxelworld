module vw.sculptor;

import std;
import vw.core;
import vw.asset;
import vw.ecs;
import vw.world;
import vw.gfx;

namespace vw::sculptor {

set_fsm_operation::set_fsm_operation(
    app_state& st, set_fsm_params params
)
    : state_(&st), params_(std::move(params)) {}

auto set_fsm_operation::execute() -> void {
    state_->fsm.data                 = params_.after;
    state_->fsm.has_unsaved_changes = true;
}

auto set_fsm_operation::undo() -> void {
    state_->fsm.data                 = params_.before;
    state_->fsm.has_unsaved_changes = true;
}

set_machines_operation::set_machines_operation(
    engine_type& engine, app_state& st, set_machines_params params
)
    : engine_(&engine), state_(&st), params_(std::move(params)) {}

auto set_machines_operation::execute() -> void {
    const auto ent = state_->scene.name_to_entity[state_->scene.root_name];
    auto& world    = engine_->get_world();

    if (world.has<ecs::animation_machines_component>(ent)) {
        const auto sources = world.get<ecs::animation_machines_component>(ent).get_sources();
        previous_.assign(sources.begin(), sources.end());
    } else {
        previous_.clear();
    }

    apply_(params_.machines);
}

auto set_machines_operation::undo() -> void {
    apply_(previous_);
}

auto set_machines_operation::apply_(
    const std::vector<asset::asset_ref>& machines
) -> void {
    auto& world    = engine_->get_world();
    const auto ent = state_->scene.name_to_entity[state_->scene.root_name];

    if (machines.empty()) {
        if (world.has<ecs::animation_machines_component>(ent)) {
            world.modify(ent).without<ecs::animation_machines_component>();
        }
    } else {
        if (!world.has<ecs::animation_machines_component>(ent)) {
            world.modify(ent).with<ecs::animation_machines_component>();
        }
        world.system<ecs::animation_fsm_system>().modify_machines(ent).set(machines);
    }

    state_->file.has_unsaved_changes = true;
}

}  // namespace vw::sculptor
