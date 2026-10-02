module vw.sculptor;

import std;

import vw.core;
import vw.asset;
import vw.ecs;
import vw.world;
import vw.gfx;

namespace vw::sculptor::mcp {

namespace {

constexpr std::string_view no_arguments =
    R"({"type": "object", "properties": {}, "additionalProperties": false})";

constexpr std::string_view get_schema = R"({
    "type": "object",
    "properties": {
        "machine": {"type": "string", "description": "The state machine: 'fsm/name.voxf' or just its name. Defaults to the machine open in the editor."}
    },
    "additionalProperties": false
})";

constexpr std::string_view create_schema = R"({
    "type": "object",
    "properties": {
        "name": {"type": "string", "description": "Name of the machine and of its file: letters, digits, '_' and '-'."},
        "attach": {"type": "boolean", "description": "Add the machine to the open prefab as its next layer. Default true."}
    },
    "required": ["name"],
    "additionalProperties": false
})";

constexpr std::string_view machines_schema = R"({
    "type": "object",
    "properties": {
        "machines": {"type": "array", "items": {"type": "string"}, "description": "The state machines of the prefab in layer order, 'fsm/name.voxf' or just names. Machine i drives animation layer i; layer 0 is the base and higher layers play over it. [] detaches all."}
    },
    "required": ["machines"],
    "additionalProperties": false
})";

constexpr std::string_view transition_schema = R"({
    "type": "object",
    "properties": {
        "to": {"type": "string", "description": "The state to go to."},
        "on": {"type": "string", "description": "A trigger parameter that must fire."},
        "when": {
            "type": "array",
            "description": "Conditions on value parameters; all must hold.",
            "items": {
                "type": "object",
                "properties": {
                    "param": {"type": "string"},
                    "op": {"enum": ["==", "!=", "<", "<=", ">", ">="]},
                    "value": {"type": ["number", "boolean"]}
                },
                "required": ["param", "op", "value"],
                "additionalProperties": false
            }
        },
        "blend": {"description": "Cross-fade into the target state: seconds, or {duration, interp, tangent_in, tangent_out}.", "type": ["number", "object"]},
        "wait_end": {"type": "boolean", "description": "Go only once the clip of this state has played to its end."},
        "wait_blend": {"type": "boolean", "description": "Go only once the blend into this state has finished."}
    },
    "required": ["to"],
    "additionalProperties": false
})";

[[nodiscard]] auto set_schema() -> std::string_view {
    static const std::string schema = std::format(
        R"({{
    "type": "object",
    "properties": {{
        "machine": {{"type": "string", "description": "The state machine to replace: 'fsm/name.voxf' or just its name. It must be a machine of the open prefab."}},
        "rig": {{"type": "string", "description": "Rig the machine is for. Defaults to the rig of the prefab."}},
        "entry": {{"type": "string", "description": "The state the machine starts in."}},
        "params": {{
            "type": "array",
            "description": "Parameters the transitions read. 'trigger' fires for one tick; the others hold a value. The game sets 'speed' and 'grounded' by itself when they are declared.",
            "items": {{
                "type": "object",
                "properties": {{
                    "name": {{"type": "string"}},
                    "type": {{"enum": ["float", "int", "bool", "trigger"]}},
                    "value": {{"type": ["number", "boolean"], "description": "Starting value. Default 0 or false."}}
                }},
                "required": ["name", "type"],
                "additionalProperties": false
            }}
        }},
        "states": {{
            "type": "array",
            "description": "Every state of the machine. Transitions of a state are tried in order.",
            "items": {{
                "type": "object",
                "properties": {{
                    "name": {{"type": "string"}},
                    "clip": {{"type": ["string", "null"], "description": "Clip the state plays, 'animations/name.voxa'. A state without a clip lets the layer fade out."}},
                    "playback": {{"enum": ["once", "loop", "ping_pong"], "description": "Default loop."}},
                    "rate": {{"type": "number", "description": "Playback speed. Default 1."}},
                    "fade_in": {{"description": "Fade of the whole layer when the state begins: seconds, or {{duration, interp, tangent_in, tangent_out}}.", "type": ["number", "object"]}},
                    "fade_out": {{"description": "Fade of the whole layer when the state ends.", "type": ["number", "object"]}},
                    "transitions": {{"type": "array", "items": {0}}}
                }},
                "required": ["name"],
                "additionalProperties": false
            }}
        }},
        "any": {{"type": "array", "description": "Transitions tried from every state, before the state's own.", "items": {0}}}
    }},
    "required": ["machine", "entry", "states"],
    "additionalProperties": false
}})",
        transition_schema
    );
    return schema;
}

