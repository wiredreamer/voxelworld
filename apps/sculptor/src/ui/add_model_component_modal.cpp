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

add_model_component_modal::add_model_component_modal(
    engine_type& eng, app_state& state, operation_manager& op_manager
)
    : engine_(&eng), state_(&state), op_manager_(&op_manager) {}

auto add_model_component_modal::open(const std::string& entity_name) -> void {
    need_open_   = true;
    entity_name_ = entity_name;
    size_        = {8, 8, 8};
    error_.clear();
}

auto add_model_component_modal::render() -> void {
    if (need_open_) {
        ImGui::OpenPopup("Add Model Component");
        need_open_ = false;
    }

    constexpr ImGuiWindowFlags flags =  //
        ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoMove;

    if (ImGui::BeginPopupModal("Add Model Component", nullptr, flags)) {
        if (!error_.empty()) {
            ImGui::TextColored(ImVec4(1.f, 0.f, 0.f, 1.f), "%s", error_.c_str());
        }

        ImGui::Text("Entity: %s", entity_name_.c_str());
        ImGui::Separator();

        imgui_input_int_left("Size X", &size_.x);
        imgui_input_int_left("Size Y", &size_.y);
        imgui_input_int_left("Size Z", &size_.z);

        ImGui::Spacing();
        if (ImGui::Button("Add")) {
            if (confirm_()) {
                ImGui::CloseCurrentPopup();
            }
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel")) {
            ImGui::CloseCurrentPopup();
        }

        ImGui::EndPopup();
    }
}

auto add_model_component_modal::confirm_() -> bool {
    error_.clear();

    if (size_.x <= 0 || size_.y <= 0 || size_.z <= 0) {
        error_ = "Size dimensions must be greater than zero.";
        return false;
    }

    auto op = std::make_unique<add_model_component_operation>(
        *engine_, *state_,
        add_model_component_params{.name = entity_name_, .size = size_}
    );
    op_manager_->execute(std::move(op));
    return true;
}

}  // namespace vw::sculptor
