module vw.sculptor;

import std;

import vw.core;
import vw.ecs;
import vw.world;
import vw.platform;
import vw.gfx;

namespace vw::sculptor {

paint_voxel_operation::paint_voxel_operation(
    engine_type& eng, app_state& st, const paint_voxel_params& params
)
    : engine_(&eng), state_(&st), params_(params) {}

auto paint_voxel_operation::execute() -> void {
    auto ent = state_->scene.name_to_entity[params_.name];

    auto& world        = engine_->get_world();
    auto& model_sys = world.system<ecs::model_system>();

    auto& model_comp = world.get<ecs::model_component>(ent);
    previous_voxel_  = model_comp.get_voxel(params_.position);

    model_sys.modify(ent).set_voxel(params_.position, params_.new_voxel);
    state_->file.has_unsaved_changes = true;
}

auto paint_voxel_operation::undo() -> void {
    auto ent = state_->scene.name_to_entity[params_.name];

    auto& world        = engine_->get_world();
    auto& model_sys = world.system<ecs::model_system>();

    model_sys.modify(ent).set_voxel(params_.position, previous_voxel_);
    state_->file.has_unsaved_changes = true;
}

}  // namespace vw::sculptor
