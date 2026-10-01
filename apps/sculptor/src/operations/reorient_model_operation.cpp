module vw.sculptor;

import std;

import vw.core;
import vw.asset;
import vw.ecs;
import vw.world;
import vw.platform;
import vw.gfx;

namespace vw::sculptor {

reorient_model_operation::reorient_model_operation(
    engine_type& eng, app_state& st, const reorient_model_params& params
)
    : engine_(&eng), state_(&st), params_(params) {}

auto reorient_model_operation::execute() -> void {
    const auto ent = state_->scene.name_to_entity[params_.name];

    auto& world     = engine_->get_world();
    auto& model_reg = world.resource<asset::model_registry>();

    const auto& model_comp = world.get<ecs::model_component>(ent);
    const auto model       = model_comp.get_model();
    if (!model) {
        return;
    }

    auto turned = asset::reoriented(*model, params_.how, model_reg);

    previous_ = model;
    replace_volume(*engine_, model, std::move(turned));
    state_->volume.selection.reset();
    state_->file.has_unsaved_changes = true;
}

auto reorient_model_operation::undo() -> void {
    if (!previous_) {
        return;
    }

    const auto ent = state_->scene.name_to_entity[params_.name];

    auto& world = engine_->get_world();
    replace_volume(*engine_, world.get<ecs::model_component>(ent).get_model(), previous_);
    state_->volume.selection.reset();
    state_->file.has_unsaved_changes = true;
}

}  // namespace vw::sculptor
