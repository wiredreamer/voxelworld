module vw.sculptor;

import std;

import vw.core;
import vw.asset;
import vw.ecs;
import vw.world;
import vw.gfx;

namespace vw::sculptor {

namespace {

constexpr std::string_view placement_properties = R"(
        "position": {"description": "Position relative to the parent, [x, y, z] in voxels.", "type": "array", "items": {"type": "number"}, "minItems": 3, "maxItems": 3},
        "rotation_degrees": {"description": "Euler rotation relative to the parent, [x, y, z] in degrees.", "type": "array", "items": {"type": "number"}, "minItems": 3, "maxItems": 3},
        "scale": {"description": "Scale relative to the parent, [x, y, z].", "type": "array", "items": {"type": "number"}, "minItems": 3, "maxItems": 3})";

constexpr std::string_view component_properties = R"(
        "volume": {
            "description": "The voxel volume of the node. {'size': [x, y, z]} makes a new empty volume of that many voxels, {'ref': 'models/...voxm'} attaches a volume file, null removes the volume. The origin of the node sits at the pivot of its volume.",
            "type": ["object", "null"],
            "properties": {
                "size": {"type": "array", "items": {"type": "integer"}, "minItems": 3, "maxItems": 3},
                "ref": {"type": "string"}
            },
            "additionalProperties": false
        },
        "anim_target": {
            "description": "The name by which animation clips address this node. true uses the node name, which is what the editor expects; null removes the target.",
            "type": ["string", "boolean", "null"]
        },
        "sockets": {
            "description": "Every socket of the node, the places other prefabs attach to. The list replaces the current one; null or [] removes all sockets.",
            "type": ["array", "null"],
            "items": {
                "type": "object",
                "properties": {
                    "name": {"type": "string"},
                    "position": {"type": "array", "items": {"type": "number"}, "minItems": 3, "maxItems": 3},
                    "rotation_degrees": {"type": "array", "items": {"type": "number"}, "minItems": 3, "maxItems": 3},
                    "scale": {"type": "array", "items": {"type": "number"}, "minItems": 3, "maxItems": 3}
                },
                "required": ["name"],
                "additionalProperties": false
            }
        },
        "variant": {
            "description": "A slot that swaps what the node shows between candidates: .voxm volumes or .vox prefabs. null removes the slot.",
            "type": ["object", "null"],
            "properties": {
                "candidates": {"type": "array", "items": {"type": "string"}},
                "selected": {"type": "integer", "minimum": 0}
            },
            "required": ["candidates"],
            "additionalProperties": false
        },
        "structure": {
            "description": "Marks the node as a building structure. All four fields are replaced together; null removes the mark.",
            "type": ["object", "null"],
            "properties": {
                "type": {"type": "string"},
                "races": {"type": "array", "items": {"type": "string"}},
                "tier": {"type": "integer", "minimum": 0, "maximum": 5},
                "size": {"enum": ["S", "M", "L", "XL", null]}
            },
            "additionalProperties": false
        },
        "furniture": {"description": "Category of a furniture point; null removes the point.", "type": ["string", "null"]},
        "connection": {"description": "Profile of a connection point; null removes the point.", "type": ["string", "null"]})";

constexpr std::string_view node_name_only_schema = R"({
    "type": "object",
    "properties": {"name": {"type": "string", "description": "Name of the node."}},
    "required": ["name"],
    "additionalProperties": false
})";

constexpr std::string_view move_schema = R"({
    "type": "object",
    "properties": {
        "name": {"type": "string", "description": "Name of the node to move."},
        "parent": {"type": "string", "description": "Name of the new parent node."},
        "index": {"type": "integer", "minimum": 0, "description": "Place among the other children of the parent, 0 is first. Appended last when omitted."}
    },
    "required": ["name", "parent"],
    "additionalProperties": false
})";

constexpr std::string_view rig_schema = R"({
    "type": "object",
    "properties": {"rig": {"type": "string", "description": "Name of the rig, shared by the prefab, its clips and its state machines."}},
    "required": ["rig"],
    "additionalProperties": false
})";

[[nodiscard]] auto create_schema() -> std::string_view {
    static const std::string schema = std::format(
        R"({{
    "type": "object",
    "properties": {{
        "name": {{"type": "string", "description": "Name of the new node, unique in the prefab: letters, digits, '_', '-' and '.'."}},
        "parent": {{"type": "string", "description": "Name of the parent node. Omit only for the first node of an empty prefab, which becomes the root."}},{},{}
    }},
    "required": ["name"],
    "additionalProperties": false
}})",
        placement_properties, component_properties
    );
    return schema;
}

