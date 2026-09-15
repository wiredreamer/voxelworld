module vw.sculptor;

import std;

import vw.core;
import vw.asset;
import vw.ecs;
import vw.world;
import vw.platform;
import vw.gfx;

namespace vw::sculptor {

trim_model_operation::trim_model_operation(
    engine_type& eng, app_state& st, const trim_model_params& params
)
    : engine_(&eng), state_(&st), params_(params) {}

auto trim_model_operation::execute() -> void {
    const auto ent = state_->scene.name_to_entity[params_.name];

    auto& world     = engine_->get_world();
    auto& model_reg = world.resource<asset::model_registry>();

    const auto& model_comp = world.get<ecs::model_component>(ent);
    const auto model       = model_comp.get_model();
    if (!model) {
        return;
    }

    auto cut = asset::trimmed(*model, model_reg);
    if (!cut) {
        return;
    }

    previous_ = model;
    world.system<ecs::model_system>().modify(ent).set_model(std::move(cut));
    state_->file.has_unsaved_changes = true;
}

auto trim_model_operation::undo() -> void {
    if (!previous_) {
        return;
    }

    const auto ent = state_->scene.name_to_entity[params_.name];

    auto& world = engine_->get_world();
    world.system<ecs::model_system>().modify(ent).set_model(previous_);
    state_->file.has_unsaved_changes = true;
}

}  // namespace vw::sculptor
