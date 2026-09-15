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
    , components_modal_(eng, st, op_manager, library) {}

auto entity_properties_panel::render(
    float /*delta_time*/
) -> void {
    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    const auto window_pos         = ImVec2(
        viewport->WorkPos.x + viewport->WorkSize.x - 10,
        viewport->WorkPos.y + state_->ui.right_top_voffset + 10
    );
    ImGui::SetNextWindowPos(window_pos, ImGuiCond_Always, ImVec2(1.0f, 0.0f));

    constexpr ImGuiWindowFlags window_flags =  //
        ImGuiWindowFlags_NoSavedSettings |     //
        ImGuiWindowFlags_NoMove |              //
        ImGuiWindowFlags_AlwaysAutoResize;

    ImGui::Begin("Entity Properties", nullptr, window_flags);

    // Внутри объёма панель говорит про узел из крошек, а не про выделение: туда
    // выделение не ходит, а undo умеет вернуть контекст чужого объёма.
    const auto& name = state_->edited_node();

    if (!state_->scene.name_to_entity.contains(name)) {
        ImGui::TextDisabled("No entity selected");
    } else {
        const auto ent = state_->scene.name_to_entity[name];

        render_header_(ent);

        const component_drawer_context context{
            .engine    = *engine_,
            .state     = *state_,
            .ops       = *op_manager_,
            .library   = *library_,
            .ent       = ent,
            .node_name = name,
        };

        auto& world = engine_->get_world();

        // Панель — обход реестра, а не список секций: новый компонент появляется
        // здесь той же регистрацией, которой он появился в файле.
        for (const auto& drawer : default_drawers().all()) {
            if (!drawer.draw || !ecs::has_component(world, ent, drawer.component)) {
                continue;
            }

            const bool editable = drawer.scope == drawer_scope::volume ?
                state_->ctx.allows_volume_edit() :
                state_->ctx.in_prefab();

            ImGui::SeparatorText(drawer.title.c_str());

            ImGui::BeginDisabled(!editable);
            drawer.draw(context);
            ImGui::EndDisabled();
        }

        ImGui::Spacing();
        ImGui::Spacing();

        ImGui::BeginDisabled(!state_->ctx.in_prefab());
        if (ImGui::Button("Components...")) {
            components_modal_.open(name);
        }
        ImGui::EndDisabled();
    }

    ImGui::Dummy({240.0f, 0.0f});

    state_->ui.right_top_voffset += ImGui::GetWindowHeight() + 10.0f;

    components_modal_.render();

    ImGui::End();
}

auto entity_properties_panel::render_header_(
    ecs::entity ent
) const -> void {
    ImGui::TextUnformatted(state_->edited_node().c_str());
    ImGui::SameLine();
    ImGui::TextDisabled("%u.%u", ent.index, ent.generation);
}

}  // namespace vw::sculptor
