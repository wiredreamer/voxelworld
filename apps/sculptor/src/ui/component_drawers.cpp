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

auto field_label(std::string_view label) -> void {
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(label.data(), label.data() + label.size());
    ImGui::SameLine(label_column);
}

// Углы показываются в градусах, а живут кватернионом, и обратный перевод не
// однозначен: пока крутят поле, оно обязано показывать набранное, а не то, что
// получилось после двух преобразований. Отсюда память между кадрами — на один
// узел, потому что правят всегда один.
struct rotation_memory {
    std::string node;
    quat value;
    vec3f degrees;
};

auto rotation_of(const ecs::transform_component& transform_comp, const std::string& node)
    -> vec3f& {
    static rotation_memory memory;

    const auto& current = transform_comp.get_rotation();
    if (memory.node != node || memory.value != current) {
        memory.node    = node;
        memory.value   = current;
        const auto rad = transform_comp.get_rotation_euler();
        memory.degrees = {math::degrees(rad.x), math::degrees(rad.y), math::degrees(rad.z)};
    }

    return memory.degrees;
}

auto commit_transform(const component_drawer_context& in, const transform& value) -> void {
    in.ops.execute(
        std::make_unique<set_transform_operation>(
            in.engine, in.state,
            set_transform_params{.name = in.node_name, .new_transform = value}
        )
    );
}

auto register_transform(component_drawer_registry& drawers) -> void {
    component_drawer drawer;
    drawer.tag   = "transform";
    drawer.title = "Transform";

    drawer.draw = [](const component_drawer_context& in) {
        const auto& transform_comp = in.engine.get_world().get<ecs::transform_component>(in.ent);

        vec3f position = transform_comp.get_position();
        if (imgui_drag_vec3f("Pos", position, label_column)) {
            auto next = transform_comp.get_transform();
            next.set_position(position);
            commit_transform(in, next);
        }

        auto& degrees = rotation_of(transform_comp, in.node_name);
        if (imgui_drag_vec3f("Rot", degrees, label_column)) {
            auto next = transform_comp.get_transform();
            next.set_rotation_euler(
                vec3f{
                    math::radians(degrees.x), math::radians(degrees.y), math::radians(degrees.z)
                }
            );
            commit_transform(in, next);
        }

        vec3f scale = transform_comp.get_scale();
        if (imgui_drag_vec3f("Scale", scale, label_column)) {
            auto next = transform_comp.get_transform();
            next.set_scale(scale);
            commit_transform(in, next);
        }
    };

    drawers.register_for<ecs::transform_component>(std::move(drawer));
}

auto register_model(component_drawer_registry& drawers) -> void {
    component_drawer drawer;
    drawer.tag   = "model";
    drawer.title = "Model";
    drawer.scope = drawer_scope::volume;

    drawer.draw = [](const component_drawer_context& in) {
        const auto& model_comp = in.engine.get_world().get<ecs::model_component>(in.ent);

        // Имя файла, а не весь путь: панель прижата к правому краю, и полный
        // путь раздвинул бы её на треть экрана ради строки, которая и так одна
        // и та же у всех узлов префаба.
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
        const auto model     = model_comp.get_model();
        const block_set* set = model ?
            in.engine.get_block_registry().set_of(model->category()) :
            nullptr;
        const std::string_view set_name = set != nullptr ? set->name : "unknown";

        const auto model_size = model_comp.size();
        field_label("Size");
        ImGui::TextDisabled(
            "%dx%dx%d, %.*s", model_size.x, model_size.y, model_size.z,
            static_cast<int>(set_name.size()), set_name.data()
        );

        // Занятый объём показывается, только когда он меньше габарита: совпали —
        // говорить не о чем, разошлись — это и есть ответ на вопрос, почему
        // модель болтается внутри себя и что срежет Trim.
        const auto& bounds  = in.state.volume.occupied;
        const bool can_trim = bounds.has_value() && bounds->size() != model_size;
        if (!bounds.has_value()) {
            field_label("Occupied");
            ImGui::TextDisabled("empty");
        } else if (can_trim) {
            const auto occupied = bounds->size();
            field_label("Occupied");
            ImGui::TextDisabled("%dx%dx%d", occupied.x, occupied.y, occupied.z);
        }

        // Точка вращения объёма: узел садится на неё, а поддерево её не видит.
        vec3f pivot = model_comp.get_pivot();
        if (imgui_drag_vec3f("Pivot", pivot, label_column)) {
            in.ops.execute(
                std::make_unique<set_pivot_operation>(
                    in.engine, in.state,
                    set_pivot_params{.name = in.node_name, .new_pivot = pivot}
                )
            );
        }

        if (can_trim && ImGui::Button("Trim")) {
            in.ops.execute(
                std::make_unique<trim_model_operation>(
                    in.engine, in.state, trim_model_params{.name = in.node_name}
                )
            );
        }

        if (in.state.ctx.in_prefab()) {
            if (can_trim) {
                ImGui::SameLine();
            }
            if (ImGui::Button("Edit voxels")) {
                in.state.ctx.enter(edit_context::model(in.node_name));
            }
        }
    };

    drawer.summary = [](const component_drawer_context& in) {
        const auto size = in.engine.get_world().get<ecs::model_component>(in.ent).size();
        return std::format("{}x{}x{}", size.x, size.y, size.z);
    };

    // Объёму нужны размер и набор блоков, поэтому строка передаёт работу своему
    // диалогу, а не заводит компонент на месте.
    drawer.add = [](const component_drawer_context& in) {
        in.state.ui.need_add_model_for = in.node_name;
    };

    drawer.remove = [](const component_drawer_context& in) {
        in.ops.execute(
            std::make_unique<remove_model_component_operation>(
                in.engine, in.state, remove_model_component_params{.name = in.node_name}
            )
        );
    };

    drawers.register_for<ecs::model_component>(std::move(drawer));
}

