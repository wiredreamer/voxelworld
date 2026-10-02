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

constexpr std::string_view retarget_schema = R"({
    "type": "object",
    "properties": {
        "clip": {"type": "string", "description": "Name of an open clip. Defaults to the clip selected in the editor."},
        "from": {"type": "string", "description": "Target the clip has a track for."},
        "to": {"type": "string", "description": "Animation target of the prefab the track is to drive; the clip must not have a track for it yet."}
    },
    "required": ["from", "to"],
    "additionalProperties": false
})";

constexpr std::string_view filmstrip_schema = R"({
    "type": "object",
    "properties": {
        "clip": {"type": "string", "description": "Name of an open clip. Defaults to the clip selected in the editor."},
        "times": {"type": "array", "items": {"type": "number", "minimum": 0}, "minItems": 1, "maxItems": 12, "description": "Moments to show, in seconds. Default: 'frames' moments spread evenly over the clip."},
        "frames": {"type": "integer", "minimum": 2, "maximum": 12, "description": "How many evenly spread moments to show when 'times' is not given. Default 6."},
        "max_size": {"type": "integer", "minimum": 64, "maximum": 1024, "description": "Longest side of one frame in pixels. Default 320."}
    },
    "additionalProperties": false
})";

constexpr uint32 default_strip_frames = 6;
constexpr uint32 most_strip_frames    = 12;
constexpr uint32 default_frame_side   = 320;
constexpr uint32 smallest_frame_side  = 64;
constexpr uint32 largest_frame_side   = 1024;
constexpr uint32 frames_in_a_row      = 4;
constexpr uint32 pose_settle_ticks    = 3;
constexpr uint32 longest_frame_wait   = 120;

constexpr std::string_view minimised_reason =
    "the editor window is minimised and draws nothing; restore it to take pictures";

struct strip_progress {
    std::string clip_name;
    std::vector<float32> times;
    std::vector<gfx::image_rgba> frames;
    uint32 frame_side    = default_frame_side;
    uint32 ticks_waited  = 0;
    uint32 frames_waited = 0;
    bool posed           = false;
    bool requested       = false;
};

constexpr std::string_view play_schema = R"({
    "type": "object",
    "properties": {
        "clip": {"type": "string", "description": "Name of an open clip. Defaults to the clip selected in the editor."},
        "from": {"type": "number", "minimum": 0, "description": "Seconds from the start of the clip to play from. Default 0."},
        "loop": {"enum": ["once", "loop", "ping_pong"], "description": "How the clip repeats. Kept for the clip until the editor closes; 'once' unless set before."},
        "speed": {"type": "number", "exclusiveMinimum": 0, "description": "Playback speed, 1 is real time. Kept for the clip until the editor closes."}
    },
    "additionalProperties": false
})";

constexpr std::array<std::pair<std::string_view, asset::animation_loop_mode>, 3> loop_names{{
    {"once", asset::animation_loop_mode::once},
    {"loop", asset::animation_loop_mode::loop},
    {"ping_pong", asset::animation_loop_mode::ping_pong},
}};

constexpr std::array<std::pair<std::string_view, asset::animation_state>, 3> playback_names{{
    {"stopped", asset::animation_state::stopped},
    {"playing", asset::animation_state::playing},
    {"paused", asset::animation_state::paused},
}};

template <typename Value, std::size_t Count>
[[nodiscard]] auto name_in(
    const std::array<std::pair<std::string_view, Value>, Count>& names, Value value
) -> std::string_view {
    const auto found = std::ranges::find(names, value, [](const auto& entry) {
        return entry.second;
    });
    return found == names.end() ? std::string_view{} : found->first;
}

