module vw.sculptor;

import std;

import vw.core;
import vw.ecs;
import vw.world;
import vw.platform;
import vw.gfx;

namespace vw::sculptor {

set_pivot_operation::set_pivot_operation(
    engine_type& engine, app_state& st, const set_pivot_params& params
)
    : engine_(&engine), state_(&st), params_(params) {}

auto set_pivot_operation::execute() -> void {
    auto& world = engine_->get_world();
    const auto ent = state_->scene.name_to_entity[params_.name];

    previous_pivot_ = world.get<ecs::model_component>(ent).get_pivot();
    world.system<ecs::model_system>().modify(ent).set_pivot(params_.new_pivot);

    state_->file.dirty_models.insert(ent);
    state_->file.has_unsaved_changes = true;
}

auto set_pivot_operation::undo() -> void {
    auto& world = engine_->get_world();
    const auto ent = state_->scene.name_to_entity[params_.name];

    world.system<ecs::model_system>().modify(ent).set_pivot(previous_pivot_);

    state_->file.dirty_models.insert(ent);
    state_->file.has_unsaved_changes = true;
}

}  // namespace vw::sculptor