auto register_sockets(component_drawer_registry& drawers) -> void {
    component_drawer drawer;
    drawer.tag   = "socket";
    drawer.title = "Sockets";

    drawer.draw = [](const component_drawer_context& in) {
        const auto& sockets =
            in.engine.get_world().get<ecs::socket_component>(in.ent).get_sockets();

        field_label("Points");
        ImGui::TextDisabled("%d", static_cast<int32>(sockets.size()));

        // Сами точки правит своя панель: здесь их список занял бы больше места,
        // чем всё остальное вместе.
        if (!in.state.ui.show_sockets && ImGui::Button("Show sockets")) {
            in.state.ui.show_sockets = true;
        }
    };

    drawer.summary = [](const component_drawer_context& in) {
        const auto count =
            in.engine.get_world().get<ecs::socket_component>(in.ent).get_sockets().size();
        return std::format("{} point(s)", count);
    };

    drawer.add = [](const component_drawer_context& in) {
        in.ops.execute(
            std::make_unique<add_socket_component_operation>(
                in.engine, in.state, add_socket_component_params{.name = in.node_name}
            )
        );
    };

    drawer.remove = [](const component_drawer_context& in) {
        in.ops.execute(
            std::make_unique<remove_socket_component_operation>(
                in.engine, in.state, remove_socket_component_params{.name = in.node_name}
            )
        );
    };

    drawers.register_for<ecs::socket_component>(std::move(drawer));
}

auto register_anim_target(component_drawer_registry& drawers) -> void {
    component_drawer drawer;
    drawer.tag   = "anim_target";
    drawer.title = "Animation target";

    drawer.draw = [](const component_drawer_context& in) {
        const auto& target =
            in.engine.get_world().get<ecs::animation_target_component>(in.ent);

        field_label("Name");
        ImGui::TextDisabled("%s", target.get_name().c_str());
    };

    drawer.summary = [](const component_drawer_context& in) {
        return in.engine.get_world().get<ecs::animation_target_component>(in.ent).get_name();
    };

    drawer.add = [](const component_drawer_context& in) {
        in.ops.execute(
            std::make_unique<add_animation_target_operation>(
                in.engine, in.state,
                add_animation_target_params{
                    .entity_name = in.node_name, .target_name = in.node_name
                }
            )
        );
    };

    drawer.remove = [](const component_drawer_context& in) {
        in.ops.execute(
            std::make_unique<remove_animation_target_operation>(
                in.engine, in.state, remove_animation_target_params{.entity_name = in.node_name}
            )
        );
    };

    drawers.register_for<ecs::animation_target_component>(std::move(drawer));
}

