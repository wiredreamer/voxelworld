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

gizmo_panel::gizmo_panel(
    app_state& st
)
    : state_(&st) {}

auto gizmo_panel::render(
    float
) const -> void {
    begin_panel(*state_, panel_slot::left, "Gizmo");

    mode_button_(gizmo_mode::translate, "Move", command::gizmo_move);
    ImGui::SameLine();
    mode_button_(gizmo_mode::rotate, "Rotate", command::gizmo_rotate);
    ImGui::SameLine();
    mode_button_(gizmo_mode::scale, "Scale", command::gizmo_scale);

    end_panel(*state_, panel_slot::left);
}

auto gizmo_panel::mode_button_(
    gizmo_mode mode, std::string_view label, command cmd
) const -> void {
    const bool is_selected  = state_->tool.gizmo == mode;
    const auto button_color = is_selected ? ImGuiCol_ButtonActive : ImGuiCol_Button;
    const auto hover_color  = is_selected ? ImGuiCol_ButtonActive : ImGuiCol_ButtonHovered;

    ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyle().Colors[button_color]);
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImGui::GetStyle().Colors[hover_color]);

    const auto keys    = keys_of(cmd);
    const auto caption = std::format("{} ({})", label, keys);
    if (ImGui::Button(caption.c_str(), ImVec2(90.f, 0.f))) {
        state_->tool.gizmo = mode;
    }

    ImGui::PopStyleColor(2);
}

}  // namespace vw::sculptor
