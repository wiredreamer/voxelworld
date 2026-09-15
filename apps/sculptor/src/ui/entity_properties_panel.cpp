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

// Ширина колонки имени: значения секций встают в один столбец, иначе панель
// читается как набор несвязанных строк.
constexpr float32 label_column = 70.f;

auto field_label(
    std::string_view label
) -> void {
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(label.data(), label.data() + label.size());
    ImGui::SameLine(label_column);
}

}  // namespace

entity_properties_panel::entity_properties_panel(
    engine_type& eng, app_state& st, operation_manager& op_manager
)
    : engine_(&eng)
    , state_(&st)
    , op_manager_(&op_manager)
    , components_modal_(eng, st, op_manager) {}

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

        // Сам узел и его состав правятся только в префабе: в клипе позу ведёт
        // дорожка, а внутри объёма речь идёт про объём. Объём поэтому и остаётся
        // живым — провалившись в него, правят именно его.
        const bool node_editable = state_->ctx.in_prefab();

        ImGui::BeginDisabled(!node_editable);
        render_transform_(ent);
        ImGui::EndDisabled();

        render_model_(ent);

        ImGui::BeginDisabled(!node_editable);

        render_sockets_(ent);
        render_rig_(ent);
        render_animation_target_(ent);

        ImGui::Spacing();
        ImGui::Spacing();
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

auto entity_properties_panel::render_transform_(
    ecs::entity ent
) const -> void {
    if (!engine_->get_world().has<ecs::transform_component>(ent)) {
        ImGui::TextColored(ImVec4(1.f, 1.f, 0.f, 1.f), "Entity has no transform component");
        return;
    }

    ImGui::SeparatorText("Transform");

    render_position_();
    render_rotation_();
    render_scale_();
}

auto entity_properties_panel::render_position_() const -> void {
    const auto& name           = state_->edited_node();
    const auto ent             = state_->scene.name_to_entity[name];
    auto& world                = engine_->get_world();
    const auto& transform_comp = world.get<ecs::transform_component>(ent);
    vec3f position             = transform_comp.get_position();
    if (imgui_drag_vec3f("Pos", position, label_column)) {
        transform new_transform = transform_comp.get_transform();
        new_transform.set_position(position);
        set_transform_params params = {
            .name          = name,
            .new_transform = new_transform,
        };
        op_manager_->execute(std::make_unique<set_transform_operation>(*engine_, *state_, params));
    }
}

auto entity_properties_panel::render_rotation_() const -> void {
    const auto& name           = state_->edited_node();
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

    if (imgui_drag_vec3f("Rot", cached_rotation_deg_, label_column)) {
        const vec3f rotation_rad = {
            math::radians(cached_rotation_deg_.x),
            math::radians(cached_rotation_deg_.y),
            math::radians(cached_rotation_deg_.z),
        };
        transform new_transform = transform_comp.get_transform();
        new_transform.set_rotation_euler(rotation_rad);
        cached_rotation_quat_       = new_transform.get_rotation();
        set_transform_params params = {
            .name          = name,
            .new_transform = new_transform,
        };
        op_manager_->execute(std::make_unique<set_transform_operation>(*engine_, *state_, params));
    }
}

auto entity_properties_panel::render_scale_() const -> void {
    const auto& name     = state_->edited_node();
    const auto ent       = state_->scene.name_to_entity[name];
    auto& world          = engine_->get_world();
    auto& transform_comp = world.get<ecs::transform_component>(ent);
    vec3f scale          = transform_comp.get_scale();
    if (imgui_drag_vec3f("Scale", scale, label_column)) {
        transform new_transform = transform_comp.get_transform();
        new_transform.set_scale(scale);
        set_transform_params params = {
            .name          = name,
            .new_transform = new_transform,
        };
        op_manager_->execute(std::make_unique<set_transform_operation>(*engine_, *state_, params));
    }
}

