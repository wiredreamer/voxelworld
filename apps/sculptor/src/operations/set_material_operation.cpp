module vw.sculptor;

import std;

import vw.core;
import vw.ecs;
import vw.world;
import vw.platform;
import vw.gfx;

namespace vw::sculptor {

set_material_operation::set_material_operation(
    engine_type& eng, app_state& st, const set_material_params& params
)
    : engine_(&eng), state_(&st), params_(params) {}

auto set_material_operation::execute() -> void {
    const auto ent = state_->scene.name_to_entity[params_.name];

    auto& world     = engine_->get_world();
    auto& model_sys = world.system<ecs::model_system>();

    previous_ = world.get<ecs::model_component>(ent).get_model()->get_matter(params_.position);

    model_sys.modify(ent).set_voxel(params_.position, matter{previous_.color, params_.made_of});
    state_->file.has_unsaved_changes = true;
}

auto set_material_operation::undo() -> void {
    const auto ent = state_->scene.name_to_entity[params_.name];

    auto& world     = engine_->get_world();
    auto& model_sys = world.system<ecs::model_system>();

    model_sys.modify(ent).set_voxel(params_.position, previous_);
    state_->file.has_unsaved_changes = true;
}

}  // namespace vw::sculptor
