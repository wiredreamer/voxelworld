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

constexpr int32 max_paste_reach_in_voxels = 64;

auto floor_half(int32 doubled) -> int32 {
    return doubled >= 0 ? doubled / 2 : -((1 - doubled) / 2);
}

}  // namespace

clipboard_service::clipboard_service(
    engine_type& eng, app_state& state, operation_manager& op_manager
)
    : engine_(&eng), state_(&state), op_manager_(&op_manager) {}

auto clipboard_service::volume_entity_(
    const std::string& node_name
) const -> ecs::entity {
    const auto it = state_->scene.name_to_entity.find(node_name);
    if (it == state_->scene.name_to_entity.end()) {
        return ecs::invalid_entity;
    }

    return engine_->get_world().has<ecs::model_component>(it->second) ? it->second
                                                                       : ecs::invalid_entity;
}

auto clipboard_service::edited_volume_() const -> std::shared_ptr<asset::model> {
    const auto ent = volume_entity_(state_->edited_node());
    if (!ent.is_valid()) {
        return nullptr;
    }

    return engine_->get_world().get<ecs::model_component>(ent).get_model();
}

auto clipboard_service::select_all() -> void {
    const auto model = edited_volume_();
    if (!model || !state_->ctx.allows_volume_edit()) {
        return;
    }

    const auto size = model->size();
    const auto whole = asset::voxel_bounds{
        .min = vec3i{0, 0, 0},
        .max = vec3i{size.x - 1, size.y - 1, size.z - 1},
    };

    state_->volume.selection = volume_selection{
        .box         = asset::occupied_bounds(*model).value_or(whole),
        .node_name   = state_->edited_node(),
        .volume_size = size,
    };
}

auto clipboard_service::deselect() -> void {
    state_->volume.selection.reset();
}

auto clipboard_service::copy() -> void {
    const auto model = edited_volume_();
    if (!model || !state_->volume.selection) {
        return;
    }

    auto clip = asset::copied(*model, state_->volume.selection->box);
    if (!clip.empty()) {
        state_->clipboard.clip = std::move(clip);
    }
}

auto clipboard_service::erase() -> void {
    if (!edited_volume_() || !state_->volume.selection || !state_->ctx.allows_volume_edit()) {
        return;
    }

    op_manager_->execute(
        std::make_unique<erase_voxels_operation>(
            *engine_, *state_,
            erase_voxels_params{
                .name = state_->edited_node(), .region = state_->volume.selection->box
            }
        )
    );
}

auto clipboard_service::fill(
    asset::fill_scope scope
) -> void {
    if (!edited_volume_() || !state_->volume.selection || !state_->ctx.allows_volume_edit()) {
        return;
    }

    op_manager_->execute(
        std::make_unique<fill_voxels_operation>(
            *engine_, *state_,
            fill_voxels_params{
                .name   = state_->edited_node(),
                .region = state_->volume.selection->box,
                .value  = matter{state_->tool.selected_voxel, state_->tool.selected_material},
                .scope  = scope,
            }
        )
    );
}

auto clipboard_service::cut() -> void {
    copy();
    erase();
}

auto clipboard_service::begin_paste() -> void {
    const auto model = edited_volume_();
    if (!model || !state_->ctx.allows_volume_edit() || state_->clipboard.clip.empty()) {
        return;
    }

    auto& paste = state_->paste;

    paste.clip          = state_->clipboard.clip;
    paste.origin        = asset::paste_origin(*model, paste.clip);
    paste.node_name     = state_->edited_node();
    paste.base          = model;
    paste.preview_stale = true;

    state_->tool.tool_before_paste = state_->tool.selected_tool;
    state_->ctx.enter(edit_context::paste(paste.node_name));
}

auto clipboard_service::reorient_paste(
    const asset::voxel_orientation& how
) -> void {
    auto& paste = state_->paste;
    if (!paste.active()) {
        return;
    }

    auto turned = asset::reoriented(paste.clip, how);

    for (std::size_t i = 0; i < 3; ++i) {
        paste.origin[i] =
            floor_half((2 * paste.origin[i]) + paste.clip.size[i] - turned.size[i]);
    }

    paste.clip          = std::move(turned);
    paste.preview_stale = true;
}

auto clipboard_service::leave_paste_() -> void {
    auto& paste = state_->paste;

    if (paste.active()) {
        const auto ent = volume_entity_(paste.node_name);
        if (ent.is_valid()) {
            replace_volume(
                *engine_, engine_->get_world().get<ecs::model_component>(ent).get_model(),
                paste.base
            );
        }
    }

    paste.base.reset();
    paste.node_name.clear();
    paste.clip          = asset::voxel_clip{};
    paste.preview_stale = false;

    auto& stack = state_->ctx.stack;
    if (stack.empty() || stack.back().kind != edit_kind::paste) {
        return;
    }

    while (!stack.empty() && stack.back().kind == edit_kind::paste) {
        stack.pop_back();
    }

    if (state_->ctx.allows_tool(state_->tool.tool_before_paste)) {
        state_->tool.selected_tool = state_->tool.tool_before_paste;
    }
}

auto clipboard_service::apply_paste() -> void {
    auto& paste = state_->paste;
    if (!paste.active() || !state_->ctx.in_paste()) {
        return;
    }

    paste_voxels_params params{
        .name   = paste.node_name,
        .clip   = std::move(paste.clip),
        .origin = paste.origin,
        .mode   = paste.mode,
    };

    leave_paste_();

    if (!volume_entity_(params.name).is_valid()) {
        return;
    }

    op_manager_->execute(
        std::make_unique<paste_voxels_operation>(*engine_, *state_, std::move(params))
    );
}

auto clipboard_service::cancel_paste() -> void {
    leave_paste_();
}

auto clipboard_service::drop_stale_selection_() -> void {
    auto& selection = state_->volume.selection;
    if (!selection) {
        return;
    }

    const auto model = edited_volume_();
    const bool holds = model && state_->ctx.shows_volume() &&
                       selection->node_name == state_->edited_node() &&
                       selection->volume_size == model->size();
    if (!holds) {
        selection.reset();
    }
}

auto clipboard_service::sync() -> void {
    auto& paste = state_->paste;

    const bool lost = paste.active() && (!volume_entity_(paste.node_name).is_valid() ||
                                         state_->ctx.node_name() != paste.node_name);
    if (paste.active() != state_->ctx.in_paste() || lost) {
        leave_paste_();
    }

    if (!paste.active()) {
        drop_stale_selection_();
        return;
    }

    if (!paste.preview_stale) {
        return;
    }
    paste.preview_stale = false;

    const auto base_size = paste.base->size();
    for (std::size_t i = 0; i < 3; ++i) {
        paste.origin[i] = std::clamp(
            paste.origin[i], -paste.clip.size[i] - max_paste_reach_in_voxels,
            base_size[i] + max_paste_reach_in_voxels
        );
    }

    auto& world     = engine_->get_world();
    auto& model_reg = world.resource<asset::model_registry>();

    replace_volume(
        *engine_, world.get<ecs::model_component>(volume_entity_(paste.node_name)).get_model(),
        asset::pasted(*paste.base, paste.clip, paste.origin, paste.mode, model_reg)
    );
}

}  // namespace vw::sculptor