constexpr std::array<std::pair<std::string_view, math::interpolation_type>, 6> interp_names{{
    {"linear", math::interpolation_type::linear},
    {"step", math::interpolation_type::step},
    {"ease_in", math::interpolation_type::ease_in},
    {"ease_out", math::interpolation_type::ease_out},
    {"ease_in_out", math::interpolation_type::ease_in_out},
    {"cubic_bezier", math::interpolation_type::cubic_bezier},
}};

constexpr std::array<std::pair<std::string_view, asset::fsm_compare>, 6> compare_names{{
    {"==", asset::fsm_compare::equal},
    {"!=", asset::fsm_compare::not_equal},
    {"<", asset::fsm_compare::less},
    {"<=", asset::fsm_compare::less_equal},
    {">", asset::fsm_compare::greater},
    {">=", asset::fsm_compare::greater_equal},
}};

constexpr std::array<std::pair<std::string_view, asset::voxf_param_type>, 4> param_type_names{{
    {"float", asset::voxf_param_type::real},
    {"int", asset::voxf_param_type::integer},
    {"bool", asset::voxf_param_type::boolean},
    {"trigger", asset::voxf_param_type::trigger},
}};

constexpr std::array<std::pair<std::string_view, asset::animation_loop_mode>, 3> playback_names{{
    {"once", asset::animation_loop_mode::once},
    {"loop", asset::animation_loop_mode::loop},
    {"ping_pong", asset::animation_loop_mode::ping_pong},
}};

template <typename T, std::size_t N>
[[nodiscard]] auto name_of(const std::array<std::pair<std::string_view, T>, N>& names, T value)
    -> std::string_view {
    const auto found = std::ranges::find(names, value, &std::pair<std::string_view, T>::second);
    return found == names.end() ? names.front().first : found->first;
}

template <typename T, std::size_t N>
[[nodiscard]] auto value_of(
    const std::array<std::pair<std::string_view, T>, N>& names, std::string_view name
) -> std::optional<T> {
    const auto found = std::ranges::find(names, name, &std::pair<std::string_view, T>::first);
    return found == names.end() ? std::nullopt : std::optional{found->second};
}

template <typename T, std::size_t N>
[[nodiscard]] auto choices_of(const std::array<std::pair<std::string_view, T>, N>& names)
    -> std::string {
    std::string list;
    for (const auto& entry : names) {
        list += list.empty() ? std::string{entry.first} : std::format(", {}", entry.first);
    }
    return list;
}

[[nodiscard]] auto describe_fade(const asset::transition& fade) -> json::value {
    const bool plain = fade.interp == math::interpolation_type::linear &&
        fade.tangent_in == 0.0F && fade.tangent_out == 1.0F;
    if (plain) {
        return json_of(fade.duration);
    }

    json::object described{
        {"duration", json_of(fade.duration)},
        {"interp", name_of(interp_names, fade.interp)},
    };
    if (fade.interp == math::interpolation_type::cubic_bezier) {
        described.set("tangent_in", json_of(fade.tangent_in));
        described.set("tangent_out", json_of(fade.tangent_out));
    }
    return described;
}

