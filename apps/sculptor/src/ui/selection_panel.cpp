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

constexpr float32 label_column = 60.f;

}  // namespace

selection_panel::selection_panel(
    app_state& st, clipboard_service& clipboard_svc
)
    : state_(&st), clipboard_service_(&clipboard_svc) {}

auto selection_panel::render(
    float
) const -> void {
    auto& selection = state_->volume.selection;
    if (!selection) {
        return;
    }

    auto& box      = selection->box;
    const auto top = selection->volume_size;

    begin_panel(*state_, panel_slot::left, "Selection");

    if (imgui_drag_vec3i("Min", box.min, label_column)) {
        for (std::size_t i = 0; i < 3; ++i) {
            box.min[i] = std::clamp(box.min[i], 0, box.max[i]);
        }
    }

    if (imgui_drag_vec3i("Max", box.max, label_column)) {
        for (std::size_t i = 0; i < 3; ++i) {
            box.max[i] = std::clamp(box.max[i], box.min[i], top[i] - 1);
        }
    }

    const auto size = box.size();
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted("Size");
    ImGui::SameLine(label_column);
    ImGui::TextDisabled("%dx%dx%d voxels", size.x, size.y, size.z);

    ImGui::Separator();

    const bool fills = ImGui::Button("Fill");
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Turn every cell of the selection into the palette voxel");
    }

    ImGui::SameLine();
    const bool recolors = ImGui::Button("Recolor");
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Repaint the voxels of the selection, keep its air");
    }

    end_panel(*state_, panel_slot::left);

    if (fills) {
        clipboard_service_->fill(asset::fill_scope::every_cell);
    } else if (recolors) {
        clipboard_service_->fill(asset::fill_scope::solid_only);
    }
}

}  // namespace vw::sculptor
