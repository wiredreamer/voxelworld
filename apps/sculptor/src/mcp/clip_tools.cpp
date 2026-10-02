module vw.sculptor;

import std;

import vw.core;
import vw.asset;
import vw.ecs;
import vw.world;
import vw.gfx;

namespace vw::sculptor::mcp {

namespace {

constexpr float32 end_of_time = std::numeric_limits<float32>::max();

constexpr std::string_view clip_only_schema = R"({
    "type": "object",
    "properties": {
        "clip": {"type": "string", "description": "Name of an open clip. Defaults to the clip selected in the editor."}
    },
    "additionalProperties": false
})";

constexpr std::string_view create_schema = R"({
    "type": "object",
    "properties": {
        "name": {"type": "string", "description": "Name of the clip and of its file: letters, digits, '_' and '-'."},
        "overwrite": {"type": "boolean", "description": "Start anew over a clip file that already exists. Default false."}
    },
    "required": ["name"],
    "additionalProperties": false
})";

constexpr std::string_view open_schema = R"({
    "type": "object",
    "properties": {
        "name": {"type": "string", "description": "Name of the clip file in the animations folder, with or without .voxa."},
        "ignore_rig": {"type": "boolean", "description": "Open the clip although its rig or its targets do not match the prefab. Default false."}
    },
    "required": ["name"],
    "additionalProperties": false
})";

constexpr std::string_view close_schema = R"({
    "type": "object",
    "properties": {
        "clip": {"type": "string", "description": "Name of an open clip. Defaults to the clip selected in the editor."},
        "discard_unsaved": {"type": "boolean", "description": "Drop unsaved changes of the clip. Default false."}
    },
    "additionalProperties": false
})";

constexpr std::string_view set_keys_schema = R"({
    "type": "object",
    "properties": {
        "clip": {"type": "string", "description": "Name of an open clip. Defaults to the clip selected in the editor."},
        "keys": {
            "type": "array",
            "description": "Keys to put. A key sets, at one time, any of the position, rotation and scale of one target; values are the node's transform relative to its parent, absolute, not offsets from the rest pose. A key put where one already stands replaces it.",
            "items": {
                "type": "object",
                "properties": {
                    "target": {"type": "string", "description": "Animation target, the anim_target of a node."},
                    "time": {"type": "number", "minimum": 0, "description": "Seconds from the start of the clip."},
                    "position": {"type": "array", "items": {"type": "number"}, "minItems": 3, "maxItems": 3},
                    "rotation_degrees": {"type": "array", "items": {"type": "number"}, "minItems": 3, "maxItems": 3},
                    "scale": {"type": "array", "items": {"type": "number"}, "minItems": 3, "maxItems": 3},
                    "interp": {"enum": ["linear", "step", "ease_in", "ease_out", "ease_in_out", "cubic_bezier"], "description": "How the value moves from this key to the next. Default linear."},
                    "tangent_in": {"type": "number", "description": "First control value of cubic_bezier. Default 0."},
                    "tangent_out": {"type": "number", "description": "Second control value of cubic_bezier. Default 1."}
                },
                "required": ["target", "time"],
                "additionalProperties": false
            }
        }
    },
    "required": ["keys"],
    "additionalProperties": false
})";

constexpr std::string_view remove_keys_schema = R"({
    "type": "object",
    "properties": {
        "clip": {"type": "string", "description": "Name of an open clip. Defaults to the clip selected in the editor."},
        "target": {"type": "string", "description": "Animation target whose keys to remove."},
        "property": {"enum": ["position", "rotation", "scale"], "description": "Remove the keys of this property only. All three when omitted."},
        "from": {"type": "number", "description": "Start of the span of time, inclusive. Default 0."},
        "to": {"type": "number", "description": "End of the span of time, inclusive. Default the end of the clip."}
    },
    "required": ["target"],
    "additionalProperties": false
})";

constexpr std::string_view pose_schema = R"({
    "type": "object",
    "properties": {
        "clip": {"type": "string", "description": "Name of an open clip. Defaults to the clip selected in the editor."},
        "time": {"type": "number", "minimum": 0, "description": "Seconds from the start of the clip."}
    },
    "required": ["time"],
    "additionalProperties": false
})";

constexpr std::array<std::pair<std::string_view, math::interpolation_type>, 6> interp_names{{
    {"linear", math::interpolation_type::linear},
    {"step", math::interpolation_type::step},
    {"ease_in", math::interpolation_type::ease_in},
    {"ease_out", math::interpolation_type::ease_out},
    {"ease_in_out", math::interpolation_type::ease_in_out},
    {"cubic_bezier", math::interpolation_type::cubic_bezier},
}};

