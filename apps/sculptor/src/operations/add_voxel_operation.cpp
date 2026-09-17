module vw.sculptor;

import std;

import vw.core;
import vw.ecs;
import vw.world;
import vw.platform;
import vw.gfx;

namespace vw::sculptor {

add_voxel_operation::add_voxel_operation(
    engine_type& eng, app_state& st, const add_voxel_params& params
)
    : engine_(&eng), state_(&st), params_(params) {}

auto add_voxel_operation::execute() -> void {
    const auto ent = state_->scene.name_to_entity[params_.name];

    auto& world        = engine_->get_world();
    auto& model_sys = world.system<ecs::model_system>();

    model_sys.modify(ent).set_voxel(params_.position, params_.new_voxel);
    state_->file.has_unsaved_changes = true;
}

auto add_voxel_operation::undo() -> void {
    const auto ent = state_->scene.name_to_entity[params_.name];

    auto& world        = engine_->get_world();
    auto& model_sys = world.system<ecs::model_system>();

    model_sys.modify(ent).set_voxel(params_.position, voxels::air);
    state_->file.has_unsaved_changes = true;
}

}  // namespace vw::sculptor
