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

delete_entity_modal::delete_entity_modal(
    engine_type& eng, app_state& state, operation_manager& op_manager,
    asset::model_library& library
)
    : engine_(&eng), state_(&state), op_manager_(&op_manager), library_(&library) {}

auto delete_entity_modal::open(
    const std::string& delete_name
) -> void {
    need_open_   = true;
    delete_name_ = delete_name;
}

auto delete_entity_modal::render(
    [[maybe_unused]] float delta_time
) -> void {
    if (need_open_) {
        ImGui::OpenPopup("Delete Entity");
        need_open_ = false;
    }

    ImGuiWindowFlags dialog_flags =          //
        ImGuiWindowFlags_AlwaysAutoResize |  //
        ImGuiWindowFlags_NoMove;
    if (ImGui::BeginPopupModal("Delete Entity", nullptr, dialog_flags)) {
        ImGui::Text(
            "Are you sure want to delete \"%s\" and everything under it?", delete_name_.c_str()
        );

        if (ImGui::Button("Delete")) {
            delete_entity_params params = {
                .name = delete_name_
            };

            auto op =
                std::make_unique<delete_entity_operation>(*engine_, *state_, *library_, params);
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
