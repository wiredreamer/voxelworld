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

delete_track_modal::delete_track_modal(
    engine_type& eng, app_state& st, operation_manager& op_manager
)
    : engine_(&eng), state_(&st), op_manager_(&op_manager) {}

auto delete_track_modal::open(
    const std::string& track_name
) -> void {
    need_open_  = true;
    track_name_ = track_name;
}

auto delete_track_modal::render(
    float
) -> void {
    if (need_open_) {
        ImGui::OpenPopup("Delete Track?");
        need_open_ = false;
    }

    ImGuiWindowFlags dialog_flags =          //
        ImGuiWindowFlags_AlwaysAutoResize |  //
        ImGuiWindowFlags_NoMove;
    if (ImGui::BeginPopupModal("Delete Track?", nullptr, dialog_flags)) {
        ImGui::Text("Delete track '%s' and all its keyframes?", track_name_.c_str());
        ImGui::Spacing();

        if (ImGui::Button("Delete")) {
            remove_track_params params = {
                .clip_name  = state_->anim.selected_clip_name,
                .track_name = track_name_,
            };
            auto op = std::make_unique<remove_track_operation>(*engine_, *state_, params);
            op_manager_->execute(std::move(op));
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel")) {
            ImGui::CloseCurrentPopup();
        }

        ImGui::EndPopup();
    }
}

}  // namespace vw::sculptor