auto entity_properties_panel::render_model_(
    ecs::entity ent
) -> void {
    auto& world = engine_->get_world();
    if (!world.has<ecs::model_component>(ent)) {
        return;
    }

    ImGui::SeparatorText("Model");

    const auto& name       = state_->edited_node();
    const auto& model_comp = world.get<ecs::model_component>(ent);

    // Имя файла, а не весь путь: панель прижата к правому краю, и полный путь
    // раздвинул бы её на треть экрана ради строки, которая и так одна и та же
    // у всех узлов префаба.
    const auto& source = model_comp.get_source();
    field_label("Volume");
    if (source.empty()) {
        ImGui::TextDisabled("unsaved");
    } else {
        const auto file_name = std::format("{}{}", source.stem(), source.extension());
        ImGui::TextDisabled("%s", file_name.c_str());
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
            ImGui::SetTooltip("%s", source.str().c_str());
        }
    }

    // Набор — только на чтение: он задан конструктором модели и не меняется,
    // а видеть его надо, иначе о том, чем модель красится, сказать нечего.
    const auto model = model_comp.get_model();
    const block_set* set =
        model ? engine_->get_block_registry().set_of(model->category()) : nullptr;
    const std::string_view set_name = set != nullptr ? set->name : "unknown";

    const auto model_size = model_comp.size();
    field_label("Size");
    ImGui::TextDisabled(
        "%dx%dx%d, %.*s",
        model_size.x,
        model_size.y,
        model_size.z,
        static_cast<int>(set_name.size()),
        set_name.data()
    );

    // Занятый объём показывается, только когда он меньше габарита: совпали —
    // говорить не о чем, а разошлись — это и есть ответ на вопрос, почему модель
    // болтается внутри себя и что срежет Trim.
    const auto& bounds  = occupied_bounds_(model.get());
    const bool can_trim = bounds.has_value() && bounds->size() != model_size;
    if (!bounds.has_value()) {
        field_label("Occupied");
        ImGui::TextDisabled("empty");
    } else if (can_trim) {
        const auto occupied = bounds->size();
        field_label("Occupied");
        ImGui::TextDisabled("%dx%dx%d", occupied.x, occupied.y, occupied.z);
    }

    ImGui::BeginDisabled(!state_->ctx.allows_volume_edit());

    // Точка вращения объёма: узел садится на неё, а поддерево её не видит.
    vec3f pivot = model_comp.get_pivot();
    if (imgui_drag_vec3f("Pivot", pivot, label_column)) {
        op_manager_->execute(
            std::make_unique<set_pivot_operation>(
                *engine_, *state_, set_pivot_params{.name = name, .new_pivot = pivot}
            )
        );
    }

    if (can_trim && ImGui::Button("Trim")) {
        op_manager_->execute(
            std::make_unique<trim_model_operation>(
                *engine_, *state_, trim_model_params{.name = name}
            )
        );
    }

    ImGui::EndDisabled();

    if (state_->ctx.in_prefab()) {
        if (can_trim) {
            ImGui::SameLine();
        }
        if (ImGui::Button("Edit voxels")) {
            state_->ctx.enter(edit_context::model(name));
        }
    }
}

auto entity_properties_panel::occupied_bounds_(
    const asset::model* model
) const -> const std::optional<asset::voxel_bounds>& {
    if (model == nullptr) {
        cached_bounds_id_ = asset::invalid_model_identity;
        cached_bounds_.reset();
        return cached_bounds_;
    }

    // Обход всего объёма ради строки в панели — только когда модель менялась: у
    // 64-куба это четверть миллиона проверок, и каждый кадр их делать не за что.
    const auto id = model->get_identity();
    if (id != cached_bounds_id_) {
        cached_bounds_id_ = id;
        cached_bounds_    = asset::occupied_bounds(*model);
    }

    return cached_bounds_;
}

auto entity_properties_panel::render_sockets_(
    ecs::entity ent
) const -> void {
    const auto& world = engine_->get_world();
    if (!world.has<ecs::socket_component>(ent)) {
        return;
    }

    ImGui::SeparatorText("Sockets");

    const auto& sockets = world.get<ecs::socket_component>(ent).get_sockets();
    field_label("Points");
    ImGui::TextDisabled("%d", static_cast<int32>(sockets.size()));

    // Сами точки правит своя панель: здесь их список занял бы больше места, чем
    // всё остальное вместе.
    if (!state_->ui.show_sockets && ImGui::Button("Show sockets")) {
        state_->ui.show_sockets = true;
    }
}

auto entity_properties_panel::render_rig_(
    ecs::entity ent
) const -> void {
    const auto& world = engine_->get_world();
    if (!world.has<ecs::rig_component>(ent)) {
        return;
    }

    ImGui::SeparatorText("Rig");

    const auto& rig_name = world.get<ecs::rig_component>(ent).get_name();
    field_label("Name");
    ImGui::TextDisabled("%s", rig_name.empty() ? "unnamed" : rig_name.c_str());

    if (!state_->ui.show_rig && ImGui::Button("Show rig")) {
        state_->ui.show_rig = true;
    }
}

auto entity_properties_panel::render_animation_target_(
    ecs::entity ent
) const -> void {
    const auto& world = engine_->get_world();
    if (!world.has<ecs::animation_target_component>(ent)) {
        return;
    }

    ImGui::SeparatorText("Animation target");

    const auto& target_comp = world.get<ecs::animation_target_component>(ent);
    field_label("Name");
    ImGui::TextDisabled("%s", target_comp.get_name().c_str());
}

}  // namespace vw::sculptor