[[nodiscard]] auto transform_schema() -> std::string_view {
    static const std::string schema = std::format(
        R"({{
    "type": "object",
    "properties": {{
        "name": {{"type": "string", "description": "Name of the node."}},{}
    }},
    "required": ["name"],
    "additionalProperties": false
}})",
        placement_properties
    );
    return schema;
}

[[nodiscard]] auto components_schema() -> std::string_view {
    static const std::string schema = std::format(
        R"({{
    "type": "object",
    "properties": {{
        "name": {{"type": "string", "description": "Name of the node."}},{}
    }},
    "required": ["name"],
    "additionalProperties": false
}})",
        component_properties
    );
    return schema;
}

[[nodiscard]] auto degrees_of(const quat& rotation) -> vec3f {
    const vec3f euler = math::quat_to_euler(rotation);
    return vec3f{math::degrees(euler.x), math::degrees(euler.y), math::degrees(euler.z)};
}

[[nodiscard]] auto rotation_of(const vec3f& degrees) -> quat {
    return math::euler_to_quat(
        vec3f{math::radians(degrees.x), math::radians(degrees.y), math::radians(degrees.z)}
    );
}

[[nodiscard]] auto name_of(ecs::structure_size size) -> json::value {
    switch (size) {
        case ecs::structure_size::small:
            return "S";
        case ecs::structure_size::medium:
            return "M";
        case ecs::structure_size::large:
            return "L";
        case ecs::structure_size::extra_large:
            return "XL";
        case ecs::structure_size::unspecified:
            break;
    }
    return {};
}

[[nodiscard]] auto structure_size_of(std::string_view text) -> std::optional<ecs::structure_size> {
    if (text == "S") {
        return ecs::structure_size::small;
    }
    if (text == "M") {
        return ecs::structure_size::medium;
    }
    if (text == "L") {
        return ecs::structure_size::large;
    }
    if (text == "XL") {
        return ecs::structure_size::extra_large;
    }
    return std::nullopt;
}

[[nodiscard]] auto read_placement(argument_reader& in, transform placement) -> transform {
    if (const auto position = in.optional_vec3f("position")) {
        placement.set_position(*position);
    }
    if (const auto degrees = in.optional_vec3f("rotation_degrees")) {
        placement.set_rotation(rotation_of(*degrees));
    }
    if (const auto scale = in.optional_vec3f("scale")) {
        placement.set_scale(*scale);
    }
    return placement;
}

auto read_volume(argument_reader& in, component_changes& changes) -> void {
    if (!in.has("volume")) {
        return;
    }
    changes.volume.requested = true;
    if (in.is_null("volume")) {
        return;
    }

    argument_reader volume{in.at("volume")};
    volume.allow({"size", "ref"});

    changes.volume.value = volume_spec{
        .blank_size = volume.optional_vec3i("size"),
        .source     = asset::asset_ref{volume.optional_text("ref").value_or(std::string{})},
    };
    if (volume.failed()) {
        in.fail(volume.error());
    }
}

auto read_anim_target(argument_reader& in, const std::string& node_name, component_changes& changes)
    -> void {
    if (!in.has("anim_target")) {
        return;
    }
    changes.anim_target.requested = true;
    if (in.is_null("anim_target")) {
        return;
    }

    if (const auto flag = in.at("anim_target").get()->as_bool()) {
        if (*flag) {
            changes.anim_target.value = node_name;
        }
        return;
    }
    changes.anim_target.value = in.text("anim_target");
}

auto read_sockets(argument_reader& in, component_changes& changes) -> void {
    if (!in.has("sockets")) {
        return;
    }
    changes.sockets.requested = true;
    if (in.is_null("sockets")) {
        return;
    }

    const auto elements = in.at("sockets").elements();
    if (!elements) {
        in.fail(json::describe(elements.error()));
        return;
    }

    std::vector<socket_spec> sockets;
    for (const json::cursor& element : *elements) {
        argument_reader socket{element};
        socket.allow({"name", "position", "rotation_degrees", "scale"});

        sockets.push_back(socket_spec{
            .name     = socket.text("name"),
            .position = socket.optional_vec3f("position").value_or(vec3f{}),
            .rotation = rotation_of(socket.optional_vec3f("rotation_degrees").value_or(vec3f{})),
            .scale    = socket.optional_vec3f("scale").value_or(vec3f{1.0F, 1.0F, 1.0F}),
        });
        if (socket.failed()) {
            in.fail(socket.error());
            return;
        }
    }
    changes.sockets.value = std::move(sockets);
}

