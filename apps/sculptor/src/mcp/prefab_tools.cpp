module vw.sculptor;

import std;

import vw.core;
import vw.asset;
import vw.ecs;
import vw.world;
import vw.gfx;

namespace vw::sculptor {

namespace {

constexpr std::string_view no_arguments =
    R"({"type": "object", "properties": {}, "additionalProperties": false})";

constexpr std::string_view new_prefab_schema = R"({
    "type": "object",
    "properties": {
        "name": {"type": "string", "description": "File name of the prefab, with or without .vox"},
        "overwrite": {"type": "boolean", "description": "Replace a prefab file that already exists. Default false."},
        "discard_unsaved": {"type": "boolean", "description": "Drop unsaved changes of what is open now. Default false."}
    },
    "required": ["name"],
    "additionalProperties": false
})";

constexpr std::string_view open_prefab_schema = R"({
    "type": "object",
    "properties": {
        "name": {"type": "string", "description": "File name of the prefab, with or without .vox"},
        "discard_unsaved": {"type": "boolean", "description": "Drop unsaved changes of what is open now. Default false."}
    },
    "required": ["name"],
    "additionalProperties": false
})";

constexpr std::string_view close_prefab_schema = R"({
    "type": "object",
    "properties": {
        "discard_unsaved": {"type": "boolean", "description": "Drop unsaved changes. Default false."}
    },
    "additionalProperties": false
})";

constexpr std::string_view save_as_schema = R"({
    "type": "object",
    "properties": {
        "name": {"type": "string", "description": "File name of the copy, with or without .vox"},
        "overwrite": {"type": "boolean", "description": "Replace a prefab file that already exists. Default false."}
    },
    "required": ["name"],
    "additionalProperties": false
})";

[[nodiscard]] auto flag(const json::value& arguments, std::string_view name)
    -> std::expected<bool, std::string> {
    const json::cursor field = json::cursor{arguments, "arguments"}[name];
    if (!field.exists()) {
        return false;
    }
    return field.boolean().transform_error([](const json::access_error& error) {
        return json::describe(error);
    });
}

[[nodiscard]] auto text(const json::value& arguments, std::string_view name)
    -> std::expected<std::string, std::string> {
    return json::cursor{arguments, "arguments"}[name]
        .string()
        .transform([](std::string_view value) { return std::string{value}; })
        .transform_error([](const json::access_error& error) { return json::describe(error); });
}

[[nodiscard]] auto joined(const std::vector<std::string>& names) -> std::string {
    std::string list;
    for (const std::string& name : names) {
        if (!list.empty()) {
            list += ", ";
        }
        list += name;
    }
    return list;
}

[[nodiscard]] auto unsaved_summary(const app_state& state) -> std::string {
    std::vector<std::string> parts;

    if (state.file.has_unsaved_changes) {
        parts.emplace_back("the prefab");
    }
    if (state.fsm.has_unsaved_changes) {
        parts.emplace_back(std::format("the state machine {}", state.fsm.source.str()));
    }

    std::vector<std::string> clips;
    for (const auto& [clip_name, unsaved] : state.anim.unsaved_clips) {
        if (unsaved) {
            clips.push_back(clip_name);
        }
    }
    std::ranges::sort(clips);
    for (const std::string& clip_name : clips) {
        parts.emplace_back(std::format("the clip {}", clip_name));
    }

    return joined(parts);
}

[[nodiscard]] auto refuse_unsaved(const app_state& state, bool discard)
    -> std::optional<tool_outcome> {
    if (discard || !state.has_unsaved_changes()) {
        return std::nullopt;
    }
    return tool_failure(std::format(
        "there are unsaved changes in {}; save them first or pass discard_unsaved: true",
        unsaved_summary(state)
    ));
}

