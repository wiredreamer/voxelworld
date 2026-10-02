module;

#include <imgui.h>

module vw.sculptor;

import std;

import vw.core;
import vw.asset;
import vw.ecs;
import vw.world;
import vw.platform;
import vw.gfx;

namespace vw::sculptor {

open_file_modal::open_file_modal(
    app_state& st, file_service& file_svc
)
    : state_(&st), file_service_(&file_svc) {}

auto open_file_modal::render(
    float
) -> void {
    if (state_->ui.need_open_file_modal) {
        ImGui::OpenPopup("Open Prefab");
        state_->ui.need_open_file_modal = false;

        filename_.clear();
        error_.clear();
        existing_filenames_ = list_prefabs();
    }

    constexpr ImGuiWindowFlags dialog_flags =  //
        ImGuiWindowFlags_AlwaysAutoResize |    //
        ImGuiWindowFlags_NoMove;
    if (ImGui::BeginPopupModal("Open Prefab", nullptr, dialog_flags)) {
        if (!error_.empty()) {
            ImGui::TextColored(ImVec4{1.0f, 0.0f, 0.0f, 1.0f}, "%s", error_.c_str());
            ImGui::Spacing();
        }

        ImGui::Text("Existing Prefabs:");
        ImGui::Spacing();

        const float list_height = ImGui::GetTextLineHeightWithSpacing() * 7.5f;

        constexpr ImGuiChildFlags child_flags =       //
            ImGuiChildFlags_AlwaysUseWindowPadding |  //
            ImGuiChildFlags_Borders;
        if (ImGui::BeginChild("##file_list", ImVec2(400.f, list_height), child_flags)) {
            for (const auto& f : existing_filenames_) {
                const bool is_selected = filename_ == f;
                if (ImGui::Selectable(f.c_str(), is_selected)) {
                    filename_ = f;
                }
            }
        }
        ImGui::EndChild();

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        if (filename_.empty()) {
            ImGui::BeginDisabled();
        }
        if (ImGui::Button("Open")) {
            if (open_file_()) {
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

        ImGui::EndPopup();
    }
}

auto open_file_modal::open_file_() -> bool {
    if (file_service_->open(filename_).has_value()) {
        return true;
    }

    error_ = std::format("Failed to open prefab: {}", filename_);
    return false;
}

}  // namespace vw::sculptor
