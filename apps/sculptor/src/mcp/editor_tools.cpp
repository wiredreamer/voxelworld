module;

#include <imgui.h>

module vw.sculptor;

import std;

import vw.core;
import vw.asset;
import vw.gfx;

namespace vw::sculptor {

namespace {

constexpr std::string_view no_arguments =
    R"({"type": "object", "properties": {}, "additionalProperties": false})";

[[nodiscard]] auto name_of(edit_kind kind) -> std::string_view {
    switch (kind) {
        case edit_kind::prefab:
            return "prefab";
        case edit_kind::model:
            return "volume";
        case edit_kind::clip:
            return "clip";
        case edit_kind::fsm:
            return "machine";
        case edit_kind::paste:
            return "paste";
    }
    return "unknown";
}

[[nodiscard]] auto name_of(tools tool) -> std::string_view {
    switch (tool) {
        case tools::invalid:
            return "none";
        case tools::select_entity:
            return "select_node";
        case tools::add_voxel:
            return "add_voxel";
        case tools::remove_voxel:
            return "remove_voxel";
        case tools::paint_voxel:
            return "paint_voxel";
        case tools::color_picker:
            return "color_picker";
        case tools::move_pivot:
            return "move_pivot";
        case tools::select_box:
            return "select_box";
        case tools::place_paste:
            return "place_paste";
        case tools::pose:
            return "pose";
    }
    return "unknown";
}

[[nodiscard]] auto describe_editor(const mcp_bindings& bindings) -> json::value {
    const app_state& state = *bindings.state;

    json::array contexts;
    for (const edit_context& context : state.ctx.stack) {
        json::object entry{{"kind", name_of(context.kind)}};
        if (!context.node_name.empty()) {
            entry.set("node", context.node_name);
        }
        if (context.kind == edit_kind::fsm) {
            entry.set("layer", context.layer);
        }
        contexts.emplace_back(std::move(entry));
    }

    std::vector<std::string_view> unsaved_clip_names;
    for (const auto& [clip_name, unsaved] : state.anim.unsaved_clips) {
        if (unsaved) {
            unsaved_clip_names.emplace_back(clip_name);
        }
    }
    std::ranges::sort(unsaved_clip_names);

    json::array unsaved_clips;
    for (const std::string_view clip_name : unsaved_clip_names) {
        unsaved_clips.emplace_back(clip_name);
    }

    return json::object{
        {"prefab", json_or_null(state.file.filename)},
        {"asset_root", app_state::asset_root_name},
        {"root_node", json_or_null(state.scene.root_name)},
        {"node_count", state.scene.name_to_entity.size()},
        {"selected_node", json_or_null(state.scene.selected_name)},
        {"edited_node", json_or_null(state.edited_node())},
        {"context", name_of(state.ctx.kind())},
        {"context_stack", std::move(contexts)},
        {"tool", name_of(state.tool.selected_tool)},
        {"clip", json_or_null(state.anim.selected_clip_name)},
        {"machine", state.fsm.is_open() ? json::value{state.fsm.source.str()} : json::value{}},
        {"unsaved",
         json::object{
             {"prefab", state.file.has_unsaved_changes},
             {"machine", state.fsm.has_unsaved_changes},
             {"clips", std::move(unsaved_clips)},
         }},
        {"can_undo", !bindings.operations->is_undo_empty()},
        {"can_redo", !bindings.operations->is_redo_empty()},
        {"window_visible", bindings.engine->get_renderer().has_drawable_surface()},
        {"busy", json_or_null(editor_busy_reason(state))},
    };
}

[[nodiscard]] auto history_state(const mcp_bindings& bindings) -> json::value {
    return json::object{
        {"can_undo", !bindings.operations->is_undo_empty()},
        {"can_redo", !bindings.operations->is_redo_empty()},
        {"context", name_of(bindings.state->ctx.kind())},
    };
}

[[nodiscard]] auto list_refs(const std::filesystem::path& dir, std::string_view extension)
    -> json::value {
    std::vector<std::string> refs;
    for (const asset::asset_ref& ref : collect_asset_refs(dir, extension)) {
        refs.push_back(ref.str());
    }
    std::ranges::sort(refs);

    json::array listed;
    listed.reserve(refs.size());
    for (std::string& ref : refs) {
        listed.emplace_back(std::move(ref));
    }
    return listed;
}

[[nodiscard]] auto list_assets() -> json::value {
    json::array prefabs;
    for (std::string& filename : list_prefabs()) {
        prefabs.emplace_back(std::move(filename));
    }

    return json::object{
        {"prefabs", std::move(prefabs)},
        {"volumes", list_refs(app_state::model_dir(), ".voxm")},
        {"clips", list_refs(app_state::clip_dir(), ".voxa")},
        {"machines", list_refs(app_state::fsm_dir(), ".voxf")},
    };
}

[[nodiscard]] auto list_palette(const voxel_registry& registry) -> json::value {
    json::array voxels;
    for (const voxel_type& type : registry.all()) {
        if (type.id.value == 0 || !registry.known(type.id)) {
            continue;
        }

        const color tint = type.material.clr;
        json::object entry{
            {"index", type.id.value},
            {"name", type.name},
            {"color", std::format("#{:02x}{:02x}{:02x}", tint.r(), tint.g(), tint.b())},
        };
        if (type.material.glow > 0) {
            entry.set("glow", type.material.glow);
        }
        if (type.material.emission > 0) {
            entry.set("emission", type.material.emission);
        }
        voxels.emplace_back(std::move(entry));
    }

    json::array groups;
    for (const voxel_group& group : registry.groups()) {
        groups.emplace_back(json::object{
            {"name", group.name},
            {"first_index", group.first.value},
            {"count", group.count},
        });
    }

    return json::object{
        {"air_index", 0},
        {"voxels", std::move(voxels)},
        {"groups", std::move(groups)},
    };
}

}  // namespace

auto json_of(float32 number) -> json::value {
    if (number == 0.0F) {
        return 0.0;
    }

    std::array<char, 32> digits{};
    const auto written = std::to_chars(digits.data(), digits.data() + digits.size(), number);

    float64 shortest = 0.0;
    std::from_chars(digits.data(), written.ptr, shortest);
    return shortest;
}

auto json_of(const vec3f& vector) -> json::value {
    return json::array{json_of(vector.x), json_of(vector.y), json_of(vector.z)};
}

auto json_of(const vec3i& vector) -> json::value {
    return json::array{vector.x, vector.y, vector.z};
}

auto json_or_null(std::string_view text) -> json::value {
    return text.empty() ? json::value{} : json::value{text};
}

auto editor_busy_reason(const app_state& state) -> std::string_view {
    if (state.paste.active()) {
        return "a paste is being placed; confirm or cancel it in the editor";
    }
    if (ImGui::IsAnyMouseDown()) {
        return "a mouse button is held in the editor; try again in a moment";
    }
    if (ImGui::IsAnyItemActive()) {
        return "a field is being edited in the editor; try again in a moment";
    }

    constexpr ImGuiPopupFlags any_popup =
        ImGuiPopupFlags_AnyPopupId | ImGuiPopupFlags_AnyPopupLevel;
    if (!state.ui.startup_modal_open &&
        ImGui::IsPopupOpen(static_cast<const char*>(nullptr), any_popup)) {
        return "a dialog is open in the editor; close it first";
    }
    return {};
}

auto when_idle(const mcp_bindings& bindings, tool_body body) -> tool_body {
    return [bindings, body = std::move(body)](const json::value& arguments) -> tool_outcome {
        const std::string_view reason = editor_busy_reason(*bindings.state);
        if (!reason.empty()) {
            return tool_failure(std::format("the editor is busy: {}", reason));
        }
        return body(arguments);
    };
}

auto make_editor_tools(const mcp_bindings& bindings) -> std::vector<mcp_tool> {
    std::vector<mcp_tool> tools;

    tools.push_back(mcp_tool{
        .name = "editor_state",
        .description =
            "Report what the editor has open: the prefab, the edit context, the selected and "
            "edited node, the active clip and state machine, unsaved changes, whether undo and "
            "redo are possible, and whether the editor is busy. Call this first and after "
            "anything fails.",
        .input_schema = no_arguments,
        .run =
            [bindings](const json::value&) -> tool_outcome {
                return tool_success(describe_editor(bindings));
            },
    });

    tools.push_back(mcp_tool{
        .name = "assets_list",
        .description =
            "List the asset files the editor can open, as paths relative to the asset root: "
            "prefabs (.vox, by file name), volumes (.voxm), animation clips (.voxa) and state "
            "machines (.voxf).",
        .input_schema = no_arguments,
        .run          = [](const json::value&) -> tool_outcome { return tool_success(list_assets()); },
    });

    tools.push_back(mcp_tool{
        .name = "palette_list",
        .description =
            "List every voxel a volume may contain: its index, name and colour, and for glowing "
            "voxels their glow and light emission. Index 0 is air. The palette is fixed and "
            "shared by all volumes.",
        .input_schema = no_arguments,
        .run =
            [bindings](const json::value&) -> tool_outcome {
                return tool_success(list_palette(bindings.engine->get_voxel_registry()));
            },
    });

    tools.push_back(mcp_tool{
        .name = "undo",
        .description =
            "Undo the last edit, exactly as Ctrl+Z in the editor. The history is shared with the "
            "user and is cleared by opening, closing, creating or renaming a prefab.",
        .input_schema = no_arguments,
        .run          = when_idle(
            bindings,
            [bindings](const json::value&) -> tool_outcome {
                if (bindings.operations->is_undo_empty()) {
                    return tool_failure("there is nothing to undo");
                }
                bindings.operations->undo();
                return tool_success(history_state(bindings));
            }
        ),
    });

    tools.push_back(mcp_tool{
        .name         = "redo",
        .description  = "Redo the edit that undo took back, exactly as Ctrl+Shift+Z in the editor.",
        .input_schema = no_arguments,
        .run          = when_idle(
            bindings,
            [bindings](const json::value&) -> tool_outcome {
                if (bindings.operations->is_redo_empty()) {
                    return tool_failure("there is nothing to redo");
                }
                bindings.operations->redo();
                return tool_success(history_state(bindings));
            }
        ),
    });

    append_prefab_tools(tools, bindings);
    append_node_tools(tools, bindings);

    return tools;
}

}  // namespace vw::sculptor
