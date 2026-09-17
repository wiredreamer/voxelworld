module vw.sculptor;

import std;

import vw.core;
import vw.asset;
import vw.ecs;
import vw.world;
import vw.platform;
import vw.gfx;

namespace vw::sculptor {

attach_model_operation::attach_model_operation(
    engine_type& engine, app_state& state, asset::model_library& library,
    attach_model_params params
)
    : engine_(&engine), state_(&state), library_(&library), params_(std::move(params)) {}

auto attach_model_operation::execute() -> void {
    const auto it = state_->scene.name_to_entity.find(params_.name);
    if (it == state_->scene.name_to_entity.end()) {
        return;
    }

    auto loaded = library_->load(params_.source);
    if (!loaded.has_value()) {
        return;
    }

    auto& world = engine_->get_world();
    world.modify(it->second).with<ecs::model_component>();
    world.system<ecs::model_system>().modify(it->second).set_model(*loaded, params_.source);

    state_->file.has_unsaved_changes = true;
}

// Объём остаётся в библиотеке: он принадлежит файлу, а не узлу, и на него
// могут смотреть другие узлы.
auto attach_model_operation::undo() -> void {
    const auto it = state_->scene.name_to_entity.find(params_.name);
    if (it == state_->scene.name_to_entity.end()) {
        return;
    }

    engine_->get_world().modify(it->second).without<ecs::model_component>();
    state_->file.has_unsaved_changes = true;
}

}  // namespace vw::sculptor
