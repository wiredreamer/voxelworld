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

constexpr float32 label_column = 70.f;

auto field_label(std::string_view label) -> void {
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(label.data(), label.data() + label.size());
    ImGui::SameLine(label_column);
}

auto add_by_operation(component_drawer& drawer) -> void {
    drawer.add = [make = drawer.make_add](const component_drawer_context& in) {
        in.ops.execute(make(in.engine, in.state, in.node_name));
    };
}

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
        if (imgui_drag_vec3f("Pos", position, label_column).changed) {
            auto next = transform_comp.get_transform();
            next.set_position(position);
            commit_transform(in, next);
        }

        auto& degrees = rotation_of(transform_comp, in.node_name);
        if (imgui_drag_vec3f("Rot", degrees, label_column).changed) {
            auto next = transform_comp.get_transform();
            next.set_rotation_euler(
                vec3f{
                    math::radians(degrees.x), math::radians(degrees.y), math::radians(degrees.z)
                }
            );
            commit_transform(in, next);
        }

        vec3f scale = transform_comp.get_scale();
        if (imgui_drag_vec3f("Scale", scale, label_column).changed) {
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

            if (in.state.ctx.allows_volume_edit() && !in.state.file.filename.empty()) {
                ImGui::SameLine();
                if (ImGui::SmallButton("Rename")) {
                    in.state.ui.need_rename_model_for = in.node_name;
                }
            }
        }

        const auto model_size = model_comp.size();
        field_label("Size");
        ImGui::TextDisabled("%dx%dx%d", model_size.x, model_size.y, model_size.z);

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

        vec3f pivot = model_comp.get_pivot();
        if (!in.state.ctx.allows_volume_edit()) {
            field_label("Pivot");
            ImGui::TextDisabled("%.1f %.1f %.1f", pivot.x, pivot.y, pivot.z);
        } else if (imgui_drag_vec3f("Pivot", pivot, label_column).changed) {
            in.ops.execute(
                std::make_unique<set_pivot_operation>(
                    in.engine, in.state,
                    set_pivot_params{.name = in.node_name, .new_pivot = pivot}
                )
            );
        }

        if (in.state.ctx.allows_volume_edit() && can_trim && ImGui::Button("Trim")) {
            in.ops.execute(
                std::make_unique<trim_model_operation>(
                    in.engine, in.state, trim_model_params{.name = in.node_name}
                )
            );
        }

        if (in.state.ctx.in_prefab() && ImGui::Button("Edit")) {
            in.state.ctx.enter(edit_context::model(in.node_name));
        }
    };

    drawer.summary = [](const component_drawer_context& in) {
        const auto size = in.engine.get_world().get<ecs::model_component>(in.ent).size();
        return std::format("{}x{}x{}", size.x, size.y, size.z);
    };

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

        if (!in.state.ui.show_sockets && ImGui::Button("Show")) {
            in.state.ui.show_sockets = true;
        }
    };

    drawer.summary = [](const component_drawer_context& in) {
        const auto count =
            in.engine.get_world().get<ecs::socket_component>(in.ent).get_sockets().size();
        return std::format("{} point(s)", count);
    };

    drawer.make_add = [](gfx::engine& eng, app_state& st, const std::string& node_name) {
        return std::make_unique<add_socket_component_operation>(
            eng, st, add_socket_component_params{.name = node_name}
        );
    };
    add_by_operation(drawer);

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

    drawer.make_add = [](gfx::engine& eng, app_state& st, const std::string& node_name) {
        return std::make_unique<add_animation_target_operation>(
            eng, st, add_animation_target_params{.entity_name = node_name, .target_name = node_name}
        );
    };
    add_by_operation(drawer);

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

        const auto label_of = [&candidates](std::size_t index) {
            return std::format("{}{}", candidates[index].stem(), candidates[index].extension());
        };

        std::optional<std::size_t> picked;
        std::optional<std::size_t> dropped;

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

        if (ImGui::Button("Add")) {
            in.state.ui.need_add_candidate_for = in.node_name;
        }

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

    drawer.make_add = [](gfx::engine& eng, app_state& st, const std::string& node_name) {
        return std::make_unique<add_variant_slot_operation>(
            eng, st, add_variant_slot_params{.name = node_name}
        );
    };
    add_by_operation(drawer);

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