[[nodiscard]] auto describe(prefab_error error, std::string_view name) -> std::string {
    switch (error) {
        case prefab_error::invalid_name:
            return std::format(
                "'{}' cannot name a prefab: use a plain file name without path separators", name
            );
        case prefab_error::already_exists:
            return std::format("the prefab '{}' already exists; pass overwrite: true to replace it", name);
        case prefab_error::not_found:
            return std::format(
                "there is no prefab '{}'; the prefabs are: {}", name, joined(list_prefabs())
            );
        case prefab_error::read_failed:
            return std::format("the prefab '{}' could not be read; the file is malformed", name);
        case prefab_error::write_failed:
            return std::format("the prefab '{}' could not be written", name);
    }
    return "unknown prefab error";
}

[[nodiscard]] auto prefab_summary(const mcp_bindings& bindings) -> json::value {
    const app_state& state = *bindings.state;

    return json::object{
        {"prefab", json_or_null(state.file.filename)},
        {"root_node", json_or_null(state.scene.root_name)},
        {"node_count", state.scene.name_to_entity.size()},
        {"unsaved", state.has_unsaved_changes()},
    };
}

[[nodiscard]] auto describe_prefab(const mcp_bindings& bindings) -> tool_outcome {
    const app_state& state = *bindings.state;

    if (state.file.filename.empty()) {
        return tool_failure("no prefab is open; use prefab_open or prefab_new");
    }

    json::object prefab{
        {"prefab", state.file.filename},
        {"unsaved", state.file.has_unsaved_changes},
    };

    const auto root = state.scene.name_to_entity.find(state.scene.root_name);
    if (root == state.scene.name_to_entity.end()) {
        prefab.set("root_node", json::value{});
        prefab.set("nodes", json::array{});
        return tool_success(prefab);
    }

    auto& world = bindings.engine->get_world();

    asset::vox_writer_plain writer;
    const ecs::vox_serializer serializer{
        world, writer, root->second, {.entity_names = state.scene.entity_to_name}
    };
    const asset::vox_prefab_data data = serializer.extract();

    json::array machines;
    for (const asset::asset_ref& machine : data.fsm_refs) {
        machines.emplace_back(machine.str());
    }

    json::array nodes;
    for (const asset::vox_entity_data& node : data.entities) {
        nodes.push_back(describe_node(bindings, node.name));
    }

    prefab.set("root_node", data.root_name);
    prefab.set("rig", json_or_null(data.rig));
    prefab.set("machines", std::move(machines));
    prefab.set("nodes", std::move(nodes));
    return tool_success(prefab);
}

}  // namespace

