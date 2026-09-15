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

constexpr float32 summary_column = 130.f;
constexpr float32 action_column  = 250.f;

}  // namespace

edit_components_modal::edit_components_modal(
    engine_type& eng, app_state& st, operation_manager& op_manager, asset::model_library& library
)
    : engine_(&eng)
    , state_(&st)
    , op_manager_(&op_manager)
    , library_(&library)
    , add_model_modal_(eng, st, op_manager) {}

auto edit_components_modal::open(
    const std::string& entity_name
) -> void {
    need_open_   = true;
    entity_name_ = entity_name;
}

auto edit_components_modal::render() -> void {
    if (need_open_) {
        ImGui::OpenPopup("Components");
        need_open_ = false;
    }

    // Компоненту, которому мало одной кнопки, отвечает свой диалог, а этот
    // уходит с дороги: вложенных модальных окон не бывает, да и выбирать размер
    // объёма поверх списка состава нечитаемо.
    if (!state_->ui.need_add_model_for.empty()) {
        add_model_modal_.open(state_->ui.need_add_model_for);
        state_->ui.need_add_model_for.clear();
        need_close_ = true;
    }

    constexpr ImGuiWindowFlags flags =  //
        ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoMove;

    if (ImGui::BeginPopupModal("Components", nullptr, flags)) {
        const auto it = state_->scene.name_to_entity.find(entity_name_);

        if (need_close_ || it == state_->scene.name_to_entity.end()) {
            // Либо уступили место другому диалогу, либо узел исчез из-под этого:
            // undo создания, удаление, переоткрытие.
            need_close_ = false;
            ImGui::CloseCurrentPopup();
        } else {
            ImGui::Text("Entity: %s", entity_name_.c_str());
            ImGui::Separator();
            ImGui::Spacing();

            // Состав — тот же реестр, что и панель: компонент, который нечем
            // поставить и нечем снять, в диалоге про состав не показывается.
            for (const auto& drawer : default_drawers().all()) {
                if (!drawer.add && !drawer.remove) {
                    continue;
                }

                render_row_(drawer, it->second);
            }

            ImGui::Spacing();
            ImGui::Separator();
            ImGui::Spacing();

            if (ImGui::Button("Close")) {
                ImGui::CloseCurrentPopup();
            }
        }

        ImGui::EndPopup();
    }

    add_model_modal_.render();
}

auto edit_components_modal::begin_row_(
    std::string_view label, std::string_view summary
) -> void {
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(label.data(), label.data() + label.size());
    ImGui::SameLine(summary_column);
    ImGui::TextDisabled("%.*s", static_cast<int>(summary.size()), summary.data());
    ImGui::SameLine(action_column);
}

auto edit_components_modal::render_row_(
    const component_drawer& drawer, ecs::entity ent
) -> void {
    const component_drawer_context context{
        .engine    = *engine_,
        .state     = *state_,
        .ops       = *op_manager_,
        .library   = *library_,
        .ent       = ent,
        .node_name = entity_name_,
    };

    const bool present = ecs::has_component(engine_->get_world(), ent, drawer.component);
    const auto summary = present && drawer.summary ? drawer.summary(context) : std::string{"none"};

    ImGui::PushID(drawer.tag.c_str());
    begin_row_(drawer.title, summary);

    if (present) {
        if (drawer.remove && ImGui::Button("Remove")) {
            drawer.remove(context);
        }
    } else if (drawer.add && ImGui::Button("Add")) {
        drawer.add(context);
    }

    ImGui::PopID();
}

}  // namespace vw::sculptor
