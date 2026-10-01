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

constexpr float32 label_column = 70.f;

struct axis_button {
    asset::voxel_axis axis;
    const char* label;
};

constexpr std::array axis_buttons{
    axis_button{asset::voxel_axis::x, "X"},
    axis_button{asset::voxel_axis::y, "Y"},
    axis_button{asset::voxel_axis::z, "Z"},
};

auto pick_axis(const char* row) -> std::optional<asset::voxel_axis> {
    std::optional<asset::voxel_axis> picked;

    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(row);

    ImGui::PushID(row);
    for (const auto& button : axis_buttons) {
        if (button.axis == asset::voxel_axis::x) {
            ImGui::SameLine(label_column);
        } else {
            ImGui::SameLine();
        }
        if (ImGui::Button(button.label)) {
            picked = button.axis;
        }
    }
    ImGui::PopID();

    return picked;
}

}  // namespace

paste_panel::paste_panel(
    app_state& st, clipboard_service& clipboard_svc
)
    : state_(&st), clipboard_service_(&clipboard_svc) {}

auto paste_panel::render(
    float
) const -> void {
    auto& paste = state_->paste;
    if (!paste.active()) {
        return;
    }

    begin_panel(*state_, panel_slot::left, "Paste");

    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted("Size");
    ImGui::SameLine(label_column);
    ImGui::TextDisabled(
        "%dx%dx%d voxels", paste.clip.size.x, paste.clip.size.y, paste.clip.size.z
    );

    if (imgui_drag_vec3i("Offset", paste.origin, label_column)) {
        paste.preview_stale = true;
    }

    std::optional<asset::voxel_orientation> turn;

    if (const auto axis = pick_axis("Flip")) {
        turn = asset::mirrored_orientation(*axis);
    }
    if (const auto axis = pick_axis("Rotate +90")) {
        turn = asset::rotated_orientation(*axis, 1);
    }
    if (const auto axis = pick_axis("Rotate -90")) {
        turn = asset::rotated_orientation(*axis, -1);
    }

    bool replaces = paste.mode == asset::paste_mode::replace;
    if (ImGui::Checkbox("Air erases the target", &replaces)) {
        paste.mode = replaces ? asset::paste_mode::replace : asset::paste_mode::keep_air;
        paste.preview_stale = true;
    }

    ImGui::Separator();

    const bool applied = ImGui::Button("Apply");
    ImGui::SameLine();
    ImGui::TextDisabled("%s", std::string{keys_of(command::confirm)}.c_str());

    ImGui::SameLine();
    const bool cancelled = ImGui::Button("Cancel");
    ImGui::SameLine();
    ImGui::TextDisabled("%s", std::string{keys_of(command::cancel)}.c_str());

    end_panel(*state_, panel_slot::left);

    if (applied) {
        clipboard_service_->apply_paste();
    } else if (cancelled) {
        clipboard_service_->cancel_paste();
    } else if (turn) {
        clipboard_service_->reorient_paste(*turn);
    }
}

}  // namespace vw::sculptor
