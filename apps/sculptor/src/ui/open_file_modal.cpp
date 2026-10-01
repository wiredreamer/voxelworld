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
    engine_type& eng, app_state& st, asset::model_library& library,
    operation_manager& op_manager
)
    : engine_(&eng), state_(&st), library_(&library), op_manager_(&op_manager) {}

auto open_file_modal::render(
    float
) -> void {
    if (state_->ui.need_open_file_modal) {
        ImGui::OpenPopup("Open Prefab");
        state_->ui.need_open_file_modal = false;

        filename_.clear();
        load_existing_filenames_();
    }

    constexpr ImGuiWindowFlags dialog_flags =  //
        ImGuiWindowFlags_AlwaysAutoResize |    //
        ImGuiWindowFlags_NoMove;
    if (ImGui::BeginPopupModal("Open Prefab", nullptr, dialog_flags)) {
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

auto open_file_modal::load_existing_filenames_() -> void {
    existing_filenames_.clear();

    namespace fs = std::filesystem;
    const fs::path prefab_dir_path = app_state::prefab_dir();
    if (!fs::exists(prefab_dir_path)) {
        log::critical("Prefab directory does not exist: {}", prefab_dir_path.string());
    }
    for (const auto& entry : fs::directory_iterator(prefab_dir_path)) {
        if (entry.is_regular_file() && entry.path().extension() == ".vox") {
            existing_filenames_.emplace_back(entry.path().filename().string());
        }
    }
}

auto open_file_modal::open_file_() -> bool {
    namespace fs = std::filesystem;

    asset::vox_parser_plain parser;
    ecs::vox_deserializer deserializer{engine_->get_world(), parser, *library_};

    const fs::path filepath = app_state::prefab_dir() / fs::path{filename_};
    auto result = deserializer.deserialize(filepath);
    if (!result.has_value()) {
        error_ = std::format("Failed to open prefab: {}", filepath.string());
        return false;
    }

    state_->reset(engine_->get_world());
    op_manager_->clear();

    state_->ui.need_startup_modal = false;

    state_->file.filename        = filename_;
    state_->scene.root_name      = result->root_name;
    state_->scene.selected_name  = result->root_name;
    state_->scene.name_to_entity = std::move(result->name_to_entity);
    state_->scene.entity_to_name = std::move(result->entity_to_name);
    state_->scene.entities       = std::move(result->entities);

    return true;
}

}  // namespace vw::sculptor