auto register_variant(component_drawer_registry& drawers) -> void {
    component_drawer drawer;
    drawer.tag   = "variant";
    drawer.title = "Variant";

    drawer.draw = [](const component_drawer_context& in) {
        const auto& slot = in.engine.get_world().get<ecs::variant_slot_component>(in.ent);

        field_label("Slot");
        ImGui::TextDisabled("%s", slot.get_name().c_str());

        const auto& candidates = slot.get_candidates();

        // Кандидаты подписаны именами файлов: путь у них общий и длинный, а
        // различаются они последним куском.
        const auto label_of = [&candidates](std::size_t index) {
            return std::format("{}{}", candidates[index].stem(), candidates[index].extension());
        };

        std::optional<std::size_t> picked;
        std::optional<std::size_t> dropped;

        // Пустой список ничего про себя не пишет: под ним стоит кнопка, и она
        // говорит то же самое короче.
        if (!candidates.empty()) {
            const auto current = slot.get_selected();

            field_label("Current");
            ImGui::SetNextItemWidth(150.f);
            if (ImGui::BeginCombo("##variant", label_of(current).c_str())) {
                for (std::size_t i = 0; i < candidates.size(); ++i) {
                    if (ImGui::Selectable(label_of(i).c_str(), i == current) && i != current) {
                        picked = i;
                    }
                }
                ImGui::EndCombo();
            }

            for (std::size_t i = 0; i < candidates.size(); ++i) {
                ImGui::PushID(static_cast<int>(i));

                field_label("");
                ImGui::TextDisabled("%s", label_of(i).c_str());
                ImGui::SameLine(label_column + 120.f);
                if (ImGui::SmallButton("x")) {
                    dropped = i;
                }

                ImGui::PopID();
            }
        }

        // Кнопка стоит вне ветки: со списком она нужна ровно так же, как без
        // него, а спрятанная за «пусто» она даёт добавить только первого.
        if (ImGui::Button("Add candidate...")) {
            in.state.ui.need_add_candidate_for = in.node_name;
        }

        // Операции идут после того, как виджеты закрыты: они меняют и список, и
        // объём узла, а ссылка на слот взята до них.
        if (picked.has_value()) {
            in.ops.execute(
                std::make_unique<select_variant_operation>(
                    in.engine, in.state, in.library,
                    select_variant_params{.name = in.node_name, .index = *picked}
                )
            );
        }

        if (dropped.has_value()) {
            auto next = candidates;
            next.erase(next.begin() + static_cast<std::ptrdiff_t>(*dropped));

            in.ops.execute(
                std::make_unique<set_variant_candidates_operation>(
                    in.engine, in.state, in.library,
                    set_variant_candidates_params{.name = in.node_name, .candidates = std::move(next)}
                )
            );
        }
    };

    drawer.summary = [](const component_drawer_context& in) {
        const auto& slot = in.engine.get_world().get<ecs::variant_slot_component>(in.ent);
        return std::format("{} of {}", slot.get_selected() + 1, slot.get_candidates().size());
    };

    drawer.add = [](const component_drawer_context& in) {
        in.ops.execute(
            std::make_unique<add_variant_slot_operation>(
                in.engine, in.state, add_variant_slot_params{.name = in.node_name}
            )
        );
    };

    drawer.remove = [](const component_drawer_context& in) {
        in.ops.execute(
            std::make_unique<remove_variant_slot_operation>(
                in.engine, in.state, in.library,
                remove_variant_slot_params{.name = in.node_name}
            )
        );
    };

    drawers.register_for<ecs::variant_slot_component>(std::move(drawer));
}

auto register_rig(component_drawer_registry& drawers) -> void {
    component_drawer drawer;
    drawer.tag   = "rig";
    drawer.title = "Rig";

    // Ни add, ни remove: риг заводит и переименовывает своя панель, а в составе
    // узла ему делать нечего — он свойство префаба целиком.
    drawer.draw = [](const component_drawer_context& in) {
        const auto& rig_name = in.engine.get_world().get<ecs::rig_component>(in.ent).get_name();

        field_label("Name");
        ImGui::TextDisabled("%s", rig_name.empty() ? "unnamed" : rig_name.c_str());

        if (!in.state.ui.show_rig && ImGui::Button("Show rig")) {
            in.state.ui.show_rig = true;
        }
    };

    drawers.register_for<ecs::rig_component>(std::move(drawer));
}

}  // namespace

component_drawer_registry::component_drawer_registry() {
    // Порядок регистрации — он же порядок секций в панели.
    register_transform(*this);
    register_model(*this);
    register_sockets(*this);
    register_variant(*this);
    register_rig(*this);
    register_anim_target(*this);
}

auto default_drawers() -> component_drawer_registry& {
    static component_drawer_registry drawers;
    return drawers;
}

}  // namespace vw::sculptor
