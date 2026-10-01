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

unsaved_changes_modal::unsaved_changes_modal(
    app_state& st, file_service& file_svc
)
    : state_(&st), file_service_(&file_svc) {}

auto unsaved_changes_modal::render() -> void {
    if (const auto requested = take_request_()) {
        if (state_->has_unsaved_changes()) {
            pending_ = *requested;
            ImGui::OpenPopup("Unsaved Changes");
        } else {
            proceed_(*requested);
        }
    }

    constexpr ImGuiWindowFlags dialog_flags =  //
        ImGuiWindowFlags_AlwaysAutoResize |    //
        ImGuiWindowFlags_NoMove;
    if (ImGui::BeginPopupModal("Unsaved Changes", nullptr, dialog_flags)) {
        ImGui::TextColored(
            ImVec4{1.0f, 1.0f, 0.0f, 1.0f}, "There are unsaved changes. Discard them?"
        );
        ImGui::Spacing();

        if (ImGui::Button("Yes")) {
            ImGui::CloseCurrentPopup();
            proceed_(pending_);
        }
        ImGui::SameLine();
        if (ImGui::Button("No")) {
            ImGui::CloseCurrentPopup();
        }

        ImGui::EndPopup();
    }
}

auto unsaved_changes_modal::take_request_() const -> std::optional<action> {
    if (std::exchange(state_->ui.need_close_file, false)) {
        return action::close_file;
    }
    if (std::exchange(state_->ui.need_new_file_modal, false)) {
        return action::new_file;
    }
    if (std::exchange(state_->ui.need_open_file_modal, false)) {
        return action::open_file;
    }
    return std::nullopt;
}

auto unsaved_changes_modal::proceed_(
    action confirmed
) const -> void {
    switch (confirmed) {
        case action::close_file: file_service_->close(); break;
        case action::new_file: state_->ui.need_new_file_modal = true; break;
        case action::open_file: state_->ui.need_open_file_modal = true; break;
    }
}

}  // namespace vw::sculptor