auto read_variant(argument_reader& in, component_changes& changes) -> void {
    if (!in.has("variant")) {
        return;
    }
    changes.variant.requested = true;
    if (in.is_null("variant")) {
        return;
    }

    argument_reader variant{in.at("variant")};
    variant.allow({"candidates", "selected"});
    if (!variant.has("candidates")) {
        variant.fail(std::format("{}: missing, expected array", variant.at("candidates").path()));
    }

    variant_spec wanted;
    for (const std::string& candidate : variant.text_list("candidates")) {
        wanted.candidates.emplace_back(candidate);
    }
    wanted.selected = variant.optional_index("selected").value_or(0);

    if (variant.failed()) {
        in.fail(variant.error());
        return;
    }
    changes.variant.value = std::move(wanted);
}

auto read_structure(argument_reader& in, component_changes& changes) -> void {
    if (!in.has("structure")) {
        return;
    }
    changes.structure.requested = true;
    if (in.is_null("structure")) {
        return;
    }

    argument_reader structure{in.at("structure")};
    structure.allow({"type", "races", "tier", "size"});

    structure_spec wanted;
    wanted.type  = structure.optional_text("type").value_or(std::string{});
    wanted.races = structure.text_list("races");

    const std::size_t tier = structure.optional_index("tier").value_or(0);
    if (tier > std::numeric_limits<uint8>::max()) {
        structure.fail(std::format("{}: {} is out of range", structure.at("tier").path(), tier));
    }
    wanted.tier = static_cast<uint8>(tier);

    if (const auto size = structure.optional_text("size")) {
        const auto parsed = structure_size_of(*size);
        if (!parsed) {
            structure.fail(std::format(
                "{}: expected S, M, L, XL or null, found '{}'", structure.at("size").path(), *size
            ));
        }
        wanted.size = parsed.value_or(ecs::structure_size::unspecified);
    }

    if (structure.failed()) {
        in.fail(structure.error());
        return;
    }
    changes.structure.value = std::move(wanted);
}

auto read_point(
    argument_reader& in, std::string_view key, component_change<std::string>& change
) -> void {
    if (!in.has(key)) {
        return;
    }
    change.requested = true;
    if (!in.is_null(key)) {
        change.value = in.text(key);
    }
}

[[nodiscard]] auto read_components(argument_reader& in, const std::string& node_name)
    -> component_changes {
    component_changes changes;
    read_volume(in, changes);
    read_anim_target(in, node_name, changes);
    read_sockets(in, changes);
    read_variant(in, changes);
    read_structure(in, changes);
    read_point(in, "furniture", changes.furniture);
    read_point(in, "connection", changes.connection);
    return changes;
}

[[nodiscard]] auto requests_anything(const component_changes& changes) -> bool {
    return changes.volume.requested || changes.anim_target.requested ||
        changes.sockets.requested || changes.variant.requested || changes.structure.requested ||
        changes.furniture.requested || changes.connection.requested;
}

[[nodiscard]] auto answer(
    const mcp_bindings& bindings, const node_service::outcome& outcome, std::string_view name
) -> tool_outcome {
    if (!outcome) {
        return tool_failure(outcome.error());
    }
    return tool_success(describe_node(bindings, name));
}

[[nodiscard]] auto current_placement(const mcp_bindings& bindings, std::string_view name)
    -> transform {
    const auto& scene = bindings.state->scene;
    const auto found  = scene.name_to_entity.find(std::string{name});
    if (found == scene.name_to_entity.end()) {
        return {};
    }
    return bindings.engine->get_world().get<ecs::transform_component>(found->second).get_transform();
}

}  // namespace

