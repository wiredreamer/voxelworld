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

tool_panel::tool_panel(
    app_state& st
)
    : state_(&st) {}

auto tool_panel::render(
    float
) const -> void {
    begin_panel(*state_, panel_slot::left, "Tools");

    ImGui::PushStyleVar(ImGuiStyleVar_ButtonTextAlign, ImVec2(0.0f, 0.5f));

    render_tool_button(tools::select_entity, "Select entity");
    render_tool_button(tools::add_voxel, "Add voxel");
    render_tool_button(tools::remove_voxel, "Remove voxel");
    render_tool_button(tools::paint_voxel, "Paint voxel");
    render_tool_button(tools::material_brush, "Material brush");
    render_tool_button(tools::color_picker, "Color picker");
    render_tool_button(tools::move_pivot, "Move pivot");
    render_tool_button(tools::select_box, "Select box");

    ImGui::PopStyleVar(1);

    end_panel(*state_, panel_slot::left);
}

auto tool_panel::render_tool_button(
    tools tool, std::string_view label
) const -> void {
    if (!state_->ctx.allows_tool(tool)) {
        return;
    }

    const bool is_selected        = state_->tool.selected_tool == tool;
    const auto button_color       = is_selected ? ImGuiCol_ButtonActive : ImGuiCol_Button;
    const auto button_hover_color = is_selected ? ImGuiCol_ButtonActive : ImGuiCol_ButtonHovered;

    ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyle().Colors[button_color]);
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImGui::GetStyle().Colors[button_hover_color]);

    if (ImGui::Button(label.data(), ImVec2(120, 0))) {
        state_->tool.selected_tool = tool;
    }
    ImGui::PopStyleColor(2);

    const auto cmd  = command_for_tool(tool);
    const auto keys = cmd ? keys_of(*cmd) : std::string_view{};
    ImGui::SameLine();
    ImGui::TextDisabled("%.*s", static_cast<int>(keys.size()), keys.data());
    ImGui::Spacing();
}

}  // namespace vw::sculptor
