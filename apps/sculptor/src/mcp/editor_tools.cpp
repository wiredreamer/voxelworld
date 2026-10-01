module;

#include <imgui.h>

module vw.sculptor;

import std;

import vw.core;
import vw.asset;
import vw.gfx;

namespace vw::sculptor {

namespace {

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

[[nodiscard]] auto text_or_null(std::string_view text) -> json::value {
    return text.empty() ? json::value{} : json::value{text};
}

[[nodiscard]] auto busy_reason(const app_state& state) -> std::string_view {
    if (state.paste.active()) {
        return "a paste is being placed; confirm or cancel it in the editor";
    }

    constexpr ImGuiPopupFlags any_popup =
        ImGuiPopupFlags_AnyPopupId | ImGuiPopupFlags_AnyPopupLevel;
    if (ImGui::IsPopupOpen(static_cast<const char*>(nullptr), any_popup)) {
        return "a dialog is open in the editor; close it first";
    }
    return {};
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

    json::array unsaved_clips;
    for (const auto& [clip_name, unsaved] : state.anim.unsaved_clips) {
        if (unsaved) {
            unsaved_clips.emplace_back(clip_name);
        }
    }
    std::ranges::sort(unsaved_clips, {}, [](const json::value& entry) {
        return *entry.as_string();
    });

    return json::object{
        {"prefab", text_or_null(state.file.filename)},
        {"asset_root", app_state::asset_root_name},
        {"root_node", text_or_null(state.scene.root_name)},
        {"node_count", state.scene.name_to_entity.size()},
        {"selected_node", text_or_null(state.scene.selected_name)},
        {"edited_node", text_or_null(state.edited_node())},
        {"context", name_of(state.ctx.kind())},
        {"context_stack", std::move(contexts)},
        {"tool", name_of(state.tool.selected_tool)},
        {"clip", text_or_null(state.anim.selected_clip_name)},
        {"machine", state.fsm.is_open() ? json::value{state.fsm.source.str()} : json::value{}},
        {"unsaved",
         json::object{
             {"prefab", state.file.has_unsaved_changes},
             {"machine", state.fsm.has_unsaved_changes},
             {"clips", std::move(unsaved_clips)},
         }},
        {"window_visible", bindings.engine->get_renderer().has_drawable_surface()},
        {"busy", text_or_null(busy_reason(state))},
    };
}

}  // namespace

auto make_editor_tools(const mcp_bindings& bindings) -> std::vector<mcp_tool> {
    std::vector<mcp_tool> tools;

    tools.push_back(mcp_tool{
        .name = "editor_state",
        .description =
            "Report what the editor has open: the prefab, the edit context, the selected and "
            "edited node, the active clip and state machine, unsaved changes, and whether the "
            "editor is busy with a dialog or a paste. Call this first and after anything fails.",
        .input_schema = R"({"type": "object", "properties": {}, "additionalProperties": false})",
        .run =
            [bindings](const json::value&) -> tool_outcome {
                return tool_success(describe_editor(bindings));
            },
    });

    return tools;
}

}  // namespace vw::sculptor
