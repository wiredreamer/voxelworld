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

create_clip_modal::create_clip_modal(
    engine_type& eng, app_state& state, operation_manager& op_manager
)
    : engine_(&eng), state_(&state), op_manager_(&op_manager) {}

auto create_clip_modal::open() -> void {
    need_open_ = true;
    auto& registry = engine_->get_world().resource<asset::animation_clip_registry>();
    name_ = std::format("clip_{}", registry.count());
    error_.clear();
    need_overwrite_confirmation_ = false;
    has_overwrite_confirmation_  = false;
}

auto create_clip_modal::render(
    float /*delta_time*/
) -> void {
    if (need_open_) {
        ImGui::OpenPopup("Create Animation Clip");
        need_open_ = false;
    }

    ImGuiWindowFlags dialog_flags =          //
        ImGuiWindowFlags_AlwaysAutoResize |  //
        ImGuiWindowFlags_NoMove;
    if (ImGui::BeginPopupModal("Create Animation Clip", nullptr, dialog_flags)) {
        if (need_overwrite_confirmation_) {
            ImGui::TextColored(
                ImVec4{1.0f, 1.0f, 0.0f, 1.0f},
                "File \"%s.voxa\" already exists. Overwrite on save?",
                name_.c_str()
            );
            ImGui::Spacing();

            if (ImGui::Button("Yes")) {
                need_overwrite_confirmation_ = false;
                has_overwrite_confirmation_  = true;
                if (create_clip()) {
                    ImGui::CloseCurrentPopup();
                }
            }
            ImGui::SameLine();
            if (ImGui::Button("No")) {
                need_overwrite_confirmation_ = false;
            }
        } else {
            if (!error_.empty()) {
                ImGui::TextColored(ImVec4(1.f, 0.f, 0.f, 1.f), "%s", error_.c_str());
            }

            imgui_input_text_string("Name", name_);

            if (ImGui::Button("Create")) {
                if (create_clip()) {
                    ImGui::CloseCurrentPopup();
                }
            }
            ImGui::SameLine();
            if (ImGui::Button("Cancel")) {
                ImGui::CloseCurrentPopup();
            }
        }

        ImGui::EndPopup();
    }
}

auto create_clip_modal::create_clip() -> bool {
    namespace fs = std::filesystem;

    error_.clear();

    if (name_.empty()) {
        error_ = "Name cannot be empty.";
        return false;
    }

    auto& registry = engine_->get_world().resource<asset::animation_clip_registry>();
    if (registry.has(name_)) {
        error_ = "A clip with this name already exists.";
        return false;
    }

    if (!has_overwrite_confirmation_) {
        fs::path filepath =
            fs::path{app_state::asset_dir_name} / std::format("{}.voxa", name_);
        if (fs::exists(filepath)) {
            need_overwrite_confirmation_ = true;
            return false;
        }
    }

    create_clip_params params = {.name = name_};
    auto op = std::make_unique<create_clip_operation>(*engine_, *state_, params);
    op_manager_->execute(std::move(op));

    return true;
}

}  // namespace vw::sculptor