auto register_machines(component_drawer_registry& drawers) -> void {
    component_drawer drawer;
    drawer.tag   = "fsm";
    drawer.title = "State machines";

    drawer.draw = [](const component_drawer_context& in) {
        const auto sources =
            in.engine.get_world().get<ecs::animation_machines_component>(in.ent).get_sources();

        std::vector<asset::asset_ref> machines{sources.begin(), sources.end()};

        std::optional<std::size_t> to_remove;
        std::optional<std::size_t> to_lift;

        for (std::size_t i = 0; i < machines.size(); ++i) {
            ImGui::PushID(static_cast<int32>(i));

            ImGui::AlignTextToFramePadding();
            ImGui::Text("%zu", i);
            ImGui::SameLine(label_column);
            ImGui::TextUnformatted(machines[i].stem().data(),
                                   machines[i].stem().data() + machines[i].stem().size());

            ImGui::SameLine();
            if (ImGui::SmallButton("Edit")) {
                in.state.ui.need_enter_machine = i;
            }

            if (i > 0) {
                ImGui::SameLine();
                if (ImGui::SmallButton("^")) {
                    to_lift = i;
                }
            }

            ImGui::SameLine();
            if (ImGui::SmallButton("x")) {
                to_remove = i;
            }

            ImGui::PopID();
        }

        if (ImGui::Button("Add")) {
            in.state.ui.need_add_machine_modal = true;
        }

        if (to_lift.has_value()) {
            std::swap(machines[*to_lift], machines[*to_lift - 1]);
        } else if (to_remove.has_value()) {
            machines.erase(machines.begin() + static_cast<std::ptrdiff_t>(*to_remove));
        } else {
            return;
        }

        in.ops.execute(std::make_unique<set_machines_operation>(
            in.engine, in.state, set_machines_params{.machines = std::move(machines)}
        ));
    };

    drawer.summary = [](const component_drawer_context& in) {
        const auto count =
            in.engine.get_world().get<ecs::animation_machines_component>(in.ent).get_sources().size();
        return std::format("{} layer(s)", count);
    };

    drawer.add = [](const component_drawer_context& in) {
        in.state.ui.need_add_machine_modal = true;
    };

    drawer.remove = [](const component_drawer_context& in) {
        in.ops.execute(
            std::make_unique<set_machines_operation>(in.engine, in.state, set_machines_params{})
        );
    };

    drawers.register_for<ecs::animation_machines_component>(std::move(drawer));
}

auto register_rig(component_drawer_registry& drawers) -> void {
    component_drawer drawer;
    drawer.tag   = "rig";
    drawer.title = "Rig";

    drawer.draw = [](const component_drawer_context& in) {
        auto& world          = in.engine.get_world();
        const auto& rig_name = world.get<ecs::rig_component>(in.ent).get_name();

        static std::string input;

        imgui_input_text_string("Name", input);

        if (ImGui::IsItemDeactivatedAfterEdit()) {
            if (input != rig_name) {
                in.ops.execute(
                    std::make_unique<set_rig_operation>(
                        in.engine, in.state, set_rig_params{.rig_name = input}
                    )
                );
            }
        } else if (!ImGui::IsItemActive()) {
            input = rig_name;
        }

        if (rig_name.empty()) {
            ImGui::TextDisabled("no rig: clips are not checked");
        }

        const auto targets = world.system<ecs::animation_system>().collect_targets(in.ent);

        const auto header = std::format("Targets: {}", targets.size());
        if (ImGui::TreeNode(header.c_str())) {
            if (targets.empty()) {
                ImGui::TextDisabled("none: no node carries an animation target");
            }
            for (const auto& name : targets) {
                ImGui::BulletText("%s", name.c_str());
            }
            ImGui::TreePop();
        }
    };

    drawers.register_for<ecs::rig_component>(std::move(drawer));
}

}  // namespace

