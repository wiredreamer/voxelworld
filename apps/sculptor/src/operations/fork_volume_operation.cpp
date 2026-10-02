module vw.sculptor;

import std;

import vw.core;
import vw.asset;
import vw.ecs;
import vw.world;
import vw.platform;
import vw.gfx;

namespace vw::sculptor {

fork_volume_operation::fork_volume_operation(
    engine_type& eng, app_state& st, const fork_volume_params& params
)
    : engine_(&eng), state_(&st), params_(params) {}

auto fork_volume_operation::execute() -> void {
    const auto node = state_->scene.name_to_entity.find(params_.name);
    if (node == state_->scene.name_to_entity.end()) {
        return;
    }

    auto& world = engine_->get_world();
    if (!world.has<ecs::model_component>(node->second)) {
        return;
    }

    const auto& model_comp = world.get<ecs::model_component>(node->second);
    const auto shared      = model_comp.get_model();
    if (!shared) {
        return;
    }

    shared_before_ = shared;
    source_before_ = model_comp.get_source();

    auto own = asset::reoriented(
        *shared,
        asset::rotated_orientation(asset::voxel_axis::y, 0),
        world.resource<asset::model_registry>()
    );

    put_(std::move(own), params_.source);
}

auto fork_volume_operation::undo() -> void {
    if (!shared_before_) {
        return;
    }
    put_(shared_before_, source_before_);
}

auto fork_volume_operation::put_(
    std::shared_ptr<asset::model> volume, const asset::asset_ref& source
) const -> void {
    const auto node = state_->scene.name_to_entity.find(params_.name);
    if (node == state_->scene.name_to_entity.end()) {
        return;
    }

    engine_->get_world()
        .system<ecs::model_system>()
        .modify(node->second)
        .set_model(std::move(volume), source);

    state_->file.dirty_models.insert(node->second);
    state_->volume.selection.reset();
    state_->file.has_unsaved_changes = true;
}

}  // namespace vw::sculptor
