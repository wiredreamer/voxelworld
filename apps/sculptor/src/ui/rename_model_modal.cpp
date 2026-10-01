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

namespace {
constexpr const char* popup_title = "Rename Model";
}  // namespace

rename_model_modal::rename_model_modal(
    engine_type& eng, app_state& st, file_service& file_svc
)
    : engine_(&eng), state_(&st), file_service_(&file_svc) {}

auto rename_model_modal::render() -> void {
    if (!state_->ui.need_rename_model_for.empty()) {
        open_(std::exchange(state_->ui.need_rename_model_for, {}));
    }

    ImGuiWindowFlags dialog_flags =          //
        ImGuiWindowFlags_AlwaysAutoResize |  //
        ImGuiWindowFlags_NoMove;
    if (ImGui::BeginPopupModal(popup_title, nullptr, dialog_flags)) {
        if (need_overwrite_confirmation_) {
            render_overwrite_confirmation_();
        } else {
            render_rename_form_();
        }
        ImGui::EndPopup();
    }
}

auto rename_model_modal::open_(
    const std::string& node_name
) -> void {
    const auto it = state_->scene.name_to_entity.find(node_name);
    if (it == state_->scene.name_to_entity.end()) {
        return;
    }

    const auto& world = engine_->get_world();
    if (!world.has<ecs::model_component>(it->second)) {
        return;
    }

    source_ = world.get<ecs::model_component>(it->second).get_source();
    if (source_.empty()) {
        return;
    }

    stem_ = source_.stem();
    error_.clear();
    need_overwrite_confirmation_ = false;
    has_overwrite_confirmation_  = false;

    ImGui::OpenPopup(popup_title);
}

auto rename_model_modal::render_overwrite_confirmation_() -> void {
    ImGui::TextColored(
        ImVec4{1.0f, 1.0f, 0.0f, 1.0f}, "A model with this name already exists. Overwrite?"
    );
    ImGui::Spacing();

    if (ImGui::Button("Yes")) {
        need_overwrite_confirmation_ = false;
        has_overwrite_confirmation_  = true;
        if (rename_()) {
            ImGui::CloseCurrentPopup();
        }
    }
    ImGui::SameLine();
    if (ImGui::Button("No")) {
        need_overwrite_confirmation_ = false;
    }
}

auto rename_model_modal::render_rename_form_() -> void {
    if (!error_.empty()) {
        ImGui::TextColored(ImVec4{1.0f, 0.0f, 0.0f, 1.0f}, "%s", error_.c_str());
        ImGui::Spacing();
    }

    ImGui::TextDisabled("%s", source_.str().c_str());
    imgui_input_text_string("Name", stem_);
    ImGui::TextDisabled("Renaming saves the prefab and clears the undo history.");

    ImGui::Spacing();

    ImGui::BeginDisabled(stem_.empty());
    if (ImGui::Button("Rename")) {
        has_overwrite_confirmation_ = false;
        if (rename_()) {
            ImGui::CloseCurrentPopup();
        }
    }
    ImGui::EndDisabled();

    ImGui::SameLine();
    if (ImGui::Button("Cancel")) {
        ImGui::CloseCurrentPopup();
    }
}

auto rename_model_modal::rename_() -> bool {
    std::string_view stem{stem_};
    if (stem.ends_with(source_.extension())) {
        stem.remove_suffix(source_.extension().size());
    }

    const auto renamed = file_service_->rename_model(source_, stem, has_overwrite_confirmation_);
    if (renamed.has_value()) {
        return true;
    }

    error_.clear();
    switch (renamed.error()) {
        case rename_model_error::invalid_name:
            error_ = "Name is empty or contains forbidden characters.";
            break;
        case rename_model_error::name_in_use:
            error_ = "Another model of this prefab already has this name.";
            break;
        case rename_model_error::file_exists: need_overwrite_confirmation_ = true; break;
        case rename_model_error::write_failed: error_ = "Failed to rename model."; break;
    }

    return false;
}

}  // namespace vw::sculptor
