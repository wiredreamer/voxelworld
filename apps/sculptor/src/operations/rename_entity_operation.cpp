module vw.sculptor;

import std;

import vw.core;
import vw.ecs;
import vw.world;
import vw.platform;
import vw.gfx;

namespace vw::sculptor {

rename_entity_operation::rename_entity_operation(
    engine_type& engine, app_state& st, const rename_entity_params& params
)
    : engine_(&engine), state_(&st), params_(params) {}

auto rename_entity_operation::execute() -> void {
    rename_(params_.name, params_.new_name);
}

auto rename_entity_operation::undo() -> void {
    rename_(params_.new_name, params_.name);
}

auto rename_entity_operation::rename_(
    const std::string& from, const std::string& to
) const -> void {
    auto& scene = state_->scene;

    const auto found = scene.name_to_entity.find(from);
    if (found == scene.name_to_entity.end() || scene.name_to_entity.contains(to)) {
        return;
    }

    const ecs::entity ent = found->second;
    scene.name_to_entity.erase(found);
    scene.name_to_entity.emplace(to, ent);
    scene.entity_to_name[ent] = to;

    if (scene.selected_name == from) {
        scene.selected_name = to;
    }
    if (scene.root_name == from) {
        scene.root_name = to;
    }
    if (scene.hidden_nodes.erase(from) > 0) {
        scene.hidden_nodes.insert(to);
    }

    for (edit_context& context : state_->ctx.stack) {
        if (context.node_name == from) {
            context.node_name = to;
        }
    }

    if (state_->volume.selection && state_->volume.selection->node_name == from) {
        state_->volume.selection->node_name = to;
    }
    if (state_->paste.node_name == from) {
        state_->paste.node_name = to;
    }

    if (auto saved = state_->anim.saved_transforms.extract(from); !saved.empty()) {
        saved.key() = to;
        state_->anim.saved_transforms.insert(std::move(saved));
    }

    state_->sockets.rename_previews_for(from, to);

    state_->file.has_unsaved_changes = true;
}

}  // namespace vw::sculptor
