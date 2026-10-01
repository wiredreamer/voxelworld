module vw.sculptor;

import std;

import vw.core;
import vw.asset;
import vw.ecs;
import vw.world;
import vw.platform;
import vw.gfx;

namespace vw::sculptor {

namespace {

auto volume_of(gfx::engine& engine, app_state& state, const std::string& name)
    -> std::shared_ptr<asset::model> {
    const auto it = state.scene.name_to_entity.find(name);
    if (it == state.scene.name_to_entity.end()) {
        return nullptr;
    }

    auto& world = engine.get_world();
    if (!world.has<ecs::model_component>(it->second)) {
        return nullptr;
    }

    return world.get<ecs::model_component>(it->second).get_model();
}

auto put_volume(
    gfx::engine& engine, app_state& state, const std::string& name,
    std::shared_ptr<asset::model> volume
) -> void {
    replace_volume(engine, volume_of(engine, state, name), std::move(volume));
    state.file.has_unsaved_changes = true;
}

}  // namespace

erase_voxels_operation::erase_voxels_operation(
    engine_type& eng, app_state& st, const erase_voxels_params& params
)
    : engine_(&eng), state_(&st), params_(params) {}

auto erase_voxels_operation::execute() -> void {
    const auto model = volume_of(*engine_, *state_, params_.name);
    if (!model) {
        return;
    }

    auto& model_reg = engine_->get_world().resource<asset::model_registry>();

    previous_ = model;
    put_volume(
        *engine_, *state_, params_.name, asset::erased(*model, params_.region, model_reg)
    );
}

auto erase_voxels_operation::undo() -> void {
    if (!previous_) {
        return;
    }

    put_volume(*engine_, *state_, params_.name, previous_);
}

fill_voxels_operation::fill_voxels_operation(
    engine_type& eng, app_state& st, const fill_voxels_params& params
)
    : engine_(&eng), state_(&st), params_(params) {}

auto fill_voxels_operation::execute() -> void {
    const auto model = volume_of(*engine_, *state_, params_.name);
    if (!model) {
        return;
    }

    auto& model_reg = engine_->get_world().resource<asset::model_registry>();

    previous_ = model;
    put_volume(
        *engine_, *state_, params_.name,
        asset::filled(*model, params_.region, params_.value, params_.scope, model_reg)
    );
}

auto fill_voxels_operation::undo() -> void {
    if (!previous_) {
        return;
    }

    put_volume(*engine_, *state_, params_.name, previous_);
}

paste_voxels_operation::paste_voxels_operation(
    engine_type& eng, app_state& st, paste_voxels_params params
)
    : engine_(&eng), state_(&st), params_(std::move(params)) {}

auto paste_voxels_operation::execute() -> void {
    const auto model = volume_of(*engine_, *state_, params_.name);
    if (!model) {
        return;
    }

    auto& model_reg = engine_->get_world().resource<asset::model_registry>();

    previous_ = model;
    put_volume(
        *engine_, *state_, params_.name,
        asset::pasted(*model, params_.clip, params_.origin, params_.mode, model_reg)
    );
}

auto paste_voxels_operation::undo() -> void {
    if (!previous_) {
        return;
    }

    put_volume(*engine_, *state_, params_.name, previous_);
}

}  // namespace vw::sculptor
