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
    engine_type& eng, app_state& st, operation_manager& op_manager
)
    : engine_(&eng), state_(&st), op_manager_(&op_manager) {}

auto new_file_modal::render(
    float /*delta_time*/
) -> void {
    if (state_->ui.need_new_file_modal) {
        ImGui::OpenPopup("New File");
        state_->ui.need_new_file_modal = false;

        filename_.clear();
        kind_.clear();
        error_.clear();
        need_overwrite_confirmation_ = false;
        has_overwrite_confirmation_  = false;
    }

    ImGuiWindowFlags dialog_flags =          //
        ImGuiWindowFlags_AlwaysAutoResize |  //
        ImGuiWindowFlags_NoMove;
    if (ImGui::BeginPopupModal("New File", nullptr, dialog_flags)) {
        if (need_overwrite_confirmation_) {
            render_overwrite_confirmation();
        } else {
            render_create_form();
        }
        ImGui::EndPopup();
    }
}

auto new_file_modal::render_overwrite_confirmation() -> void {
    ImGui::TextColored(ImVec4{1.0f, 1.0f, 0.0f, 1.0f}, "File already exists. Overwrite?");
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

    constexpr std::array<std::string_view, 3> kinds{
        std::string_view{}, asset::kinds::character, asset::kinds::structure
    };

    const auto label = kind_.empty() ? "item" : kind_.c_str();

    if (ImGui::BeginCombo("Kind", label)) {
        for (const auto kind : kinds) {
            const bool selected = kind == kind_;
            if (ImGui::Selectable(kind.empty() ? "item" : std::string{kind}.c_str(), selected)) {
                kind_ = std::string{kind};
            }
        }
        ImGui::EndCombo();
    }

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
    namespace fs = std::filesystem;

    fs::path filepath(app_state::prefab_dir() / filename_);
    if (filepath.extension() != ".vox") {
        filepath.replace_extension("vox");
    }

    if (!has_overwrite_confirmation_ && fs::exists(filepath)) {
        need_overwrite_confirmation_ = true;
        return false;
    }

    auto file = std::ofstream(filepath, std::ios::trunc);
    if (!file.is_open()) {
        error_ = "Failed to create file.";
        return false;
    }

    // Шапка пишется сразу: документ без единого узла всё равно уже знает, чем он
    // будет, и переоткрытие не должно об этом забыть.
    file << std::format("# Vox File Version {}\n", asset::vox_file_version);
    if (!kind_.empty()) {
        file << std::format("kind {}\n", kind_);
    }
    file.close();

    state_->reset(engine_->get_world());
    op_manager_->clear();

    state_->ui.need_startup_modal = false;

    state_->file.filename = filepath.filename().string();
    state_->file.kind     = kind_;
    state_->apply_kind_defaults(engine_->get_block_registry());

    return true;
}

}  // namespace vw::sculptor
