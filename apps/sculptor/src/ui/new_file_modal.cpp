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

new_file_modal::new_file_modal(
    app_state& st, file_service& file_svc
)
    : state_(&st), file_service_(&file_svc) {}

auto new_file_modal::render(
    float
) -> void {
    if (state_->ui.need_new_file_modal) {
        ImGui::OpenPopup("New Prefab");
        state_->ui.need_new_file_modal = false;

        filename_.clear();
        error_.clear();
        need_overwrite_confirmation_ = false;
        has_overwrite_confirmation_  = false;
    }

    ImGuiWindowFlags dialog_flags =          //
        ImGuiWindowFlags_AlwaysAutoResize |  //
        ImGuiWindowFlags_NoMove;
    if (ImGui::BeginPopupModal("New Prefab", nullptr, dialog_flags)) {
        if (need_overwrite_confirmation_) {
            render_overwrite_confirmation();
        } else {
            render_create_form();
        }
        ImGui::EndPopup();
    }
}

auto new_file_modal::render_overwrite_confirmation() -> void {
    ImGui::TextColored(ImVec4{1.0f, 1.0f, 0.0f, 1.0f}, "Prefab already exists. Overwrite?");
    ImGui::Spacing();

    if (ImGui::Button("Yes")) {
        need_overwrite_confirmation_ = false;
        has_overwrite_confirmation_  = true;
        if (create_file_()) {
            ImGui::CloseCurrentPopup();
        }
    }
    ImGui::SameLine();
    if (ImGui::Button("No")) {
        need_overwrite_confirmation_ = false;
    }
}

auto new_file_modal::render_create_form() -> void {
    if (!error_.empty()) {
        ImGui::TextColored(ImVec4{1.0f, 0.0f, 0.0f, 1.0f}, "%s", error_.c_str());
        ImGui::Spacing();
    }

    imgui_input_text_string("Filename", filename_);

    ImGui::Spacing();

    if (filename_.empty()) {
        ImGui::BeginDisabled();
    }
    if (ImGui::Button("Create")) {
        if (create_file_()) {
            ImGui::CloseCurrentPopup();
        }
    }
    if (filename_.empty()) {
        ImGui::EndDisabled();
    }
    ImGui::SameLine();
    if (ImGui::Button("Cancel")) {
        ImGui::CloseCurrentPopup();
        if (state_->file.filename.empty()) {
            state_->ui.need_startup_modal = true;
        }
    }
}

auto new_file_modal::create_file_() -> bool {
    const auto created = file_service_->create(filename_, has_overwrite_confirmation_);
    if (created.has_value()) {
        return true;
    }

    switch (created.error()) {
        case prefab_error::already_exists:
            need_overwrite_confirmation_ = true;
            break;
        case prefab_error::invalid_name:
            error_ = "The name cannot be used for a prefab.";
            break;
        default:
            error_ = "Failed to create prefab.";
            break;
    }
    return false;
}

}  // namespace vw::sculptor