[[nodiscard]] auto describe_value(asset::voxf_param_type type, float32 value) -> json::value {
    switch (type) {
        case asset::voxf_param_type::boolean:
            return value != 0.0F;
        case asset::voxf_param_type::integer:
            return static_cast<int64>(std::lround(value));
        case asset::voxf_param_type::real:
        case asset::voxf_param_type::trigger:
            break;
    }
    return json_of(value);
}

[[nodiscard]] auto describe_rule(
    const asset::voxf_data& data, const asset::animation_fsm::transition_rule& rule
) -> json::value {
    json::object described{{"to", rule.target_state}};

    if (!rule.trigger_name.empty()) {
        described.set("on", rule.trigger_name);
    }

    if (!rule.conditions.empty()) {
        json::array conditions;
        for (const asset::fsm_condition& condition : rule.conditions) {
            const auto param = std::ranges::find(data.params, condition.parameter, &asset::voxf_param::name);
            const auto type  = param == data.params.end() ? asset::voxf_param_type::real : param->type;

            conditions.emplace_back(json::object{
                {"param", condition.parameter},
                {"op", name_of(compare_names, condition.compare)},
                {"value", describe_value(type, condition.value)},
            });
        }
        described.set("when", std::move(conditions));
    }

    if (rule.blend.duration != 0.0F || rule.blend.interp != math::interpolation_type::linear) {
        described.set("blend", describe_fade(rule.blend));
    }
    if (rule.wait_until_end) {
        described.set("wait_end", true);
    }
    if (rule.wait_until_blend) {
        described.set("wait_blend", true);
    }
    return described;
}

[[nodiscard]] auto describe_machine(
    const editor_bindings& bindings, const asset::asset_ref& machine, const asset::voxf_data& data
) -> json::value {
    json::array params;
    for (const asset::voxf_param& param : data.params) {
        json::object entry{
            {"name", param.name},
            {"type", name_of(param_type_names, param.type)},
        };
        if (param.type != asset::voxf_param_type::trigger) {
            entry.set("value", describe_value(param.type, param.value));
        }
        params.emplace_back(std::move(entry));
    }

    json::array states;
    for (const asset::voxf_state& state : data.states) {
        json::object entry{
            {"name", state.name},
            {"clip", json_or_null(state.clip.str())},
            {"playback", name_of(playback_names, state.loop_mode)},
            {"rate", json_of(state.rate)},
        };
        if (state.fade_in.duration != 0.0F) {
            entry.set("fade_in", describe_fade(state.fade_in));
        }
        if (state.fade_out.duration != 0.0F) {
            entry.set("fade_out", describe_fade(state.fade_out));
        }

        json::array transitions;
        for (const auto& rule : state.transitions) {
            transitions.push_back(describe_rule(data, rule));
        }
        entry.set("transitions", std::move(transitions));
        states.emplace_back(std::move(entry));
    }

    json::array any;
    for (const auto& rule : data.any_transitions) {
        any.push_back(describe_rule(data, rule));
    }

    const auto layer = bindings.machines->layer_of(machine);
    const auto& open = bindings.state->fsm;

    return json::object{
        {"machine", machine.str()},
        {"layer", layer ? json::value{*layer} : json::value{}},
        {"unsaved", open.is_open() && open.source == machine && open.has_unsaved_changes},
        {"rig", json_or_null(data.rig)},
        {"entry", data.entry_state},
        {"params", std::move(params)},
        {"states", std::move(states)},
        {"any", std::move(any)},
    };
}

[[nodiscard]] auto read_number(argument_reader& in, std::string_view key, float32 fallback)
    -> float32 {
    if (!in.has(key) || in.is_null(key)) {
        return fallback;
    }

    const json::value* given = in.at(key).get();
    if (const auto flag = given->as_bool()) {
        return *flag ? 1.0F : 0.0F;
    }

    const auto number = in.at(key).number();
    if (!number) {
        in.fail(json::describe(number.error()));
        return fallback;
    }
    return static_cast<float32>(*number);
}