[[nodiscard]] auto describe_playback(const editor_bindings& bindings, std::string_view clip_name)
    -> json::object {
    const clip_playback_status status = bindings.clips->playback(clip_name);
    return json::object{
        {"state", name_in(playback_names, status.state)},
        {"time", json_of(status.time)},
        {"loop", name_in(loop_names, status.loop)},
        {"speed", json_of(status.speed)},
    };
}

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
        {"playback", describe_playback(bindings, clip.get_name())},
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
        .name = "clip_retarget",
        .description =
            "Make the track of one target drive another target, keys untouched, as one undo "
            "step. Use it after an animation target was renamed: the clips are not followed "
            "automatically, each open clip is retargeted and saved on its own.",
        .input_schema = retarget_schema,
        .run          = when_idle(
            bindings,
            [bindings](const json::value& arguments) -> tool_outcome {
                argument_reader in{arguments};
                in.allow({"clip", "from", "to"});
                const std::string clip_name = clip_name_of(bindings, in);
                const std::string from      = in.text("from");
                const std::string to        = in.text("to");
                if (in.failed()) {
                    return tool_failure(in.error());
                }

                const auto moved = bindings.clips->retarget(clip_name, from, to);
                if (!moved) {
                    return tool_failure(moved.error());
                }

                const auto clip = bindings.clips->find(clip_name);
                return tool_success(describe_clip(bindings, **clip, true));
            }
        ),
    });

    tools.push_back(tool{
        .name = "clip_filmstrip",
        .description =
            "Take pictures of the prefab at several moments of an open clip and return them as "
            "one image, moments left to right and top to bottom, four to a row. One call "
            "instead of clip_pose_at and view_screenshot for each moment. The view is the one "
            "view_set left; the clip stays posed at the last moment. Needs the window visible.",
        .input_schema = filmstrip_schema,
        .run          = when_idle(
            bindings,
            [bindings](const json::value& arguments) -> tool_outcome {
                argument_reader in{arguments};
                in.allow({"clip", "times", "frames", "max_size"});
                const std::string clip_name = clip_name_of(bindings, in);
                const auto frame_count =
                    in.optional_index("frames").value_or(default_strip_frames);
                const auto side = in.optional_index("max_size").value_or(default_frame_side);

                std::vector<float32> times;
                if (in.has("times")) {
                    const auto given = in.at("times").elements();
                    if (!given) {
                        in.fail(json::describe(given.error()));
                    } else {
                        for (const json::cursor& entry : *given) {
                            const auto time = entry.number();
                            if (!time) {
                                in.fail(json::describe(time.error()));
                                break;
                            }
                            times.push_back(static_cast<float32>(*time));
                        }
                    }
                }
                if (in.failed()) {
                    return tool_failure(in.error());
                }

                const auto clip = bindings.clips->find(clip_name);
                if (!clip) {
                    return tool_failure(clip.error());
                }
                if (side < smallest_frame_side || side > largest_frame_side) {
                    return tool_failure(std::format(
                        "arguments.max_size: must be {}..{}, got {}", smallest_frame_side,
                        largest_frame_side, side
                    ));
                }

                if (times.empty()) {
                    if (frame_count < 2 || frame_count > most_strip_frames) {
                        return tool_failure(std::format(
                            "arguments.frames: must be 2..{}, got {}", most_strip_frames, frame_count
                        ));
                    }
                    const float32 duration = (*clip)->get_duration();
                    for (std::size_t index = 0; index < frame_count; ++index) {
                        times.push_back(
                            duration * static_cast<float32>(index) /
                            static_cast<float32>(frame_count - 1)
                        );
                    }
                }
                if (times.size() > most_strip_frames) {
                    return tool_failure(std::format(
                        "arguments.times: at most {} moments, got {}", most_strip_frames,
                        times.size()
                    ));
                }
                if (!bindings.engine->get_renderer().has_drawable_surface()) {
                    return tool_failure(std::string{minimised_reason});
                }

                auto progress        = std::make_shared<strip_progress>();
                progress->clip_name  = (*clip)->get_name();
                progress->times      = std::move(times);
                progress->frame_side = static_cast<uint32>(side);

                tool_outcome waiting;
                waiting.later = [bindings, progress]() -> std::optional<tool_outcome> {
                    auto& renderer = bindings.engine->get_renderer();
                    if (!renderer.has_drawable_surface()) {
                        return tool_failure(std::string{minimised_reason});
                    }

                    const std::size_t at = progress->frames.size();

                    if (!progress->posed) {
                        const auto posed =
                            bindings.clips->show_pose(progress->clip_name, progress->times[at]);
                        if (!posed) {
                            return tool_failure(posed.error());
                        }
                        progress->posed        = true;
                        progress->ticks_waited = 0;
                        return std::nullopt;
                    }

                    if (!progress->requested) {
                        if (++progress->ticks_waited <= pose_settle_ticks) {
                            return std::nullopt;
                        }
                        if (!renderer.request_capture(gfx::frame_capture_request{
                                .with_interface = false, .with_overlays = false
                            })) {
                            return tool_failure(
                                "this display cannot be captured: its surface format is not "
                                "supported"
                            );
                        }
                        progress->requested     = true;
                        progress->frames_waited = 0;
                        return std::nullopt;
                    }

                    const auto frame = renderer.take_capture();
                    if (!frame) {
                        if (++progress->frames_waited > longest_frame_wait) {
                            return tool_failure("a frame was not captured in time; try again");
                        }
                        return std::nullopt;
                    }

                    progress->frames.push_back(gfx::shrunk_to_fit(*frame, progress->frame_side));
                    progress->posed     = false;
                    progress->requested = false;

                    if (progress->frames.size() < progress->times.size()) {
                        return std::nullopt;
                    }

                    const gfx::image_rgba sheet = gfx::tiled(progress->frames, frames_in_a_row);
                    auto encoded                = gfx::encode_png(sheet);
                    if (!encoded) {
                        return tool_failure("the picture could not be encoded");
                    }

                    json::array moments;
                    for (const float32 time : progress->times) {
                        moments.emplace_back(json_of(time));
                    }

                    tool_outcome strip = tool_success(json::object{
                        {"clip", progress->clip_name},
                        {"times", std::move(moments)},
                        {"columns", std::min<std::size_t>(frames_in_a_row, progress->frames.size())},
                        {"frame_width", progress->frames.front().width},
                        {"frame_height", progress->frames.front().height},
                        {"width", sheet.width},
                        {"height", sheet.height},
                    });
                    strip.image =
                        tool_image{.media_type = "image/png", .bytes = std::move(*encoded)};
                    return strip;
                };
                return waiting;
            }
        ),
    });

    tools.push_back(tool{
        .name = "clip_play",
        .description =
            "Play an open clip in the editor so that the user can watch it; the answer comes at "
            "once and the clip keeps playing. 'loop' and 'speed' stay with the clip for the rest "
            "of the session. To look at a moment yourself use clip_pose_at and view_screenshot: "
            "a screenshot of a playing clip shows whatever moment it happens to catch.",
        .input_schema = play_schema,
        .run          = when_idle(
            bindings,
            [bindings](const json::value& arguments) -> tool_outcome {
                argument_reader in{arguments};
                in.allow({"clip", "from", "loop", "speed"});
                const std::string clip_name = clip_name_of(bindings, in);

                clip_playback how;
                if (in.has("from")) {
                    const auto from = in.at("from").number();
                    if (!from) {
                        in.fail(json::describe(from.error()));
                    } else {
                        how.from = static_cast<float32>(*from);
                    }
                }
                if (in.has("speed")) {
                    const auto speed = in.at("speed").number();
                    if (!speed) {
                        in.fail(json::describe(speed.error()));
                    } else {
                        how.speed = static_cast<float32>(*speed);
                    }
                }
                if (const auto loop = in.optional_text("loop")) {
                    const auto found = std::ranges::find(loop_names, *loop, [](const auto& entry) {
                        return entry.first;
                    });
                    if (found == loop_names.end()) {
                        in.fail(std::format(
                            "loop: '{}' is not a loop mode; one of once, loop, ping_pong", *loop
                        ));
                    } else {
                        how.loop = found->second;
                    }
                }
                if (in.failed()) {
                    return tool_failure(in.error());
                }

                return answer_with_clip(bindings, bindings.clips->play(clip_name, how), clip_name);
            }
        ),
    });

    tools.push_back(tool{
        .name = "clip_stop",
        .description =
            "Stop a playing clip and put the prefab into the pose the clip gives at its start.",
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
                return answer_with_clip(bindings, bindings.clips->stop(clip_name), clip_name);
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
