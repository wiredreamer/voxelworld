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

entity_properties_panel::entity_properties_panel(
    engine_type& eng, app_state& st, operation_manager& op_manager, asset::model_library& library
)
    : engine_(&eng)
    , state_(&st)
    , op_manager_(&op_manager)
    , library_(&library)
    , components_modal_(eng, st, op_manager, library)
    , candidate_modal_(eng, st, op_manager, library) {}

auto entity_properties_panel::render(
    float
) -> void {
    const bool volume_only = state_->ctx.kind() == edit_kind::model;

    begin_panel(
        *state_,
        panel_slot::right,
        volume_only ? "Volume###EntityProperties" : "Entity Properties###EntityProperties"
    );

    const auto& name = state_->edited_node();

    if (!state_->scene.name_to_entity.contains(name)) {
        ImGui::TextDisabled("No entity selected");
    } else {
        const auto ent = state_->scene.name_to_entity[name];

        render_header_(ent, name, !volume_only);

        const component_drawer_context context{
            .engine    = *engine_,
            .state     = *state_,
            .ops       = *op_manager_,
            .library   = *library_,
            .ent       = ent,
            .node_name = name,
        };

        auto& world = engine_->get_world();

        for (const auto& drawer : default_drawers().all()) {
            if (!drawer.draw || !ecs::has_component(world, ent, drawer.component)) {
                continue;
            }

            if (volume_only && drawer.scope != drawer_scope::volume) {
                continue;
            }

            const bool editable = drawer.scope == drawer_scope::volume || state_->ctx.in_prefab();

            ImGui::SeparatorText(drawer.title.c_str());

            ImGui::PushID(drawer.tag.c_str());
            ImGui::BeginDisabled(!editable);
            drawer.draw(context);
            ImGui::EndDisabled();
            ImGui::PopID();
        }
    }

    ImGui::Dummy({240.0f, 0.0f});

    components_modal_.render();
    candidate_modal_.render();

    end_panel(*state_, panel_slot::right);
}

auto entity_properties_panel::render_header_(
    ecs::entity ent, const std::string& name, bool composable
) -> void {
    ImGui::TextUnformatted(name.c_str());
    ImGui::SameLine();
    ImGui::TextDisabled("%u.%u", ent.index, ent.generation);

    if (!composable) {
        return;
    }

    constexpr std::string_view label = "Edit";
    const float32 width =
        ImGui::CalcTextSize(label.data()).x + (ImGui::GetStyle().FramePadding.x * 2.f);

    ImGui::SameLine();
    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + std::max(0.f, ImGui::GetContentRegionAvail().x - width));
    if (ImGui::SmallButton(label.data())) {
        components_modal_.open(name);
    }
    ImGui::SetItemTooltip("Add or remove components");
}

}  // namespace vw::sculptor