[[nodiscard]] auto read_fade(argument_reader& in, std::string_view key) -> asset::transition {
    asset::transition fade;
    if (!in.has(key) || in.is_null(key)) {
        return fade;
    }

    const json::cursor at = in.at(key);
    if (at.get()->is_number()) {
        fade.duration = static_cast<float32>(*at.get()->as_number());
        return fade;
    }

    argument_reader fields{at};
    fields.allow({"duration", "interp", "tangent_in", "tangent_out"});

    fade.duration    = read_number(fields, "duration", 0.0F);
    fade.tangent_in  = read_number(fields, "tangent_in", 0.0F);
    fade.tangent_out = read_number(fields, "tangent_out", 1.0F);

    const std::string interp_name = fields.optional_text("interp").value_or(std::string{"linear"});
    const auto interp             = value_of(interp_names, interp_name);
    if (!interp) {
        fields.fail(std::format(
            "{}: expected one of {}, found '{}'", at["interp"].path(), choices_of(interp_names),
            interp_name
        ));
    }
    fade.interp = interp.value_or(math::interpolation_type::linear);

    if (fields.failed()) {
        in.fail(fields.error());
    }
    return fade;
}

[[nodiscard]] auto read_rules(argument_reader& in, std::string_view key)
    -> std::vector<asset::animation_fsm::transition_rule> {
    std::vector<asset::animation_fsm::transition_rule> rules;
    if (!in.has(key) || in.is_null(key)) {
        return rules;
    }

    const auto entries = in.at(key).elements();
    if (!entries) {
        in.fail(json::describe(entries.error()));
        return rules;
    }

    for (const json::cursor& entry : *entries) {
        argument_reader fields{entry};
        fields.allow({"to", "on", "when", "blend", "wait_end", "wait_blend"});

        asset::animation_fsm::transition_rule rule;
        rule.target_state     = fields.text("to");
        rule.trigger_name     = fields.optional_text("on").value_or(std::string{});
        rule.blend            = read_fade(fields, "blend");
        rule.wait_until_end   = fields.flag_or("wait_end", false);
        rule.wait_until_blend = fields.flag_or("wait_blend", false);

        if (fields.has("when") && !fields.is_null("when")) {
            const auto conditions = fields.at("when").elements();
            if (!conditions) {
                fields.fail(json::describe(conditions.error()));
            } else {
                for (const json::cursor& condition_at : *conditions) {
                    argument_reader condition{condition_at};
                    condition.allow({"param", "op", "value"});

                    const std::string op_name = condition.text("op");
                    const auto compare        = value_of(compare_names, op_name);
                    if (!condition.failed() && !compare) {
                        condition.fail(std::format(
                            "{}: expected one of {}, found '{}'", condition_at["op"].path(),
                            choices_of(compare_names), op_name
                        ));
                    }
                    if (!condition.has("value")) {
                        condition.fail(std::format(
                            "{}: missing, expected number or boolean", condition_at["value"].path()
                        ));
                    }

                    rule.conditions.push_back(asset::fsm_condition{
                        .parameter = condition.text("param"),
                        .compare   = compare.value_or(asset::fsm_compare::equal),
                        .value     = read_number(condition, "value", 0.0F),
                    });
                    if (condition.failed()) {
                        fields.fail(condition.error());
                        break;
                    }
                }
            }
        }

        if (fields.failed()) {
            in.fail(fields.error());
            return rules;
        }
        rules.push_back(std::move(rule));
    }
    return rules;
}

