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

entity_properties_panel::entity_properties_panel(
    engine_type& eng, app_state& st, operation_manager& op_manager
)
    : engine_(&eng)
    , state_(&st)
    , op_manager_(&op_manager)
    , add_model_modal_(eng, st, op_manager) {}

auto entity_properties_panel::render(float /*delta_time*/) -> void {
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

    if (state_->scene.selected_name.empty()) {
        ImGui::TextDisabled("No entity selected");
    } else {
        const auto ent    = state_->scene.name_to_entity[state_->scene.selected_name];
        const auto& world = engine_->get_world();

        ImGui::Text(
            "Selected: %s %u.%u",
            state_->scene.selected_name.c_str(),
            ent.index,
            ent.generation
        );

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        if (!world.has<ecs::transform_component>(ent)) {
            ImGui::TextColored(ImVec4(1.f, 1.f, 0.f, 1.f), "Entity has no transform component");
        } else {
            ImGui::BeginDisabled(state_->anim.animation_mode);
            render_position();
            render_rotation();
            render_scale();
            ImGui::EndDisabled();
        }

        ImGui::Spacing();
        render_components_section();
    }

    ImGui::Dummy({200.0f, 0.0f});

    state_->ui.right_top_voffset += ImGui::GetWindowHeight() + 10.0f;

    add_model_modal_.render();

    ImGui::End();
}

auto entity_properties_panel::render_position() const -> void {
    const auto ent             = state_->scene.name_to_entity[state_->scene.selected_name];
    auto& world                = engine_->get_world();
    const auto& transform_comp = world.get<ecs::transform_component>(ent);
    vec3f position             = transform_comp.get_position();
    if (imgui_drag_vec3f("Pos", position)) {
        transform new_transform = transform_comp.get_transform();
        new_transform.set_position(position);
        set_transform_params params = {
            .name          = state_->scene.selected_name,
            .new_transform = new_transform,
        };
        op_manager_->execute(std::make_unique<set_transform_operation>(*engine_, *state_, params));
    }
}

auto entity_properties_panel::render_rotation() const -> void {
    const auto& name           = state_->scene.selected_name;
    const auto ent             = state_->scene.name_to_entity[name];
    auto& world                = engine_->get_world();
    const auto& transform_comp = world.get<ecs::transform_component>(ent);

    const auto& current_quat = transform_comp.get_rotation();
    if (cached_rotation_entity_ != name || cached_rotation_quat_ != current_quat) {
        cached_rotation_entity_ = name;
        cached_rotation_quat_   = current_quat;
        const vec3f euler       = transform_comp.get_rotation_euler();
        cached_rotation_deg_    = {
            math::degrees(euler.x),
            math::degrees(euler.y),
            math::degrees(euler.z),
        };
    }

    if (imgui_drag_vec3f("Rot", cached_rotation_deg_)) {
        const vec3f rotation_rad = {
            math::radians(cached_rotation_deg_.x),
            math::radians(cached_rotation_deg_.y),
            math::radians(cached_rotation_deg_.z),
        };
        transform new_transform = transform_comp.get_transform();
        new_transform.set_rotation_euler(rotation_rad);
        cached_rotation_quat_ = new_transform.get_rotation();
        set_transform_params params = {
            .name          = name,
            .new_transform = new_transform,
        };
        op_manager_->execute(std::make_unique<set_transform_operation>(*engine_, *state_, params));
    }
}

auto entity_properties_panel::render_scale() const -> void {
    const auto ent       = state_->scene.name_to_entity[state_->scene.selected_name];
    auto& world          = engine_->get_world();
    auto& transform_comp = world.get<ecs::transform_component>(ent);
    vec3f scale          = transform_comp.get_scale();
    if (imgui_drag_vec3f("Scale", scale)) {
        transform new_transform = transform_comp.get_transform();
        new_transform.set_scale(scale);
        set_transform_params params = {
            .name          = state_->scene.selected_name,
            .new_transform = new_transform,
        };
        op_manager_->execute(std::make_unique<set_transform_operation>(*engine_, *state_, params));
    }
}

auto entity_properties_panel::render_components_section() -> void {
    if (!ImGui::CollapsingHeader("Components")) {
        return;
    }

    const auto& name = state_->scene.selected_name;
    const auto ent   = state_->scene.name_to_entity.at(name);
    auto& world      = engine_->get_world();

    ImGui::BeginDisabled(state_->anim.animation_mode);

    const bool has_model = world.has<ecs::model_component>(ent);
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted("Model");
    ImGui::SameLine(100.f);
    if (has_model) {
        const auto& model_comp = world.get<ecs::model_component>(ent);
        const auto model_size  = model_comp.size();

        // Набор — только на чтение: он задан конструктором модели и не меняется,
        // а видеть его надо, иначе о том, чем модель красится, сказать нечего.
        const auto model      = model_comp.get_model();
        const block_set* set  = model
             ? engine_->get_block_registry().set_of(model->category())
             : nullptr;
        const std::string_view set_name = set != nullptr ? set->name : "unknown";

        ImGui::TextDisabled(
            "(%dx%dx%d) %.*s", model_size.x, model_size.y, model_size.z,
            static_cast<int>(set_name.size()), set_name.data()
        );
        ImGui::SameLine();
        if (ImGui::Button("Remove##model")) {
            op_manager_->execute(std::make_unique<remove_model_component_operation>(
                *engine_, *state_, remove_model_component_params{.name = name}
            ));
        }

        // Точка вращения объёма: узел садится на неё, а поддерево её не видит.
        vec3f pivot = model_comp.get_pivot();
        if (imgui_drag_vec3f("Pivot", pivot)) {
            op_manager_->execute(std::make_unique<set_pivot_operation>(
                *engine_, *state_, set_pivot_params{.name = name, .new_pivot = pivot}
            ));
        }
    } else {
        if (ImGui::Button("Add##model")) {
            add_model_modal_.open(name);
        }
    }

    const bool has_socket = world.has<ecs::socket_component>(ent);
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted("Socket");
    ImGui::SameLine(100.f);
    if (has_socket) {
        if (ImGui::Button("Remove##socket")) {
            op_manager_->execute(std::make_unique<remove_socket_component_operation>(
                *engine_, *state_, remove_socket_component_params{.name = name}
            ));
        }
    } else {
        if (ImGui::Button("Add##socket")) {
            op_manager_->execute(std::make_unique<add_socket_component_operation>(
                *engine_, *state_, add_socket_component_params{.name = name}
            ));
        }
    }

    const bool has_anim_target = world.has<ecs::animation_target_component>(ent);
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted("Anim. Target");
    ImGui::SameLine(100.f);
    if (has_anim_target) {
        const auto& target_comp = world.get<ecs::animation_target_component>(ent);
        ImGui::TextDisabled("(%s)", target_comp.get_name().c_str());
        ImGui::SameLine();
        if (ImGui::Button("Remove##anim_target")) {
            op_manager_->execute(std::make_unique<remove_animation_target_operation>(
                *engine_, *state_, remove_animation_target_params{.entity_name = name}
            ));
        }
    } else {
        if (ImGui::Button("Add##anim_target")) {
            op_manager_->execute(std::make_unique<add_animation_target_operation>(
                *engine_, *state_,
                add_animation_target_params{.entity_name = name, .target_name = name}
            ));
        }
    }

    ImGui::EndDisabled();
}

}  // namespace vw::sculptor