auto describe_node(const mcp_bindings& bindings, std::string_view name) -> json::value {
    const auto& scene = bindings.state->scene;
    const auto found  = scene.name_to_entity.find(std::string{name});
    if (found == scene.name_to_entity.end()) {
        return {};
    }

    auto& world           = bindings.engine->get_world();
    const ecs::entity ent = found->second;

    json::object node{{"name", name}};

    const ecs::entity parent = world.get<ecs::hierarchy_component>(ent).get_parent();
    const auto parent_name   = scene.entity_to_name.find(parent);
    node.set(
        "parent", parent_name == scene.entity_to_name.end() ? json::value{} : json::value{parent_name->second}
    );

    const auto& placement = world.get<ecs::transform_component>(ent);
    node.set("position", json_of(placement.get_position()));
    node.set("rotation_degrees", json_of(degrees_of(placement.get_rotation())));
    node.set("scale", json_of(placement.get_scale()));

    if (world.has<ecs::model_component>(ent)) {
        const auto& model_comp = world.get<ecs::model_component>(ent);

        json::object volume{{"ref", json_or_null(model_comp.get_source().str())}};
        if (model_comp.has_model()) {
            volume.set("size", json_of(model_comp.get_model()->size()));
            volume.set("pivot", json_of(model_comp.get_model()->pivot()));
        }
        node.set("volume", std::move(volume));
    }

    if (world.has<ecs::animation_target_component>(ent)) {
        node.set("anim_target", world.get<ecs::animation_target_component>(ent).get_name());
    }

    if (world.has<ecs::socket_component>(ent)) {
        json::array sockets;
        for (const ecs::socket_point& point : world.get<ecs::socket_component>(ent).get_sockets()) {
            sockets.emplace_back(json::object{
                {"name", point.name},
                {"position", json_of(point.position)},
                {"rotation_degrees", json_of(degrees_of(point.rotation))},
                {"scale", json_of(point.scale)},
            });
        }
        node.set("sockets", std::move(sockets));
    }

    if (world.has<ecs::variant_slot_component>(ent)) {
        const auto& slot = world.get<ecs::variant_slot_component>(ent);

        const auto listed = [](const auto& items) {
            json::array texts;
            for (const auto& item : items) {
                texts.emplace_back(std::string_view{item});
            }
            return texts;
        };

        json::array candidates;
        for (const asset::asset_ref& candidate : slot.get_candidates()) {
            candidates.emplace_back(candidate.str());
        }

        json::object variant{
            {"candidates", std::move(candidates)},
            {"selected", slot.get_selected()},
        };
        if (!slot.required_targets().empty()) {
            variant.set("required_targets", listed(slot.required_targets()));
        }
        if (!slot.required_sockets().empty()) {
            variant.set("required_sockets", listed(slot.required_sockets()));
        }
        node.set("variant", std::move(variant));
    }

    if (world.has<ecs::structure_component>(ent)) {
        const auto& structure = world.get<ecs::structure_component>(ent);

        json::array races;
        for (const std::string& race : structure.get_races()) {
            races.emplace_back(race);
        }

        node.set(
            "structure",
            json::object{
                {"type", structure.get_type()},
                {"races", std::move(races)},
                {"tier", structure.get_tier()},
                {"size", name_of(structure.get_size())},
            }
        );
    }

    if (world.has<ecs::furniture_point_component>(ent)) {
        node.set("furniture", world.get<ecs::furniture_point_component>(ent).get_category());
    }
    if (world.has<ecs::connection_point_component>(ent)) {
        node.set("connection", world.get<ecs::connection_point_component>(ent).get_profile());
    }

    return node;
}