[[nodiscard]] auto name_of(math::interpolation_type interp) -> std::string_view {
    const auto found = std::ranges::find(
        interp_names, interp, &std::pair<std::string_view, math::interpolation_type>::second
    );
    return found == interp_names.end() ? std::string_view{"linear"} : found->first;
}

[[nodiscard]] auto interp_of(std::string_view name) -> std::optional<math::interpolation_type> {
    const auto found = std::ranges::find(
        interp_names, name, &std::pair<std::string_view, math::interpolation_type>::first
    );
    return found == interp_names.end() ? std::nullopt : std::optional{found->second};
}

[[nodiscard]] auto property_of(std::string_view name) -> std::optional<asset::animation_property> {
    if (name == "position") {
        return asset::animation_property::position;
    }
    if (name == "rotation") {
        return asset::animation_property::rotation;
    }
    if (name == "scale") {
        return asset::animation_property::scale;
    }
    return std::nullopt;
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

[[nodiscard]] auto clip_name_of(const editor_bindings& bindings, argument_reader& in) -> std::string {
    if (const auto named = in.optional_text("clip")) {
        return *named;
    }
    if (bindings.state->anim.selected_clip_name.empty()) {
        in.fail("no clip is open; open one with clip_open or make one with clip_create");
    }
    return bindings.state->anim.selected_clip_name;
}

struct merged_key {
    float32 time = 0.0F;
    math::interpolation_type interp = math::interpolation_type::linear;
    float32 tangent_in              = 0.0F;
    float32 tangent_out             = 1.0F;

    std::optional<vec3f> position;
    std::optional<vec3f> rotation_degrees;
    std::optional<vec3f> scale;
};

template <typename T>
[[nodiscard]] auto same_curve(const merged_key& merged, const asset::keyframe<T>& key) -> bool {
    return std::abs(merged.time - key.time) < asset::same_instant_seconds &&
        merged.interp == key.interp && merged.tangent_in == key.tangent_in &&
        merged.tangent_out == key.tangent_out;
}

template <typename T>
[[nodiscard]] auto slot_for(std::vector<merged_key>& merged, const asset::keyframe<T>& key)
    -> merged_key& {
    const auto found = std::ranges::find_if(merged, [&key](const merged_key& candidate) {
        return same_curve(candidate, key);
    });
    if (found != merged.end()) {
        return *found;
    }

    return merged.emplace_back(merged_key{
        .time             = key.time,
        .interp           = key.interp,
        .tangent_in       = key.tangent_in,
        .tangent_out      = key.tangent_out,
        .position         = {},
        .rotation_degrees = {},
        .scale            = {},
    });
}

[[nodiscard]] auto describe_track(const asset::animation_track& track) -> json::value {
    std::vector<merged_key> merged;

    if (const auto* channel = track.get_channel(asset::animation_property::position)) {
        for (const auto& key : std::get<asset::animation_channel<vec3f>>(*channel).get_keyframes()) {
            slot_for(merged, key).position = key.value;
        }
    }
    if (const auto* channel = track.get_channel(asset::animation_property::rotation)) {
        for (const auto& key : std::get<asset::animation_channel<quat>>(*channel).get_keyframes()) {
            slot_for(merged, key).rotation_degrees = degrees_of(key.value);
        }
    }
    if (const auto* channel = track.get_channel(asset::animation_property::scale)) {
        for (const auto& key : std::get<asset::animation_channel<vec3f>>(*channel).get_keyframes()) {
            slot_for(merged, key).scale = key.value;
        }
    }

    std::ranges::stable_sort(merged, {}, &merged_key::time);

    json::array keys;
    for (const merged_key& key : merged) {
        json::object entry{{"time", json_of(key.time)}};
        if (key.position) {
            entry.set("position", json_of(*key.position));
        }
        if (key.rotation_degrees) {
            entry.set("rotation_degrees", json_of(*key.rotation_degrees));
        }
        if (key.scale) {
            entry.set("scale", json_of(*key.scale));
        }
        if (key.interp != math::interpolation_type::linear) {
            entry.set("interp", name_of(key.interp));
        }
        if (key.interp == math::interpolation_type::cubic_bezier) {
            entry.set("tangent_in", json_of(key.tangent_in));
            entry.set("tangent_out", json_of(key.tangent_out));
        }
        keys.emplace_back(std::move(entry));
    }

    return json::object{
        {"target", track.get_target_name()},
        {"keys", std::move(keys)},
    };
}

[[nodiscard]] auto describe_clip(
    const editor_bindings& bindings, const asset::animation_clip& clip, bool with_tracks
) -> json::object {
    const auto& anim = bindings.state->anim;

    std::size_t key_count = 0;
    for (const asset::animation_track& track : clip.get_tracks()) {
        key_count += asset::count_keys(track);
    }

    json::object described{
        {"clip", clip.get_name()},
        {"rig", json_or_null(clip.get_rig())},
        {"duration", json_of(clip.get_duration())},
        {"track_count", clip.get_tracks().size()},
        {"key_count", key_count},
        {"unsaved", anim.has_unsaved_clip(clip.get_name())},
        {"selected", anim.selected_clip_name == clip.get_name()},
    };

    if (with_tracks) {
        json::array tracks;
        for (const asset::animation_track& track : clip.get_tracks()) {
            tracks.push_back(describe_track(track));
        }
        described.set("tracks", std::move(tracks));
    }
    return described;
}

[[nodiscard]] auto answer_with_clip(
    const editor_bindings& bindings, const clip_service::outcome& outcome, std::string_view name
) -> tool_outcome {
    if (!outcome) {
        return tool_failure(outcome.error());
    }

    const auto clip = bindings.clips->find(name);
    if (!clip) {
        return tool_failure(clip.error());
    }
    return tool_success(describe_clip(bindings, **clip, false));
}

[[nodiscard]] auto find_track(std::vector<asset::animation_track>& tracks, std::string_view target)
    -> asset::animation_track* {
    const auto found = std::ranges::find(tracks, target, &asset::animation_track::get_target_name);
    return found == tracks.end() ? nullptr : &*found;
}

[[nodiscard]] auto set_keys(const editor_bindings& bindings, const json::value& arguments)
    -> tool_outcome {
    argument_reader in{arguments};
    in.allow({"clip", "keys"});
    const std::string clip_name = clip_name_of(bindings, in);
    if (in.failed()) {
        return tool_failure(in.error());
    }

    const auto clip = bindings.clips->find(clip_name);
    if (!clip) {
        return tool_failure(clip.error());
    }

    const auto entries = in.at("keys").elements();
    if (!entries) {
        return tool_failure(json::describe(entries.error()));
    }
    if (entries->empty()) {
        return tool_failure("arguments.keys: give at least one key");
    }

    std::vector<asset::animation_track> tracks = (*clip)->get_tracks();

    for (const json::cursor& entry : *entries) {
        argument_reader key{entry};
        key.allow({
            "target", "time", "position", "rotation_degrees", "scale", "interp", "tangent_in",
            "tangent_out",
        });

        const std::string target = key.text("target");
        const auto time          = key.at("time").number();
        const auto position      = key.optional_vec3f("position");
        const auto degrees       = key.optional_vec3f("rotation_degrees");
        const auto scale         = key.optional_vec3f("scale");
        const auto interp_name   = key.optional_text("interp").value_or(std::string{"linear"});

        const auto number_or = [&key](std::string_view field, float32 fallback) -> float32 {
            if (!key.has(field) || key.is_null(field)) {
                return fallback;
            }
            const auto read = key.at(field).number();
            if (!read) {
                key.fail(json::describe(read.error()));
                return fallback;
            }
            return static_cast<float32>(*read);
        };
        const float32 tangent_in  = number_or("tangent_in", 0.0F);
        const float32 tangent_out = number_or("tangent_out", 1.0F);

        if (!time) {
            key.fail(json::describe(time.error()));
        }
        if (key.failed()) {
            return tool_failure(key.error());
        }

        const auto interp = interp_of(interp_name);
        if (!interp) {
            return tool_failure(std::format(
                "{}: expected linear, step, ease_in, ease_out, ease_in_out or cubic_bezier, found "
                "'{}'",
                entry["interp"].path(), interp_name
            ));
        }
        if (*time < 0.0) {
            return tool_failure(std::format("{}: must not be negative", entry["time"].path()));
        }
        if (!position && !degrees && !scale) {
            return tool_failure(std::format(
                "{}: give at least one of position, rotation_degrees and scale", entry.path()
            ));
        }

        asset::animation_track* track = find_track(tracks, target);
        if (track == nullptr) {
            track = &tracks.emplace_back(target);
        }

        asset::put_key(
            *track,
            asset::pose_key{
                .time        = static_cast<float32>(*time),
                .position    = position,
                .rotation    = degrees ? std::optional{rotation_of(*degrees)} : std::nullopt,
                .scale       = scale,
                .interp      = *interp,
                .tangent_in  = tangent_in,
                .tangent_out = tangent_out,
            }
        );
    }

    return answer_with_clip(
        bindings, bindings.clips->set_tracks(clip_name, std::move(tracks)), clip_name
    );
}

[[nodiscard]] auto remove_keys(const editor_bindings& bindings, const json::value& arguments)
    -> tool_outcome {
    argument_reader in{arguments};
    in.allow({"clip", "target", "property", "from", "to"});
    const std::string clip_name = clip_name_of(bindings, in);
    const std::string target    = in.text("target");
    const auto property_name    = in.optional_text("property");

    const auto time_or = [&in](std::string_view field, float32 fallback) -> float32 {
        if (!in.has(field) || in.is_null(field)) {
            return fallback;
        }
        const auto read = in.at(field).number();
        if (!read) {
            in.fail(json::describe(read.error()));
            return fallback;
        }
        return static_cast<float32>(*read);
    };
    const float32 from = time_or("from", 0.0F);
    const float32 to   = time_or("to", end_of_time);
    if (in.failed()) {
        return tool_failure(in.error());
    }

    std::optional<asset::animation_property> property;
    if (property_name) {
        property = property_of(*property_name);
        if (!property) {
            return tool_failure(std::format(
                "arguments.property: expected position, rotation or scale, found '{}'",
                *property_name
            ));
        }
    }
    if (from > to) {
        return tool_failure("arguments.from must not exceed arguments.to");
    }

    const auto clip = bindings.clips->find(clip_name);
    if (!clip) {
        return tool_failure(clip.error());
    }

    std::vector<asset::animation_track> tracks = (*clip)->get_tracks();

    asset::animation_track* track = find_track(tracks, target);
    if (track == nullptr) {
        std::string listed;
        for (const asset::animation_track& present : tracks) {
            listed += listed.empty() ? present.get_target_name()
                                     : std::format(", {}", present.get_target_name());
        }
        return tool_failure(std::format(
            "the clip '{}' has no track for '{}'; its tracks are: {}", clip_name, target,
            listed.empty() ? std::string{"none"} : listed
        ));
    }

    const uint32 dropped = asset::drop_keys(*track, property, from, to);
    if (dropped == 0) {
        return tool_failure(std::format(
            "the track '{}' has no keys in that span; nothing was removed", target
        ));
    }
    if (asset::count_keys(*track) == 0) {
        std::erase_if(tracks, [&target](const asset::animation_track& present) {
            return present.get_target_name() == target;
        });
    }

    const auto applied = bindings.clips->set_tracks(clip_name, std::move(tracks));
    if (!applied) {
        return tool_failure(applied.error());
    }

    json::object described = describe_clip(bindings, **clip, false);
    described.set("keys_removed", dropped);
    return tool_success(described);
}

}  // namespace