auto append_prefab_tools(std::vector<mcp_tool>& tools, const mcp_bindings& bindings) -> void {
    tools.push_back(mcp_tool{
        .name = "prefab_get",
        .description =
            "Read the open prefab: its rig, its state machines and every node with its parent, "
            "position, rotation in degrees, scale, volume (file, size in voxels, pivot) and the "
            "other components, in the same shape node_set_components takes them.",
        .input_schema = no_arguments,
        .run =
            [bindings](const json::value&) -> tool_outcome { return describe_prefab(bindings); },
    });

    tools.push_back(mcp_tool{
        .name = "prefab_new",
        .description =
            "Create an empty prefab file and open it in place of what is open now. The new "
            "prefab has no nodes; the first node created becomes its root. Clears the undo "
            "history.",
        .input_schema = new_prefab_schema,
        .run          = when_idle(
            bindings,
            [bindings](const json::value& arguments) -> tool_outcome {
                const auto name      = text(arguments, "name");
                const auto overwrite = flag(arguments, "overwrite");
                const auto discard   = flag(arguments, "discard_unsaved");
                if (!name || !overwrite || !discard) {
                    return tool_failure(!name ? name.error() : !overwrite ? overwrite.error() : discard.error());
                }
                if (auto refused = refuse_unsaved(*bindings.state, *discard)) {
                    return *refused;
                }

                const auto created = bindings.files->create(*name, *overwrite);
                if (!created) {
                    return tool_failure(describe(created.error(), *name));
                }
                return tool_success(prefab_summary(bindings));
            }
        ),
    });

    tools.push_back(mcp_tool{
        .name = "prefab_open",
        .description =
            "Open a prefab from the prefabs folder in place of what is open now. Volumes are "
            "read from disk again, open clips are closed, the undo history is cleared.",
        .input_schema = open_prefab_schema,
        .run          = when_idle(
            bindings,
            [bindings](const json::value& arguments) -> tool_outcome {
                const auto name    = text(arguments, "name");
                const auto discard = flag(arguments, "discard_unsaved");
                if (!name || !discard) {
                    return tool_failure(!name ? name.error() : discard.error());
                }
                if (auto refused = refuse_unsaved(*bindings.state, *discard)) {
                    return *refused;
                }

                const auto opened = bindings.files->open(*name);
                if (!opened) {
                    return tool_failure(describe(opened.error(), *name));
                }
                return tool_success(prefab_summary(bindings));
            }
        ),
    });

    tools.push_back(mcp_tool{
        .name = "prefab_save",
        .description =
            "Write the open prefab and every volume changed since the last save. Clips and "
            "state machines are saved by their own tools.",
        .input_schema = no_arguments,
        .run          = when_idle(
            bindings,
            [bindings](const json::value&) -> tool_outcome {
                if (bindings.state->file.filename.empty()) {
                    return tool_failure("no prefab is open; use prefab_open or prefab_new");
                }
                if (bindings.state->scene.root_name.empty()) {
                    return tool_failure("the prefab has no nodes yet; create its root node before saving");
                }
                if (!bindings.files->save()) {
                    return tool_failure(std::format(
                        "the prefab '{}' could not be written", bindings.state->file.filename
                    ));
                }
                return tool_success(prefab_summary(bindings));
            }
        ),
    });

    tools.push_back(mcp_tool{
        .name = "prefab_save_as",
        .description =
            "Save the open prefab under another name and keep editing the copy. The volumes of "
            "the prefab are copied into the folder of the new prefab. Clears the undo history.",
        .input_schema = save_as_schema,
        .run          = when_idle(
            bindings,
            [bindings](const json::value& arguments) -> tool_outcome {
                const auto name      = text(arguments, "name");
                const auto overwrite = flag(arguments, "overwrite");
                if (!name || !overwrite) {
                    return tool_failure(!name ? name.error() : overwrite.error());
                }
                if (bindings.state->scene.root_name.empty()) {
                    return tool_failure("there is nothing to save: no prefab with nodes is open");
                }

                const auto filename = prefab_filename(*name);
                if (!filename) {
                    return tool_failure(describe(prefab_error::invalid_name, *name));
                }

                std::error_code ec;
                if (!*overwrite && std::filesystem::exists(app_state::prefab_dir() / *filename, ec)) {
                    return tool_failure(describe(prefab_error::already_exists, *filename));
                }
                if (!bindings.files->save_as(*filename)) {
                    return tool_failure(describe(prefab_error::write_failed, *filename));
                }
                return tool_success(prefab_summary(bindings));
            }
        ),
    });

    tools.push_back(mcp_tool{
        .name = "prefab_close",
        .description =
            "Close the open prefab without saving and leave the editor empty. Open clips are "
            "closed too, the undo history is cleared.",
        .input_schema = close_prefab_schema,
        .run          = when_idle(
            bindings,
            [bindings](const json::value& arguments) -> tool_outcome {
                const auto discard = flag(arguments, "discard_unsaved");
                if (!discard) {
                    return tool_failure(discard.error());
                }
                if (bindings.state->file.filename.empty()) {
                    return tool_failure("no prefab is open");
                }
                if (auto refused = refuse_unsaved(*bindings.state, *discard)) {
                    return *refused;
                }

                bindings.files->close();
                return tool_success(prefab_summary(bindings));
            }
        ),
    });
}

}  // namespace vw::sculptor
