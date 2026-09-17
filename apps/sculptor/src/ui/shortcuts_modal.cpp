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

shortcuts_modal::shortcuts_modal(
    app_state& st
)
    : state_(&st) {}

auto shortcuts_modal::open() -> void {
    need_open_ = true;
}

auto shortcuts_modal::render() -> void {
    if (need_open_) {
        need_open_ = false;
        ImGui::OpenPopup("Keyboard Shortcuts");
    }

    imgui_clamp_window_pos_to_viewport();
    if (!ImGui::BeginPopupModal(
            "Keyboard Shortcuts", nullptr, ImGuiWindowFlags_AlwaysAutoResize
        )) {
        return;
    }

    std::string_view group;
    for (const auto& entry : shortcuts) {
        if (entry.group != group) {
            group = entry.group;
            ImGui::SeparatorText(std::string{group}.c_str());
        }

        const bool available = is_available(entry.cmd, *state_);

        ImGui::BeginDisabled(!available);
        ImGui::TextUnformatted(entry.keys.data(), entry.keys.data() + entry.keys.size());
        ImGui::SameLine(110.f);
        ImGui::TextUnformatted(entry.title.data(), entry.title.data() + entry.title.size());
        ImGui::EndDisabled();
    }

    ImGui::Spacing();
    if (ImGui::Button("Close")) {
        ImGui::CloseCurrentPopup();
    }

    ImGui::EndPopup();
}

}  // namespace vw::sculptor
