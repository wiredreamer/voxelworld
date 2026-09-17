module vw.sculptor;

import std;

import vw.core;
import vw.ecs;
import vw.world;
import vw.platform;
import vw.gfx;

namespace vw::sculptor {

set_rig_operation::set_rig_operation(
    engine_type& engine, app_state& st, const set_rig_params& params
)
    : engine_(&engine), state_(&st), params_(params) {}

auto set_rig_operation::execute() -> void {
    auto& world = engine_->get_world();

    const auto it = state_->scene.name_to_entity.find(state_->scene.root_name);
    if (it == state_->scene.name_to_entity.end()) {
        return;
    }

    previous_rig_.clear();
    if (world.has<ecs::rig_component>(it->second)) {
        previous_rig_ = world.get<ecs::rig_component>(it->second).get_name();
    }

    apply_(params_.rig_name);
}

auto set_rig_operation::undo() -> void {
    apply_(previous_rig_);
}

auto set_rig_operation::apply_(
    const std::string& rig_name
) const -> void {
    auto& world = engine_->get_world();

    const auto it = state_->scene.name_to_entity.find(state_->scene.root_name);
    if (it == state_->scene.name_to_entity.end()) {
        return;
    }

    world.modify(it->second).with<ecs::rig_component>();
    world.system<ecs::animation_system>().modify_rig(it->second).set_name(rig_name);

    state_->file.has_unsaved_changes = true;
}

}  // namespace vw::sculptor
