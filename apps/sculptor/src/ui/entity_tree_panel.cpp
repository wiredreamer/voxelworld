module;

#include <imgui.h>

module vw.sculptor;

import std;

import vw.core;
import vw.ecs;
import vw.world;
import vw.platform;
import vw.gfx;

namespace vw::sculptor {

entity_tree_panel::entity_tree_panel(
    engine_type& eng, app_state& st, operation_manager& op_manager
)
    : engine_(&eng)
    , state_(&st)
    , op_manager_(&op_manager)
    , creation_modal_(eng, st, op_manager)
    , deletion_modal_(eng, st, op_manager) {}

auto entity_tree_panel::render(
    float delta_time
) -> void {
    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    const auto window_pos         = ImVec2(
        viewport->WorkPos.x + viewport->WorkSize.x - 10,
        viewport->WorkPos.y + state_->ui.right_top_voffset + 10
    );
    ImGui::SetNextWindowPos(window_pos, ImGuiCond_Always, ImVec2(1.0f, 0.0f));

    constexpr ImGuiWindowFlags window_flags =  //
        ImGuiWindowFlags_NoSavedSettings |     //
        ImGuiWindowFlags_AlwaysAutoResize |    //
        ImGuiWindowFlags_NoMove;

    ImGui::Begin("Entity Tree", nullptr, window_flags);

    const bool can_add =
        !state_->anim.animation_mode && state_->ctx.in_prefab() &&
        (state_->scene.root_name.empty() || !state_->scene.selected_name.empty());
    if (!can_add) {
        ImGui::BeginDisabled();
    }
    if (ImGui::Button("Add Entity")) {
        creation_modal_.open();
    }
    if (!can_add) {
        ImGui::EndDisabled();
    }

    ImGui::SameLine();

    const bool can_remove = !state_->anim.animation_mode && state_->ctx.in_prefab() &&
        !state_->scene.selected_name.empty() &&
        state_->scene.name_to_entity.contains(state_->scene.selected_name);
    if (!can_remove) {
        ImGui::BeginDisabled();
    }
    if (ImGui::Button("Remove Entity")) {
        deletion_modal_.open(state_->scene.selected_name);
    }
    if (!can_remove) {
        ImGui::EndDisabled();
    }

    if (!state_->scene.root_name.empty()) {
        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();
        const auto preview_entities = state_->sockets.get_preview_entities();
        render_entity_node(state_->scene.root_name, preview_entities);
    }

    creation_modal_.render(delta_time);
    deletion_modal_.render(delta_time);

    ImGui::Dummy({200.0f, 0.0f});

    state_->ui.right_top_voffset += ImGui::GetWindowHeight() + 10.0f;

    ImGui::End();
}

// Выбор узла — правка префаба, а внутри объёма её нет: провалившись в .voxm,
// правишь именно тот узел, через который вошёл, и уехать из него мимо крошек
// нельзя.
auto entity_tree_panel::select_(const std::string& name) const -> void {
    if (!state_->ctx.in_prefab()) {
        return;
    }
    state_->scene.selected_name = name;
}

auto entity_tree_panel::render_entity_node(
    const std::string& name, const std::unordered_set<ecs::entity>& preview_entities
) -> void {
    if (name.empty()) {
        return;
    }

    auto ent    = state_->scene.name_to_entity[name];
    auto& world = engine_->get_world();

    const bool has_hierarchy = world.has<ecs::hierarchy_component>(ent);
    bool has_children        = false;

    if (has_hierarchy) {
        const auto& hierarchy_comp = world.get<ecs::hierarchy_component>(ent);
        has_children = std::ranges::any_of(hierarchy_comp.get_children(), [&](auto child) {
            return !preview_entities.contains(child);
        });
    }

    const bool is_selected = state_->scene.selected_name == name;
    bool is_open           = false;

    ImGuiTreeNodeFlags node_flags =       //
        ImGuiTreeNodeFlags_OpenOnArrow |  //
        ImGuiTreeNodeFlags_OpenOnDoubleClick | ImGuiTreeNodeFlags_DefaultOpen;

    if (is_selected) {
        node_flags |= ImGuiTreeNodeFlags_Selected;
    }

    const auto node_id = std::format("{}##{}.{}", name, ent.index, ent.generation);
    if (has_children) {
        if (ImGui::TreeNodeEx(node_id.c_str(), node_flags)) {
            is_open = true;
        }
    } else {
        if (ImGui::Selectable(node_id.c_str(), is_selected)) {
            select_(name);
        }
    }

    if (ImGui::IsItemClicked() && !ImGui::IsItemToggledOpen()) {
        select_(name);
    }

    if (!state_->anim.animation_mode && state_->ctx.in_prefab()) {
        const auto context_menu_id =
            std::format("EntityContextMenu_{}_{}", ent.index, ent.generation);
        if (ImGui::BeginPopupContextItem(context_menu_id.c_str())) {
            state_->scene.selected_name = name;
            if (ImGui::MenuItem("Create child")) {
                creation_modal_.open();
            }
            if (!has_children && ImGui::MenuItem("Delete entity")) {
                deletion_modal_.open(name);
            }
            ImGui::EndPopup();
        }
    }

    if (is_open && has_children) {
        const auto& hierarchy_comp = world.get<ecs::hierarchy_component>(ent);
        const auto& children       = hierarchy_comp.get_children();

        for (auto child : children) {
            if (preview_entities.contains(child)) {
                continue;
            }
            auto child_name = state_->scene.entity_to_name[child];
            render_entity_node(child_name, preview_entities);
        }

        ImGui::TreePop();
    }
}

}  // namespace vw::sculptor
