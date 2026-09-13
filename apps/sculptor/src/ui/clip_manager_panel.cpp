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

clip_manager_panel::clip_manager_panel(
    engine_type& eng, app_state& st, operation_manager& op_manager,
    clip_service& clip_svc
)
    : engine_(&eng)
    , state_(&st)
    , op_manager_(&op_manager)
    , clip_service_(&clip_svc)
    , create_modal_(eng, st, op_manager)
    , layer_blend_modal_(st)
    , save_clip_as_modal_(eng, st, clip_svc) {}

auto clip_manager_panel::render(
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

    bool still_open = true;
    ImGui::Begin("Animation Clips", &still_open, window_flags);
    if (!still_open) {
        if (state_->anim.animation_mode) {
            clip_service_->exit_animation_mode();
        }
        state_->ui.show_clip_manager = false;
        state_->ui.show_timeline     = false;
    } else if (!state_->anim.animation_mode) {
        clip_service_->enter_animation_mode();
    }

    if (state_->ui.need_create_clip_modal) {
        create_modal_.open();
        state_->ui.need_create_clip_modal = false;
    }

    if (state_->ui.need_save_clip) {
        state_->ui.need_save_clip = false;
        clip_service_->save_clip(state_->anim.selected_clip_name);
    }

    if (state_->ui.need_load_clip_modal) {
        load_voxa_filenames_();
        selected_load_filename_.clear();
        ImGui::OpenPopup("Open Animation");
        state_->ui.need_load_clip_modal = false;
    }

    if (state_->ui.need_close_clip) {
        state_->ui.need_close_clip = false;
        if (state_->anim.has_unsaved_clip(state_->anim.selected_clip_name)) {
            need_close_confirm_popup_ = true;
        } else {
            clip_service_->close_clip(state_->anim.selected_clip_name);
        }
    }

    const bool has_selected = !state_->anim.selected_clip_name.empty() &&
        engine_->get_world().resource<asset::animation_clip_registry>().has(state_->anim.selected_clip_name);

    if (ImGui::Button("New")) {
        create_modal_.open();
    }

    ImGui::SameLine();
    if (ImGui::Button("Open")) {
        load_voxa_filenames_();
        selected_load_filename_.clear();
        need_load_popup_ = true;
    }

    ImGui::SameLine();
    ImGui::BeginDisabled(!has_selected);
    if (ImGui::Button("Save")) {
        clip_service_->save_clip(state_->anim.selected_clip_name);
    }
    ImGui::EndDisabled();

    ImGui::SameLine();
    ImGui::BeginDisabled(!has_selected);
    if (ImGui::Button("Save As")) {
        save_clip_as_modal_.open();
    }
    ImGui::EndDisabled();

    ImGui::SameLine();
    ImGui::BeginDisabled(!has_selected);
    if (ImGui::Button("Close")) {
        if (state_->anim.has_unsaved_clip(state_->anim.selected_clip_name)) {
            need_close_confirm_popup_ = true;
        } else {
            clip_service_->close_clip(state_->anim.selected_clip_name);
        }
    }
    ImGui::EndDisabled();

    if (need_close_confirm_popup_) {
        ImGui::OpenPopup("Close Animation?");
        need_close_confirm_popup_ = false;
    }

    render_close_confirm_popup_();

    if (need_load_popup_) {
        ImGui::OpenPopup("Open Animation");
        need_load_popup_ = false;
    }
    render_load_popup_();

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    const float list_height = ImGui::GetTextLineHeightWithSpacing() * 7.5f;

    constexpr ImGuiChildFlags child_flags =       //
        ImGuiChildFlags_AlwaysUseWindowPadding |  //
        ImGuiChildFlags_Borders;

    if (ImGui::BeginChild("##clip_list", ImVec2(0.f, list_height), child_flags)) {
        const auto& registry = engine_->get_world().resource<asset::animation_clip_registry>();
        for (const auto& [name, clip] : registry.all()) {
            const bool is_selected = (state_->anim.selected_clip_name == name);
            const bool is_unsaved  = state_->anim.has_unsaved_clip(name);

            if (clip) {
                ImGui::TextDisabled("[%zu]", state_->anim.get_layer_for_clip(name));
                ImGui::SameLine();
            }

            if (is_unsaved) {
                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.f, 0.9f, 0.4f, 1.f));
            }

            auto display_name = is_unsaved ? std::format("{}*", name) : name;
            if (ImGui::Selectable(display_name.c_str(), is_selected, false)) {
                if (!is_selected) {
                    state_->anim.selected_clip_name = name;
                    state_->ui.show_timeline   = true;
                    state_->anim.selected_track_name.clear();
                    state_->anim.selected_keyframe_id = asset::invalid_keyframe_id;
                }
            }

            if (is_unsaved) {
                ImGui::PopStyleColor();
            }
        }
    }
    ImGui::EndChild();

    if (has_selected) {
        ImGui::Spacing();
        ImGui::Text("Layer");
        ImGui::SameLine();
        const auto current_layer = state_->anim.get_layer_for_clip(state_->anim.selected_clip_name);
        for (int i = 0; i < 5; ++i) {
            if (i > 0) {
                ImGui::SameLine();
            }
            bool is_active = (current_layer == static_cast<std::size_t>(i));
            if (is_active) {
                ImGui::PushStyleColor(
                    ImGuiCol_Button, ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive)
                );
            }
            auto btn_id = std::format("{}##layer_btn", i);
            if (ImGui::SmallButton(btn_id.c_str())) {
                clip_service_->stop_layer_for_clip(state_->anim.selected_clip_name);
                state_->anim.clip_to_layer[state_->anim.selected_clip_name] = static_cast<std::size_t>(i);
            }
            if (is_active) {
                ImGui::PopStyleColor();
            }
        }

        const auto layer_idx = state_->anim.get_layer_for_clip(state_->anim.selected_clip_name);
        if (layer_idx > 0) {
            ImGui::SameLine();
            if (ImGui::SmallButton("Blend")) {
                layer_blend_modal_.open();
            }
        }

        if (state_->anim.animation_mode) {
            ImGui::Spacing();
            if (ImGui::Button("Reset All")) {
                clip_service_->reset_all();
            }
        }
    }

    create_modal_.render(delta_time);
    layer_blend_modal_.render(delta_time);
    save_clip_as_modal_.render(delta_time);

    ImGui::Dummy({200.0f, 0.0f});

    state_->ui.right_top_voffset += ImGui::GetWindowHeight() + 10.0f;

    ImGui::End();
}

