module vw.sculptor;

import std;

import vw.core;
import vw.asset;
import vw.ecs;
import vw.world;
import vw.platform;
import vw.gfx;

namespace vw::sculptor {

create_entity_operation::create_entity_operation(
    engine_type& engine, app_state& state, const create_entity_params& params
)
    : base_operation(), engine_(&engine), state_(&state), params_(params) {}

auto create_entity_operation::execute() -> void {
    auto& world            = engine_->get_world();
    auto& hierarchy_sys = world.system<ecs::hierarchy_system>();
    auto& model_reg = world.resource<asset::model_registry>();
    auto& model_sys = world.system<ecs::model_system>();

    auto modifier = world.create()
        .with<ecs::hierarchy_component>()
        .with<ecs::transform_component>()
        .with<ecs::spatial_component>();

    std::shared_ptr<asset::model> model = nullptr;
    if (params_.with_model) {
        modifier.with<ecs::model_component>();

        const block_id fill = state_->tool.brush_for(
            params_.category, engine_->get_block_registry()
        );
        model = model_reg.create(params_.name, params_.category, params_.size);
        model->fill(voxel{fill});
    }

    if (params_.with_socket) {
        modifier.with<ecs::socket_component>();
    }

    const auto ent = modifier.get_entity();

    if (model) {
        model->set_pivot(
            vec3f{params_.size.x / 2.f, params_.size.y / 2.f, params_.size.z / 2.f}
        );
        model_sys.modify(ent).set_model(model);
    }

    if (!params_.parent_name.empty() && state_->scene.name_to_entity.contains(params_.parent_name)) {
        auto parent_ent = state_->scene.name_to_entity[params_.parent_name];
        hierarchy_sys.modify(ent).set_parent(parent_ent);
    }

    state_->scene.name_to_entity[params_.name] = ent;
    state_->scene.entity_to_name[ent]          = params_.name;

    if (state_->scene.root_name.empty()) {
        state_->scene.root_name = params_.name;
    }
    state_->scene.selected_name = params_.name;

    state_->scene.entities.push_back(ent);
    state_->file.has_unsaved_changes = true;
}

auto create_entity_operation::undo() -> void {
    auto& world = engine_->get_world();
    auto ent    = state_->scene.name_to_entity[params_.name];

    state_->scene.entity_to_name.erase(ent);
    state_->scene.name_to_entity.erase(params_.name);

    world.destroy(ent);
    std::erase(state_->scene.entities, ent);

    if (state_->scene.root_name == params_.name) {
        state_->scene.root_name = "";
    }
    state_->scene.selected_name       = "";
    state_->file.has_unsaved_changes = true;
}

}  // namespace vw::sculptor
