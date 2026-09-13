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

save_clip_as_modal::save_clip_as_modal(
    engine_type& eng, app_state& st, clip_service& clip_svc
)
    : engine_(&eng), state_(&st), clip_service_(&clip_svc) {}

auto save_clip_as_modal::open() -> void {
    need_open_ = true;
    name_      = state_->anim.selected_clip_name;
    error_.clear();
    need_overwrite_confirmation_ = false;
    has_overwrite_confirmation_  = false;
}

auto save_clip_as_modal::render(
    float /*delta_time*/
) -> void {
    if (need_open_) {
        ImGui::OpenPopup("Save Animation As");
        need_open_ = false;
    }

    ImGuiWindowFlags dialog_flags =          //
        ImGuiWindowFlags_AlwaysAutoResize |  //
        ImGuiWindowFlags_NoMove;
    if (ImGui::BeginPopupModal("Save Animation As", nullptr, dialog_flags)) {
        if (need_overwrite_confirmation_) {
            render_overwrite_confirmation_();
        } else {
            render_save_form_();
        }
        ImGui::EndPopup();
    }
}

auto save_clip_as_modal::render_overwrite_confirmation_() -> void {
    ImGui::TextColored(
        ImVec4{1.0f, 1.0f, 0.0f, 1.0f},
        "File \"%s.voxa\" already exists. Overwrite?",
        name_.c_str()
    );
    ImGui::Spacing();

    if (ImGui::Button("Yes")) {
        need_overwrite_confirmation_ = false;
        has_overwrite_confirmation_  = true;
        if (save_clip_()) {
            ImGui::CloseCurrentPopup();
        }
    }
    ImGui::SameLine();
    if (ImGui::Button("No")) {
        need_overwrite_confirmation_ = false;
    }
}

auto save_clip_as_modal::render_save_form_() -> void {
    if (!error_.empty()) {
        ImGui::TextColored(ImVec4{1.0f, 0.0f, 0.0f, 1.0f}, "%s", error_.c_str());
        ImGui::Spacing();
    }

    imgui_input_text_string("Name", name_);

    ImGui::Spacing();

    if (name_.empty()) {
        ImGui::BeginDisabled();
    }
    if (ImGui::Button("Save")) {
        if (save_clip_()) {
            ImGui::CloseCurrentPopup();
        }
    }
    if (name_.empty()) {
        ImGui::EndDisabled();
    }
    ImGui::SameLine();
    if (ImGui::Button("Cancel")) {
        ImGui::CloseCurrentPopup();
    }
}

auto save_clip_as_modal::save_clip_() -> bool {
    namespace fs = std::filesystem;

    error_.clear();

    if (name_.empty()) {
        error_ = "Name cannot be empty.";
        return false;
    }

    const auto& old_name = state_->anim.selected_clip_name;
    if (name_ == old_name) {
        error_ = "Name must be different from the current name.";
        return false;
    }

    auto& registry = engine_->get_world().resource<asset::animation_clip_registry>();
    if (registry.has(name_)) {
        error_ = "A clip with this name is already open.";
        return false;
    }

    if (!has_overwrite_confirmation_) {
        const fs::path filepath =
            fs::path{app_state::asset_dir_name} / std::format("{}.voxa", name_);
        if (fs::exists(filepath)) {
            need_overwrite_confirmation_ = true;
            return false;
        }
    }

    return clip_service_->save_clip_as(old_name, name_);
}

}  // namespace vw::sculptor
