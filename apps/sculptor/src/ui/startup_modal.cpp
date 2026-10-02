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

startup_modal::startup_modal(
    engine_type& eng, app_state& state
)
    : engine_(&eng), state_(&state) {}

auto startup_modal::render(
    float
) -> void {
    if (state_->ui.need_startup_modal) {
        ImGui::OpenPopup("Welcome to Sculptor");
        state_->ui.need_startup_modal = false;
    }

    ImGuiWindowFlags dialog_flags =          //
        ImGuiWindowFlags_AlwaysAutoResize |  //
        ImGuiWindowFlags_NoMove;
    state_->ui.startup_modal_open =
        ImGui::BeginPopupModal("Welcome to Sculptor", nullptr, dialog_flags);

    if (state_->ui.startup_modal_open) {
        if (!state_->file.filename.empty()) {
            ImGui::CloseCurrentPopup();
        }

        ImGui::Text("Select next step to get started:");
        ImGui::Spacing();
        if (ImGui::Button("New Prefab")) {
            state_->ui.need_new_file_modal = true;
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Open Prefab")) {
            state_->ui.need_open_file_modal = true;
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
}

}  // namespace vw::sculptor
