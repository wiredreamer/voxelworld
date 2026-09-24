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
namespace {

auto to_imvec4(color clr) -> ImVec4 {
    return {
        static_cast<float>(clr.r()) / 255.0f,
        static_cast<float>(clr.g()) / 255.0f,
        static_cast<float>(clr.b()) / 255.0f,
        1.0f
    };
}

constexpr int32 swatches_per_row = 6;

}  // namespace


voxel_palette_panel::voxel_palette_panel(
    engine_type& eng, app_state& st
)
    : engine_(&eng), state_(&st) {}

auto voxel_palette_panel::swatch_(
    const voxel_type& type, int32 index_in_row
) -> void {
    if (index_in_row % swatches_per_row != 0) {
        ImGui::SameLine(0, 0);
    }

    ImGui::PushID(static_cast<int>(type.id.value));

    constexpr ImGuiColorEditFlags btn_flags =  //
        ImGuiColorEditFlags_NoAlpha |          //
        ImGuiColorEditFlags_NoPicker |         //
        ImGuiColorEditFlags_NoBorder;

    if (ImGui::ColorButton(
            "##voxel", to_imvec4(type.material.clr), btn_flags, ImVec2(30.0f, 30.0f)
        )) {
        state_->tool.selected_voxel = type.id;
    }
    ImGui::SetItemTooltip("%.*s", static_cast<int>(type.name.size()), type.name.data());

    ImGui::PopID();
}

auto voxel_palette_panel::render(
    [[maybe_unused]] float delta_time
) -> void {
    const voxel_registry& registry = engine_->get_voxel_registry();

    begin_panel(*state_, panel_slot::bottom, "Voxel Palette");

    const voxel_type& selected = registry.get(state_->tool.selected_voxel);
    const color selected_color = selected.material.clr;

    ImGui::ColorButton(
        "##current_block",
        to_imvec4(selected_color),
        ImGuiColorEditFlags_NoAlpha | ImGuiColorEditFlags_NoPicker,
        ImVec2(ImGui::GetContentRegionAvail().x, 40.0f)
    );
    ImGui::Text("%.*s", static_cast<int>(selected.name.size()), selected.name.data());
    ImGui::Text(
        "#%02X%02X%02X  %u",
        selected_color.r(),
        selected_color.g(),
        selected_color.b(),
        selected.id.value
    );

    if (selected.material.emission != 0 || selected.material.glow != 0) {
        ImGui::Text(
            "emits %u, glows %u", selected.material.emission, selected.material.glow
        );
    }

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    for (const voxel_group& group : registry.groups()) {
        ImGui::SeparatorText(std::string{group.name}.c_str());

        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0, 0));
        int32 in_row = 0;
        for (uint8 offset = 0; offset < group.count; ++offset) {
            const voxel& id = group.at(offset);
            if (!registry.known(id)) {
                continue;
            }
            swatch_(registry.get(id), in_row);
            ++in_row;
        }
        ImGui::PopStyleVar();
    }

    end_panel(*state_, panel_slot::bottom);
}


}  // namespace vw::sculptor