[[nodiscard]] auto read_machine(argument_reader& in, std::string fallback_rig) -> asset::voxf_data {
    asset::voxf_data data;
    data.rig         = in.optional_text("rig").value_or(std::move(fallback_rig));
    data.entry_state = in.text("entry");

    if (in.has("params") && !in.is_null("params")) {
        const auto entries = in.at("params").elements();
        if (!entries) {
            in.fail(json::describe(entries.error()));
        } else {
            for (const json::cursor& entry : *entries) {
                argument_reader fields{entry};
                fields.allow({"name", "type", "value"});

                const std::string type_name = fields.text("type");
                const auto type             = value_of(param_type_names, type_name);
                if (!fields.failed() && !type) {
                    fields.fail(std::format(
                        "{}: expected one of {}, found '{}'", entry["type"].path(),
                        choices_of(param_type_names), type_name
                    ));
                }

                data.params.push_back(asset::voxf_param{
                    .name  = fields.text("name"),
                    .type  = type.value_or(asset::voxf_param_type::real),
                    .value = read_number(fields, "value", 0.0F),
                });
                if (fields.failed()) {
                    in.fail(fields.error());
                    return data;
                }
            }
        }
    }

    const auto states = in.at("states").elements();
    if (!states) {
        in.fail(json::describe(states.error()));
        return data;
    }

    for (const json::cursor& entry : *states) {
        argument_reader fields{entry};
        fields.allow({"name", "clip", "playback", "rate", "fade_in", "fade_out", "transitions"});

        const std::string playback_name =
            fields.optional_text("playback").value_or(std::string{"loop"});
        const auto playback = value_of(playback_names, playback_name);
        if (!playback) {
            fields.fail(std::format(
                "{}: expected one of {}, found '{}'", entry["playback"].path(),
                choices_of(playback_names), playback_name
            ));
        }

        asset::voxf_state state;
        state.name        = fields.text("name");
        state.clip        = asset::asset_ref{fields.optional_text("clip").value_or(std::string{})};
        state.loop_mode   = playback.value_or(asset::animation_loop_mode::loop);
        state.rate        = read_number(fields, "rate", 1.0F);
        state.fade_in     = read_fade(fields, "fade_in");
        state.fade_out    = read_fade(fields, "fade_out");
        state.transitions = read_rules(fields, "transitions");

        if (fields.failed()) {
            in.fail(fields.error());
            return data;
        }
        data.states.push_back(std::move(state));
    }

    data.any_transitions = read_rules(in, "any");
    return data;
}

[[nodiscard]] auto rig_of_prefab(const editor_bindings& bindings) -> std::string {
    const auto& scene = bindings.state->scene;
    const auto root   = scene.name_to_entity.find(scene.root_name);
    if (root == scene.name_to_entity.end()) {
        return {};
    }

    auto& world = bindings.engine->get_world();
    return world.has<ecs::rig_component>(root->second)
               ? world.get<ecs::rig_component>(root->second).get_name()
               : std::string{};
}

[[nodiscard]] auto answer_with_machine(const editor_bindings& bindings, const asset::asset_ref& machine)
    -> tool_outcome {
    const auto data = bindings.machines->read(machine);
    if (!data) {
        return tool_failure(data.error());
    }
    return tool_success(describe_machine(bindings, machine, *data));
}

[[nodiscard]] auto describe_attached(const editor_bindings& bindings) -> json::value {
    json::array attached;
    for (const asset::asset_ref& machine : bindings.machines->machines()) {
        attached.emplace_back(machine.str());
    }
    return json::object{{"machines", std::move(attached)}};
}

}  // namespace

