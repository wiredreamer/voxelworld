module vw.sculptor;

import std;

import vw.core;
import vw.asset;
import vw.ecs;
import vw.world;
import vw.platform;
import vw.gfx;

namespace vw::sculptor {

add_model_component_operation::add_model_component_operation(
    engine_type& engine, app_state& state, const add_model_component_params& params
)
    : engine_(&engine), state_(&state), params_(params) {}

auto add_model_component_operation::execute() -> void {
    auto& world          = engine_->get_world();
    auto& model_reg = world.resource<asset::model_registry>();
    auto& model_sys = world.system<ecs::model_system>();

    if (!state_->scene.name_to_entity.contains(params_.name)) {
        return;
    }
    const auto ent = state_->scene.name_to_entity[params_.name];

    world.modify(ent).with<ecs::model_component>();

    const voxel fill =
        state_->tool.brush_for(params_.category, engine_->get_voxel_registry());

    const auto model = model_reg.create(params_.name, params_.category, params_.size);
    model->fill(fill);

    // Вращать новый объём удобнее вокруг середины, чем вокруг угла.
    model->set_pivot(
        vec3f{params_.size.x / 2.f, params_.size.y / 2.f, params_.size.z / 2.f}
    );

    model_sys.modify(ent).set_model(model);
    state_->file.has_unsaved_changes = true;
}

auto add_model_component_operation::undo() -> void {
    auto& world          = engine_->get_world();
    auto& model_reg = world.resource<asset::model_registry>();

    if (!state_->scene.name_to_entity.contains(params_.name)) {
        return;
    }
    const auto ent = state_->scene.name_to_entity[params_.name];

    model_reg.erase(params_.name);
    world.modify(ent).without<ecs::model_component>();
    state_->file.has_unsaved_changes = true;
}

}  // namespace vw::sculptor
