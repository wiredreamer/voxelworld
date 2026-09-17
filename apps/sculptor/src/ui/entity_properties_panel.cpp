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
    float /*delta_time*/
) -> void {
    // Внутри объёма панель — про сам объём, и называется соответственно: узел со
    // своим составом и трансформом правят в префабе, а сюда провалились за
    // вокселями, точкой вращения и обрезкой.
    const bool volume_only = state_->ctx.kind() == edit_kind::model;

    begin_panel(
        *state_,
        panel_slot::right,
        volume_only ? "Volume###EntityProperties" : "Entity Properties###EntityProperties"
    );

    // Внутри объёма панель говорит про узел из крошек, а не про выделение: туда
    // выделение не ходит, а undo умеет вернуть контекст чужого объёма.
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

        // Панель — обход реестра, а не список секций: новый компонент появляется
        // здесь той же регистрацией, которой он появился в файле.
        for (const auto& drawer : default_drawers().all()) {
            if (!drawer.draw || !ecs::has_component(world, ent, drawer.component)) {
                continue;
            }

            if (volume_only && drawer.scope != drawer_scope::volume) {
                continue;
            }

            // Секция объёма целиком не гасится: из префаба в объём входят её
            // кнопкой, а что в ней можно менять, она решает сама.
            const bool editable = drawer.scope == drawer_scope::volume || state_->ctx.in_prefab();

            ImGui::SeparatorText(drawer.title.c_str());

            // Секции подписывают кнопки глаголом, а существительное берут из
            // заголовка. Своя область ID нужна, чтобы «Add» двух секций не стала
            // для ImGui одной кнопкой.
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

    // Состав — свойство узла целиком, а не одной из секций, поэтому кнопка живёт
    // в строке с его именем и прижата к краю: так она не спорит с кнопками
    // секций и не отнимает под себя отдельную строку внизу.
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