auto append_clip_tools(std::vector<tool>& tools, const editor_bindings& bindings) -> void {
    tools.push_back(tool{
        .name = "clip_create",
        .description =
            "Make a new, empty animation clip for the open prefab and open it for editing. The "
            "clip takes the rig of the prefab. It is written to disk by clip_save.",
        .input_schema = create_schema,
        .run          = when_idle(
            bindings,
            [bindings](const json::value& arguments) -> tool_outcome {
                argument_reader in{arguments};
                in.allow({"name", "overwrite"});
                const std::string name = in.text("name");
                const bool overwrite   = in.flag_or("overwrite", false);
                if (in.failed()) {
                    return tool_failure(in.error());
                }
                return answer_with_clip(bindings, bindings.clips->create(name, overwrite), name);
            }
        ),
    });

    tools.push_back(tool{
        .name = "clip_open",
        .description =
            "Open an animation clip from the animations folder on the open prefab and make it "
            "the clip being edited. A clip that is already open is only selected, its unsaved "
            "changes stay.",
        .input_schema = open_schema,
        .run          = when_idle(
            bindings,
            [bindings](const json::value& arguments) -> tool_outcome {
                argument_reader in{arguments};
                in.allow({"name", "ignore_rig"});
                const std::string name = in.text("name");
                const bool ignore_rig  = in.flag_or("ignore_rig", false);
                if (in.failed()) {
                    return tool_failure(in.error());
                }
                return answer_with_clip(bindings, bindings.clips->open(name, ignore_rig), name);
            }
        ),
    });

    tools.push_back(tool{
        .name = "clip_get",
        .description =
            "Read an open clip: its rig, its length in seconds and, for every animated target, "
            "its keys in the order of time. A key carries any of position, rotation_degrees and "
            "scale, the node's transform relative to its parent at that time.",
        .input_schema = clip_only_schema,
        .run =
            [bindings](const json::value& arguments) -> tool_outcome {
                argument_reader in{arguments};
                in.allow({"clip"});
                const std::string clip_name = clip_name_of(bindings, in);
                if (in.failed()) {
                    return tool_failure(in.error());
                }

                const auto clip = bindings.clips->find(clip_name);
                if (!clip) {
                    return tool_failure(clip.error());
                }
                return tool_success(describe_clip(bindings, **clip, true));
            },
    });

    tools.push_back(tool{
        .name = "clip_set_keys",
        .description =
            "Put keys into an open clip as one undo step. A track is made for a target that has "
            "none; the target must be an anim_target of a node. The clip lasts until its last "
            "key. Opens the clip in the editor.",
        .input_schema = set_keys_schema,
        .run          = when_idle(
            bindings,
            [bindings](const json::value& arguments) -> tool_outcome {
                return set_keys(bindings, arguments);
            }
        ),
    });

    tools.push_back(tool{
        .name = "clip_remove_keys",
        .description =
            "Remove the keys of one target within a span of time as one undo step, for one "
            "property or for all three. With no span, every key goes and the track with them.",
        .input_schema = remove_keys_schema,
        .run          = when_idle(
            bindings,
            [bindings](const json::value& arguments) -> tool_outcome {
                return remove_keys(bindings, arguments);
            }
        ),
    });

    tools.push_back(tool{
        .name = "clip_pose_at",
        .description =
            "Put the prefab into the pose the clip gives at a time, so that view_screenshot "
            "shows it. The pose is a preview: leaving the clip restores the rest pose.",
        .input_schema = pose_schema,
        .run          = when_idle(
            bindings,
            [bindings](const json::value& arguments) -> tool_outcome {
                argument_reader in{arguments};
                in.allow({"clip", "time"});
                const std::string clip_name = clip_name_of(bindings, in);
                const auto time             = in.at("time").number();
                if (in.failed() || !time) {
                    return tool_failure(in.failed() ? in.error() : json::describe(time.error()));
                }

                const auto posed =
                    bindings.clips->show_pose(clip_name, static_cast<float32>(*time));
                if (!posed) {
                    return tool_failure(posed.error());
                }

                const auto clip = bindings.clips->find(clip_name);
                json::object described = describe_clip(bindings, **clip, false);
                described.set("posed_at", json_of(static_cast<float32>(*time)));
                return tool_success(described);
            }
        ),
    });

    tools.push_back(tool{
        .name         = "clip_save",
        .description  = "Write an open clip to its file in the animations folder.",
        .input_schema = clip_only_schema,
        .run          = when_idle(
            bindings,
            [bindings](const json::value& arguments) -> tool_outcome {
                argument_reader in{arguments};
                in.allow({"clip"});
                const std::string clip_name = clip_name_of(bindings, in);
                if (in.failed()) {
                    return tool_failure(in.error());
                }
                return answer_with_clip(bindings, bindings.clips->save(clip_name), clip_name);
            }
        ),
    });

    tools.push_back(tool{
        .name = "clip_close",
        .description =
            "Close an open clip. When no clip is left open the editor returns to the prefab and "
            "its rest pose.",
        .input_schema = close_schema,
        .run          = when_idle(
            bindings,
            [bindings](const json::value& arguments) -> tool_outcome {
                argument_reader in{arguments};
                in.allow({"clip", "discard_unsaved"});
                const std::string clip_name = clip_name_of(bindings, in);
                const bool discard          = in.flag_or("discard_unsaved", false);
                if (in.failed()) {
                    return tool_failure(in.error());
                }

                const auto closed = bindings.clips->close(clip_name, discard);
                if (!closed) {
                    return tool_failure(closed.error());
                }

                json::array still_open;
                for (std::string& name : bindings.clips->open_clips()) {
                    still_open.emplace_back(std::move(name));
                }
                return tool_success(json::object{
                    {"closed", clip_name},
                    {"open_clips", std::move(still_open)},
                });
            }
        ),
    });
}

}  // namespace vw::sculptor::mcp
