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
    engine_type& eng, app_state& st, operation_manager& op_manager
)
    : engine_(&eng), state_(&st), op_manager_(&op_manager), add_model_modal_(eng, st, op_manager) {}

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

    constexpr ImGuiWindowFlags flags =  //
        ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoMove;

    if (ImGui::BeginPopupModal("Components", nullptr, flags)) {
        const auto it = state_->scene.name_to_entity.find(entity_name_);
        if (it == state_->scene.name_to_entity.end()) {
            // Узел исчез из-под диалога: undo создания, удаление, переоткрытие.
            ImGui::CloseCurrentPopup();
        } else {
            ImGui::Text("Entity: %s", entity_name_.c_str());
            ImGui::Separator();
            ImGui::Spacing();

            render_model_row_(it->second);
            render_socket_row_(it->second);
            render_target_row_(it->second);

            ImGui::Spacing();
            ImGui::Separator();
            ImGui::Spacing();

            if (ImGui::Button("Close")) {
                ImGui::CloseCurrentPopup();
            }
        }

        ImGui::EndPopup();
    }

    // Диалог объёма открывается только после того, как этот закрылся: вложенных
    // модальных окон не бывает, да и выбирать размер поверх списка состава
    // нечитаемо.
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

auto edit_components_modal::render_model_row_(
    ecs::entity ent
) -> void {
    auto& world = engine_->get_world();

    if (!world.has<ecs::model_component>(ent)) {
        begin_row_("Model", "none");
        if (ImGui::Button("Add##model")) {
            // Объёму нужны размер и набор блоков, поэтому строка передаёт работу
            // своему диалогу, а не заводит компонент на месте.
            add_model_modal_.open(entity_name_);
            ImGui::CloseCurrentPopup();
        }
        return;
    }

    const auto size = world.get<ecs::model_component>(ent).size();
    begin_row_("Model", std::format("{}x{}x{}", size.x, size.y, size.z));
    if (ImGui::Button("Remove##model")) {
        op_manager_->execute(
            std::make_unique<remove_model_component_operation>(
                *engine_, *state_, remove_model_component_params{.name = entity_name_}
            )
        );
    }
}

auto edit_components_modal::render_socket_row_(
    ecs::entity ent
) -> void {
    auto& world = engine_->get_world();

    if (!world.has<ecs::socket_component>(ent)) {
        begin_row_("Sockets", "none");
        if (ImGui::Button("Add##sockets")) {
            op_manager_->execute(
                std::make_unique<add_socket_component_operation>(
                    *engine_, *state_, add_socket_component_params{.name = entity_name_}
                )
            );
        }
        return;
    }

    const auto count = world.get<ecs::socket_component>(ent).get_sockets().size();
    begin_row_("Sockets", std::format("{} point(s)", count));
    if (ImGui::Button("Remove##sockets")) {
        op_manager_->execute(
            std::make_unique<remove_socket_component_operation>(
                *engine_, *state_, remove_socket_component_params{.name = entity_name_}
            )
        );
    }
}

auto edit_components_modal::render_target_row_(
    ecs::entity ent
) -> void {
    auto& world = engine_->get_world();

    if (!world.has<ecs::animation_target_component>(ent)) {
        begin_row_("Anim. target", "none");
        if (ImGui::Button("Add##anim_target")) {
            op_manager_->execute(
                std::make_unique<add_animation_target_operation>(
                    *engine_,
                    *state_,
                    add_animation_target_params{
                        .entity_name = entity_name_, .target_name = entity_name_
                    }
                )
            );
        }
        return;
    }

    const auto& target = world.get<ecs::animation_target_component>(ent);
    begin_row_("Anim. target", target.get_name());
    if (ImGui::Button("Remove##anim_target")) {
        op_manager_->execute(
            std::make_unique<remove_animation_target_operation>(
                *engine_, *state_, remove_animation_target_params{.entity_name = entity_name_}
            )
        );
    }
}

}  // namespace vw::sculptor