auto append_fsm_tools(std::vector<tool>& tools, const editor_bindings& bindings) -> void {
    tools.push_back(tool{
        .name = "fsm_get",
        .description =
            "Read a state machine: its parameters, its states with the clip each plays and the "
            "transitions out of each, and the transitions tried from any state. The machine "
            "open in the editor is read with its unsaved changes, any other from its file.",
        .input_schema = get_schema,
        .run =
            [bindings](const json::value& arguments) -> tool_outcome {
                argument_reader in{arguments};
                in.allow({"machine"});
                const auto named = in.optional_text("machine");
                if (in.failed()) {
                    return tool_failure(in.error());
                }

                if (!named && !bindings.state->fsm.is_open()) {
                    return tool_failure(
                        "no state machine is open; name one with 'machine', assets_list lists them"
                    );
                }
                return answer_with_machine(
                    bindings, named ? fsm_service::machine_ref(*named) : bindings.state->fsm.source
                );
            },
    });

    tools.push_back(tool{
        .name = "fsm_set",
        .description =
            "Replace a state machine of the open prefab with the one given, whole, as one undo "
            "step: read it with fsm_get, change what is needed and send it back. The machine "
            "is checked first: names are unique, every transition goes to a state, triggers and "
            "conditions read declared parameters, clips exist. Opens the machine in the editor; "
            "fsm_save writes it.",
        .input_schema = set_schema(),
        .run          = when_idle(
            bindings,
            [bindings](const json::value& arguments) -> tool_outcome {
                argument_reader in{arguments};
                in.allow({"machine", "rig", "entry", "params", "states", "any"});
                const std::string named     = in.text("machine");
                const asset::voxf_data data = read_machine(in, rig_of_prefab(bindings));
                if (in.failed()) {
                    return tool_failure(in.error());
                }

                const asset::asset_ref machine = fsm_service::machine_ref(named);
                const auto replaced            = bindings.machines->replace(machine, data);
                if (!replaced) {
                    return tool_failure(replaced.error());
                }
                return answer_with_machine(bindings, machine);
            }
        ),
    });

    tools.push_back(tool{
        .name         = "fsm_save",
        .description  = "Write the state machine open in the editor to its file.",
        .input_schema = no_arguments,
        .run          = when_idle(
            bindings,
            [bindings](const json::value&) -> tool_outcome {
                const auto saved = bindings.machines->save_open();
                if (!saved) {
                    return tool_failure(saved.error());
                }
                return tool_success(json::object{
                    {"saved", bindings.state->fsm.source.str()},
                    {"unsaved", bindings.state->fsm.has_unsaved_changes},
                });
            }
        ),
    });

    tools.push_back(tool{
        .name = "fsm_create",
        .description =
            "Make a new state machine file with a single state 'idle' and, unless told not to, "
            "attach it to the open prefab as its next layer. The file is written at once and "
            "is not removed by undo; the attachment is.",
        .input_schema = create_schema,
        .run          = when_idle(
            bindings,
            [bindings](const json::value& arguments) -> tool_outcome {
                argument_reader in{arguments};
                in.allow({"name", "attach"});
                const std::string name = in.text("name");
                const bool attach      = in.flag_or("attach", true);
                if (in.failed()) {
                    return tool_failure(in.error());
                }

                const auto made = bindings.machines->create_machine(name, attach);
                if (!made) {
                    return tool_failure(made.error());
                }
                return answer_with_machine(bindings, *made);
            }
        ),
    });

    tools.push_back(tool{
        .name = "prefab_set_machines",
        .description =
            "Set which state machines the open prefab runs and in what order. Machine i drives "
            "animation layer i: layer 0 is the base, higher layers play over it on the targets "
            "their clips animate.",
        .input_schema = machines_schema,
        .run          = when_idle(
            bindings,
            [bindings](const json::value& arguments) -> tool_outcome {
                argument_reader in{arguments};
                in.allow({"machines"});
                if (!in.has("machines")) {
                    in.fail("arguments.machines: missing, expected array");
                }

                std::vector<asset::asset_ref> wanted;
                for (const std::string& name : in.text_list("machines")) {
                    wanted.push_back(fsm_service::machine_ref(name));
                }
                if (in.failed()) {
                    return tool_failure(in.error());
                }

                const auto set = bindings.machines->set_machines(std::move(wanted));
                if (!set) {
                    return tool_failure(set.error());
                }
                return tool_success(describe_attached(bindings));
            }
        ),
    });
}

}  // namespace vw::sculptor::mcp
