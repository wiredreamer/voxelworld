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

open_clip_modal::open_clip_modal(
    engine_type& eng, app_state& st, clip_service& clip_svc
)
    : engine_(&eng), state_(&st), clip_service_(&clip_svc) {}

auto open_clip_modal::open() -> void {
    need_open_ = true;
    load_filenames_();
    selected_.clear();
    error_.clear();
    rig_mismatch_seen_ = false;
}

auto open_clip_modal::render() -> void {
    if (need_open_) {
        need_open_ = false;
        ImGui::OpenPopup("Open Animation");
    }

    constexpr ImGuiWindowFlags dialog_flags =
        ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoMove;

    if (!ImGui::BeginPopupModal("Open Animation", nullptr, dialog_flags)) {
        return;
    }

    ImGui::Text("Animation files:");
    ImGui::Spacing();

    const float list_height = ImGui::GetTextLineHeightWithSpacing() * 7.5f;

    constexpr ImGuiChildFlags child_flags =
        ImGuiChildFlags_AlwaysUseWindowPadding | ImGuiChildFlags_Borders;

    if (ImGui::BeginChild("##voxa_file_list", ImVec2(400.f, list_height), child_flags)) {
        if (filenames_.empty()) {
            ImGui::TextDisabled("No .voxa files found");
        }
        for (const auto& filename : filenames_) {
            if (ImGui::Selectable(filename.c_str(), selected_ == filename)) {
                selected_ = filename;
                error_.clear();
                rig_mismatch_seen_ = false;
            }
        }
    }
    ImGui::EndChild();

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    if (!error_.empty()) {
        ImGui::TextColored(ImVec4(1.f, 1.f, 0.f, 1.f), "%s", error_.c_str());
        ImGui::Spacing();
    }

    ImGui::BeginDisabled(selected_.empty());
    if (ImGui::Button(rig_mismatch_seen_ ? "Open anyway" : "Open")) {
        const auto report = clip_service_->load_clip(selected_, rig_mismatch_seen_);
        switch (report.status) {
            case clip_load_status::loaded:
                error_.clear();
                rig_mismatch_seen_ = false;

                // Открыли клип — значит пришли его править: режим включается
                // сам, иначе после диалога пришлось бы искать, чем его включить.
                state_->ui.need_enter_animation = true;
                ImGui::CloseCurrentPopup();
                break;
            case clip_load_status::file_error:
                error_             = "Failed to read the clip.";
                rig_mismatch_seen_ = false;
                break;
            case clip_load_status::rig_mismatch:
                error_             = describe_rig_report_(report.rig);
                rig_mismatch_seen_ = true;
                break;
        }
    }
    ImGui::EndDisabled();

    ImGui::SameLine();
    if (ImGui::Button("Cancel")) {
        ImGui::CloseCurrentPopup();
    }

    ImGui::EndPopup();
}

auto open_clip_modal::describe_rig_report_(
    const ecs::rig_report& report
) -> std::string {
    std::string text;

    if (!report.rig_matches()) {
        text = std::format(
            "This clip is for rig '{}', the document is '{}'.", report.clip_rig, report.rig
        );
    }

    if (!report.unknown_targets.empty()) {
        if (!text.empty()) {
            text += "\n";
        }

        text += "No node here for: ";
        for (std::size_t i = 0; i < report.unknown_targets.size(); ++i) {
            if (i != 0) {
                text += ", ";
            }
            text += report.unknown_targets[i];
        }
        text += ".";
    }

    return text;
}

auto open_clip_modal::load_filenames_() -> void {
    namespace fs = std::filesystem;

    filenames_.clear();

    const fs::path dir = app_state::clip_dir();
    if (!fs::exists(dir)) {
        return;
    }

    for (const auto& entry : fs::directory_iterator(dir)) {
        if (entry.is_regular_file() && entry.path().extension() == ".voxa") {
            filenames_.emplace_back(entry.path().filename().string());
        }
    }
}

}  // namespace vw::sculptor