auto append_node_tools(std::vector<mcp_tool>& tools, const mcp_bindings& bindings) -> void {
    tools.push_back(mcp_tool{
        .name = "node_create",
        .description =
            "Add a node to the open prefab, with its placement and any components, as one undo "
            "step. The first node of an empty prefab is its root and takes no parent; every "
            "other node names its parent. Returns the node as prefab_get shows it.",
        .input_schema = create_schema(),
        .run          = when_idle(
            bindings,
            [bindings](const json::value& arguments) -> tool_outcome {
                argument_reader in{arguments};
                in.allow({
                    "name", "parent", "position", "rotation_degrees", "scale", "volume",
                    "anim_target", "sockets", "variant", "structure", "furniture", "connection",
                });

                node_spec spec;
                spec.name       = in.text("name");
                spec.parent     = in.optional_text("parent").value_or(std::string{});
                spec.placement  = read_placement(in, transform{});
                spec.components = read_components(in, spec.name);
                if (in.failed()) {
                    return tool_failure(in.error());
                }

                return answer(bindings, bindings.nodes->create(spec), spec.name);
            }
        ),
    });

    tools.push_back(mcp_tool{
        .name = "node_delete",
        .description =
            "Delete a node together with every node under it, as one undo step. Deleting the "
            "root empties the prefab. Volume files on disk are left alone.",
        .input_schema = node_name_only_schema,
        .run          = when_idle(
            bindings,
            [bindings](const json::value& arguments) -> tool_outcome {
                argument_reader in{arguments};
                in.allow({"name"});
                const std::string name = in.text("name");
                if (in.failed()) {
                    return tool_failure(in.error());
                }

                const auto removed = bindings.nodes->remove(name);
                if (!removed) {
                    return tool_failure(removed.error());
                }
                return tool_success(json::object{
                    {"deleted", name},
                    {"node_count", bindings.state->scene.name_to_entity.size()},
                    {"root_node", json_or_null(bindings.state->scene.root_name)},
                });
            }
        ),
    });

    tools.push_back(mcp_tool{
        .name = "node_move",
        .description =
            "Put a node under another parent or at another place among its siblings. The node "
            "stays where it is in the world: its position, rotation and scale are recomputed "
            "relative to the new parent. The root cannot be moved.",
        .input_schema = move_schema,
        .run          = when_idle(
            bindings,
            [bindings](const json::value& arguments) -> tool_outcome {
                argument_reader in{arguments};
                in.allow({"name", "parent", "index"});
                const std::string name   = in.text("name");
                const std::string parent = in.text("parent");
                const auto index         = in.optional_index("index");
                if (in.failed()) {
                    return tool_failure(in.error());
                }

                return answer(bindings, bindings.nodes->reparent(name, parent, index), name);
            }
        ),
    });

    tools.push_back(mcp_tool{
        .name = "node_set_transform",
        .description =
            "Set the position, rotation or scale of a node relative to its parent. Fields left "
            "out keep their value. Clips key absolute transforms, so a node that clips animate "
            "keeps playing from the keyed values, not from the new one.",
        .input_schema = transform_schema(),
        .run          = when_idle(
            bindings,
            [bindings](const json::value& arguments) -> tool_outcome {
                argument_reader in{arguments};
                in.allow({"name", "position", "rotation_degrees", "scale"});
                const std::string name = in.text("name");
                const transform placement =
                    read_placement(in, current_placement(bindings, name));
                if (in.failed()) {
                    return tool_failure(in.error());
                }
                if (!in.has("position") && !in.has("rotation_degrees") && !in.has("scale")) {
                    return tool_failure("give at least one of position, rotation_degrees and scale");
                }

                return answer(bindings, bindings.nodes->set_transform(name, placement), name);
            }
        ),
    });

    tools.push_back(mcp_tool{
        .name = "node_set_components",
        .description =
            "Add, change or remove components of a node as one undo step. Only the components "
            "named in the call are touched; a component given as null is removed. Returns the "
            "node as prefab_get shows it.",
        .input_schema = components_schema(),
        .run          = when_idle(
            bindings,
            [bindings](const json::value& arguments) -> tool_outcome {
                argument_reader in{arguments};
                in.allow({
                    "name", "volume", "anim_target", "sockets", "variant", "structure",
                    "furniture", "connection",
                });
                const std::string name          = in.text("name");
                const component_changes changes = read_components(in, name);
                if (in.failed()) {
                    return tool_failure(in.error());
                }
                if (!requests_anything(changes)) {
                    return tool_failure(
                        "name at least one component: volume, anim_target, sockets, variant, "
                        "structure, furniture or connection"
                    );
                }

                return answer(bindings, bindings.nodes->set_components(name, changes), name);
            }
        ),
    });

    tools.push_back(mcp_tool{
        .name = "prefab_set_rig",
        .description =
            "Name the rig of the open prefab. A clip or a state machine belongs to the prefab "
            "only if it carries the same rig name.",
        .input_schema = rig_schema,
        .run          = when_idle(
            bindings,
            [bindings](const json::value& arguments) -> tool_outcome {
                argument_reader in{arguments};
                in.allow({"rig"});
                const std::string rig = in.text("rig");
                if (in.failed()) {
                    return tool_failure(in.error());
                }

                const auto named = bindings.nodes->set_rig(rig);
                if (!named) {
                    return tool_failure(named.error());
                }
                return tool_success(json::object{{"rig", rig}});
            }
        ),
    });
}

}  // namespace vw::sculptor