auto clip_manager_panel::render_close_confirm_popup_() const -> void {
    if (ImGui::BeginPopupModal("Close Animation?", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::Text(
            "Animation \"%s\" has unsaved changes. Close anyway?",
            state_->anim.selected_clip_name.c_str()
        );
        ImGui::Spacing();

        if (ImGui::Button("Save & Close")) {
            clip_service_->save_clip(state_->anim.selected_clip_name);
            clip_service_->close_clip(state_->anim.selected_clip_name);
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Close")) {
            clip_service_->close_clip(state_->anim.selected_clip_name);
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel")) {
            ImGui::CloseCurrentPopup();
        }

        ImGui::EndPopup();
    }
}

auto clip_manager_panel::render_load_popup_() -> void {
    ImGuiWindowFlags dialog_flags = ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoMove;

    if (ImGui::BeginPopupModal("Open Animation", nullptr, dialog_flags)) {
        ImGui::Text("Animation files:");
        ImGui::Spacing();

        const float list_height = ImGui::GetTextLineHeightWithSpacing() * 7.5f;
        ImGuiChildFlags child_flags =
            ImGuiChildFlags_AlwaysUseWindowPadding | ImGuiChildFlags_Borders;
        if (ImGui::BeginChild("##voxa_file_list", ImVec2(400.f, list_height), child_flags)) {
            if (voxa_filenames_.empty()) {
                ImGui::TextDisabled("No .voxa files found");
            } else {
                for (const auto& filename : voxa_filenames_) {
                    bool is_selected = selected_load_filename_ == filename;
                    if (ImGui::Selectable(filename.c_str(), is_selected)) {
                        selected_load_filename_ = filename;
                    }
                }
            }
        }
        ImGui::EndChild();

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        if (selected_load_filename_.empty()) {
            ImGui::BeginDisabled();
        }
        if (ImGui::Button("Open")) {
            if (clip_service_->load_clip(selected_load_filename_)) {
                ImGui::CloseCurrentPopup();
            }
        }
        if (selected_load_filename_.empty()) {
            ImGui::EndDisabled();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel")) {
            ImGui::CloseCurrentPopup();
        }

        ImGui::EndPopup();
    }
}

auto clip_manager_panel::load_voxa_filenames_() -> void {
    namespace fs = std::filesystem;

    voxa_filenames_.clear();

    fs::path asset_dir_path{app_state::asset_dir_name};
    if (!fs::exists(asset_dir_path)) {
        return;
    }

    for (const auto& entry : fs::directory_iterator(asset_dir_path)) {
        if (entry.is_regular_file() && entry.path().extension() == ".voxa") {
            voxa_filenames_.emplace_back(entry.path().filename().string());
        }
    }
}

}  // namespace vw::sculptor