namespace {

constexpr std::array<const char*, 5> structure_size_names{"-", "S", "M", "L", "XL"};

auto races_to_text(std::span<const std::string> races) -> std::string {
    std::string text;
    for (const auto& race : races) {
        if (!text.empty()) {
            text += ' ';
        }
        text += race;
    }
    return text;
}

auto races_from_text(std::string_view text) -> std::vector<std::string> {
    std::vector<std::string> races;

    std::size_t pos = 0;
    while (pos < text.size()) {
        const auto start = text.find_first_not_of(' ', pos);
        if (start == std::string_view::npos) {
            break;
        }

        const auto end = text.find(' ', start);
        races.emplace_back(text.substr(
            start, end == std::string_view::npos ? std::string_view::npos : end - start
        ));
        pos = end == std::string_view::npos ? text.size() : end;
    }

    return races;
}

auto params_of(const ecs::structure_component& structure, std::string name)
    -> set_structure_params {
    const auto races = structure.get_races();

    return set_structure_params{
        .name  = std::move(name),
        .type  = structure.get_type(),
        .races = {races.begin(), races.end()},
        .tier  = structure.get_tier(),
        .size  = structure.get_size(),
    };
}

auto register_structure(component_drawer_registry& drawers) -> void {
    component_drawer drawer;
    drawer.tag   = "structure";
    drawer.title = "Structure";

    drawer.draw = [](const component_drawer_context& in) {
        const auto& structure =
            in.engine.get_world().get<ecs::structure_component>(in.ent);

        auto params = params_of(structure, in.node_name);
        bool commit = false;

        auto type = params.type;
        field_label("Type");
        imgui_input_text_string("##structure_type", type);
        if (ImGui::IsItemDeactivatedAfterEdit() && type != params.type) {
            params.type = type;
            commit      = true;
        }

        auto races = races_to_text(structure.get_races());
        const auto races_before = races;
        field_label("Races");
        imgui_input_text_string("##structure_races", races);
        if (ImGui::IsItemDeactivatedAfterEdit() && races != races_before) {
            params.races = races_from_text(races);
            commit       = true;
        }

        auto tier = static_cast<int32>(params.tier);
        field_label("Tier");
        ImGui::SetNextItemWidth(80.f);
        if (ImGui::SliderInt("##structure_tier", &tier, 0, 5, tier == 0 ? "-" : "%d")) {
            params.tier = static_cast<uint8>(tier);
            commit      = true;
        }

        auto size = static_cast<int32>(params.size);
        field_label("Size");
        ImGui::SetNextItemWidth(80.f);
        if (ImGui::Combo("##structure_size", &size, structure_size_names.data(),
                         static_cast<int32>(structure_size_names.size()))) {
            params.size = static_cast<ecs::structure_size>(size);
            commit      = true;
        }

        if (commit) {
            in.ops.execute(
                std::make_unique<set_structure_operation>(in.engine, in.state, std::move(params))
            );
        }
    };

    drawer.summary = [](const component_drawer_context& in) {
        const auto& structure = in.engine.get_world().get<ecs::structure_component>(in.ent);
        return structure.get_type().empty() ? std::string{"untyped"} : structure.get_type();
    };

    drawer.make_add = [](gfx::engine& eng, app_state& st, const std::string& node_name) {
        return std::make_unique<set_structure_operation>(
            eng, st, set_structure_params{.name = node_name}
        );
    };
    add_by_operation(drawer);

    drawer.remove = [](const component_drawer_context& in) {
        in.engine.get_world()
            .modify(in.state.scene.name_to_entity.at(in.node_name))
            .without<ecs::structure_component>();
        in.state.file.has_unsaved_changes = true;
    };

    drawers.register_for<ecs::structure_component>(std::move(drawer));
}

auto register_point(
    component_drawer_registry& drawers, point_kind kind, std::string tag, std::string title,
    std::string_view field
) -> void {
    component_drawer drawer;
    drawer.tag   = std::move(tag);
    drawer.title = std::move(title);

    const auto read = [kind](const component_drawer_context& in) -> std::string {
        const auto& world = in.engine.get_world();
        return kind == point_kind::furniture
                   ? world.get<ecs::furniture_point_component>(in.ent).get_category()
                   : world.get<ecs::connection_point_component>(in.ent).get_profile();
    };

    drawer.draw = [kind, read, label = std::string{field}](const component_drawer_context& in) {
        auto value            = read(in);
        const auto before     = value;

        field_label(label);
        imgui_input_text_string("##point_tag", value);

        if (ImGui::IsItemDeactivatedAfterEdit() && value != before) {
            in.ops.execute(std::make_unique<set_point_operation>(
                in.engine, in.state,
                set_point_params{.name = in.node_name, .kind = kind, .tag = std::move(value)}
            ));
        }
    };

    drawer.summary = [read](const component_drawer_context& in) {
        auto value = read(in);
        return value.empty() ? std::string{"unnamed"} : value;
    };

    drawer.make_add = [kind](gfx::engine& eng, app_state& st, const std::string& node_name) {
        return std::make_unique<set_point_operation>(
            eng, st, set_point_params{.name = node_name, .kind = kind}
        );
    };
    add_by_operation(drawer);

    drawer.remove = [kind](const component_drawer_context& in) {
        in.ops.execute(std::make_unique<set_point_operation>(
            in.engine, in.state,
            set_point_params{.name = in.node_name, .kind = kind, .present = false}
        ));
    };

    if (kind == point_kind::furniture) {
        drawers.register_for<ecs::furniture_point_component>(std::move(drawer));
    } else {
        drawers.register_for<ecs::connection_point_component>(std::move(drawer));
    }
}

}  // namespace

component_drawer_registry::component_drawer_registry() {
    register_transform(*this);
    register_model(*this);
    register_sockets(*this);
    register_variant(*this);
    register_rig(*this);
    register_anim_target(*this);
    register_machines(*this);
    register_structure(*this);
    register_point(*this, point_kind::furniture, "furniture", "Furniture point", "Category");
    register_point(*this, point_kind::connection, "connection", "Connection", "Profile");
}

auto default_drawers() -> component_drawer_registry& {
    static component_drawer_registry drawers;
    return drawers;
}

}  // namespace vw::sculptor
