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

namespace {

constexpr const char* popup_title = "Rename Entity";

}  // namespace

rename_entity_modal::rename_entity_modal(
    engine_type& eng, app_state& state, operation_manager& op_manager
)
    : engine_(&eng), state_(&state), op_manager_(&op_manager) {}

auto rename_entity_modal::open(
    const std::string& name
) -> void {
    need_open_ = true;
    name_      = name;
    new_name_  = name;
    error_.clear();
}

auto rename_entity_modal::render() -> void {
    if (need_open_) {
        ImGui::OpenPopup(popup_title);
        need_open_ = false;
    }

    const ImGuiWindowFlags dialog_flags =    //
        ImGuiWindowFlags_AlwaysAutoResize |  //
        ImGuiWindowFlags_NoMove;
    if (!ImGui::BeginPopupModal(popup_title, nullptr, dialog_flags)) {
        return;
    }

    if (!error_.empty()) {
        ImGui::TextColored(ImVec4{1.0f, 0.0f, 0.0f, 1.0f}, "%s", error_.c_str());
        ImGui::Spacing();
    }

    ImGui::TextDisabled("%s", name_.c_str());
    imgui_input_text_string("Name", new_name_);
    ImGui::TextDisabled("Only the node is renamed: its animation target, variant slot");
    ImGui::TextDisabled("and volume file keep their names.");

    ImGui::Spacing();

    ImGui::BeginDisabled(new_name_.empty());
    if (ImGui::Button("Rename")) {
        if (rename_()) {
            ImGui::CloseCurrentPopup();
        }
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    if (ImGui::Button("Cancel")) {
        ImGui::CloseCurrentPopup();
    }

    ImGui::EndPopup();
}

auto rename_entity_modal::rename_() -> bool {
    if (new_name_ == name_) {
        return true;
    }

    if (auto problem = node_rename_problem(*state_, name_, new_name_)) {
        error_ = std::move(*problem);
        return false;
    }

    op_manager_->execute(
        std::make_unique<rename_entity_operation>(
            *engine_, *state_, rename_entity_params{.name = name_, .new_name = new_name_}
        )
    );
    return true;
}

}  // namespace vw::sculptor
