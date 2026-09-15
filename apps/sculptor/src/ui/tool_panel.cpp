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
    float /*delta_time*/
) const -> void {
    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    const auto window_pos =
        ImVec2(viewport->WorkPos.x + 10, viewport->WorkPos.y + state_->ui.left_top_voffset + 10);
    ImGui::SetNextWindowPos(window_pos, ImGuiCond_Always);

    constexpr ImGuiWindowFlags window_flags =  //
        ImGuiWindowFlags_NoCollapse |          //
        ImGuiWindowFlags_NoSavedSettings |     //
        ImGuiWindowFlags_AlwaysAutoResize;

    ImGui::Begin("Tools", nullptr, window_flags);

    ImGui::PushStyleVar(ImGuiStyleVar_ButtonTextAlign, ImVec2(0.0f, 0.5f));

    render_tool_button(tools::select_entity, "Select entity", "(0)");
    render_tool_button(tools::add_voxel, "Add voxel", "(1)");
    render_tool_button(tools::remove_voxel, "Remove voxel", "(2)");
    render_tool_button(tools::paint_voxel, "Paint voxel", "(3)");
    render_tool_button(tools::color_picker, "Color picker", "(4)");
    render_tool_button(tools::move_pivot, "Move pivot", "(5)");

    ImGui::PopStyleVar(1);

    state_->ui.left_top_voffset += ImGui::GetWindowHeight() + 10.f;

    ImGui::End();
}

auto tool_panel::render_tool_button(
    tools tool, std::string_view label, std::string_view shortcut
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

    ImGui::SameLine();
    ImGui::TextDisabled("%s", shortcut.data());
    ImGui::Spacing();
}

}  // namespace vw::sculptor
