module vw.sculptor;

import std;

import vw.core;
import vw.asset;
import vw.ecs;
import vw.world;
import vw.gfx;

namespace vw::sculptor::mcp {

namespace {

constexpr uint32 default_shot_side = 1024;
constexpr uint32 smallest_shot_side = 64;
constexpr uint32 largest_shot_side  = 2048;

constexpr uint32 settle_ticks       = 3;
constexpr uint32 longest_mesh_wait  = 240;
constexpr uint32 longest_frame_wait = 120;

constexpr std::string_view minimised_reason =
    "the editor window is minimised and draws nothing; restore it to take a screenshot";

constexpr std::string_view set_schema = R"({
    "type": "object",
    "properties": {
        "node": {"type": "string", "description": "The node to look at, with everything under it. Defaults to the whole prefab."},
        "from": {"enum": ["iso", "+x", "-x", "+y", "-y", "+z", "-z"], "description": "The side the camera stands on, looking back at the target: '+y' is from above, 'iso' is from +x +y +z. Default 'iso'."},
        "yaw_degrees": {"type": "number", "description": "Heading of the view direction, overrides 'from': 0 looks along +z, 90 along +x."},
        "pitch_degrees": {"type": "number", "description": "Tilt of the view direction, overrides 'from': negative looks down."},
        "projection": {"enum": ["perspective", "orthographic"], "description": "How the view is projected; stays as it is when omitted. 'orthographic' draws without foreshortening: with a named side every voxel is a square of the same size, which is the view to compare with the layer text of volume_get."},
        "distance": {"type": "number", "description": "Perspective only: distance from the target in voxels. By default the target fills the view."},
        "height": {"type": "number", "description": "Orthographic only: how many voxels the picture spans from top to bottom. By default the target fills the view."}
    },
    "additionalProperties": false
})";

constexpr std::string_view screenshot_schema = R"({
    "type": "object",
    "properties": {
        "max_size": {"type": "integer", "minimum": 64, "maximum": 2048, "description": "Longest side of the picture in pixels. Default 1024."},
        "overlays": {"type": "boolean", "description": "Draw the editor's overlays: axes, gizmo, volume box. Default false."},
        "interface": {"type": "boolean", "description": "Draw the editor's panels and menus. Default false."}
    },
    "additionalProperties": false
})";

constexpr std::array<std::pair<std::string_view, view_side>, 7> side_names{{
    {"iso", view_side::iso},
    {"+x", view_side::plus_x},
    {"-x", view_side::minus_x},
    {"+y", view_side::plus_y},
    {"-y", view_side::minus_y},
    {"+z", view_side::plus_z},
    {"-z", view_side::minus_z},
}};

constexpr std::array<std::pair<std::string_view, gfx::projection_kind>, 2> projection_names{{
    {"perspective", gfx::projection_kind::perspective},
    {"orthographic", gfx::projection_kind::orthographic},
}};

[[nodiscard]] auto name_of(gfx::projection_kind kind) -> std::string_view {
    return kind == gfx::projection_kind::orthographic ? "orthographic" : "perspective";
}

[[nodiscard]] auto describe_camera(const editor_bindings& bindings) -> json::object {
    const auto& camera     = bindings.engine->get_camera();
    const view_report seen = bindings.views->report();

    json::object described{
        {"projection", name_of(seen.projection)},
        {"position", json_of(camera.get_position())},
        {"yaw_degrees", json_of(camera.get_yaw())},
        {"pitch_degrees", json_of(camera.get_pitch())},
    };
    if (camera.is_orthographic()) {
        described.set("voxels_high", json_of(seen.height));
        described.set("voxels_wide", json_of(seen.width));
    }
    return described;
}

[[nodiscard]] auto set_view(const editor_bindings& bindings, const json::value& arguments)
    -> tool_outcome {
    argument_reader in{arguments};
    in.allow({"node", "from", "yaw_degrees", "pitch_degrees", "distance", "projection", "height"});

    const auto node       = in.optional_text("node");
    const auto side       = in.optional_text("from").value_or(std::string{"iso"});
    const auto projection = in.optional_text("projection");

    const auto number = [&in](std::string_view key) -> std::optional<float32> {
        if (!in.has(key) || in.is_null(key)) {
            return std::nullopt;
        }
        const auto read = in.at(key).number();
        if (!read) {
            in.fail(json::describe(read.error()));
            return std::nullopt;
        }
        return static_cast<float32>(*read);
    };

    const auto yaw      = number("yaw_degrees");
    const auto pitch    = number("pitch_degrees");
    const auto distance = number("distance");
    const auto height   = number("height");
    if (in.failed()) {
        return tool_failure(in.error());
    }

    const auto named_side = std::ranges::find(side_names, side, [](const auto& entry) {
        return entry.first;
    });
    if (named_side == side_names.end()) {
        return tool_failure(std::format(
            "arguments.from: expected iso, +x, -x, +y, -y, +z or -z, found '{}'", side
        ));
    }

    view_request request{
        .node       = node,
        .direction  = direction_of(named_side->second),
        .projection = std::nullopt,
        .distance   = distance,
        .height     = height,
    };
    request.direction.yaw_degrees   = yaw.value_or(request.direction.yaw_degrees);
    request.direction.pitch_degrees = pitch.value_or(request.direction.pitch_degrees);

    if (projection) {
        const auto named = std::ranges::find(projection_names, *projection, [](const auto& entry) {
            return entry.first;
        });
        if (named == projection_names.end()) {
            return tool_failure(std::format(
                "arguments.projection: expected perspective or orthographic, found '{}'",
                *projection
            ));
        }
        request.projection = named->second;
    }

    const auto seen = bindings.views->look(request);
    if (!seen) {
        return tool_failure(seen.error());
    }

    json::object described = describe_camera(bindings);
    described.set("looking_at", seen->looking_at);
    described.set("target", json_of(seen->target));
    if (seen->projection == gfx::projection_kind::perspective) {
        described.set("distance", json_of(seen->distance));
    }
    return tool_success(described);
}

constexpr std::string_view hide_schema = R"({
    "type": "object",
    "properties": {
        "nodes": {"type": "array", "items": {"type": "string"}, "description": "Every node to hide, each with the nodes under it. The list replaces the current one; [] shows everything."}
    },
    "required": ["nodes"],
    "additionalProperties": false
})";

constexpr std::string_view preview_schema = R"({
    "type": "object",
    "properties": {
        "node": {"type": "string", "description": "Name of the node that has the socket."},
        "socket": {"type": "string", "description": "Name of the socket."},
        "prefab": {"type": ["string", "null"], "description": "Prefab to show in the socket, a file of the prefabs folder with or without .vox; null takes the preview away."}
    },
    "required": ["node", "socket", "prefab"],
    "additionalProperties": false
})";

struct shot_progress {
    uint32 ticks_waited  = 0;
    uint32 frames_waited = 0;
    bool requested       = false;
};

[[nodiscard]] auto take_screenshot(const editor_bindings& bindings, const json::value& arguments)
    -> tool_outcome {
    argument_reader in{arguments};
    in.allow({"max_size", "overlays", "interface"});

    const auto side      = in.optional_index("max_size").value_or(default_shot_side);
    const bool overlays  = in.flag_or("overlays", false);
    const bool panels    = in.flag_or("interface", false);
    if (in.failed()) {
        return tool_failure(in.error());
    }
    if (side < smallest_shot_side || side > largest_shot_side) {
        return tool_failure(std::format(
            "arguments.max_size: must be {}..{}, got {}", smallest_shot_side, largest_shot_side, side
        ));
    }
    if (!bindings.engine->get_renderer().has_drawable_surface()) {
        return tool_failure(std::string{minimised_reason});
    }

    const auto longest_side = static_cast<uint32>(side);
    auto progress           = std::make_shared<shot_progress>();

    tool_outcome waiting;
    waiting.later = [bindings, progress, longest_side, overlays,
                     panels]() -> std::optional<tool_outcome> {
        auto& renderer = bindings.engine->get_renderer();
        if (!renderer.has_drawable_surface()) {
            return tool_failure(std::string{minimised_reason});
        }

        if (!progress->requested) {
            ++progress->ticks_waited;

            const bool settled =
                progress->ticks_waited > settle_ticks && !renderer.has_pending_meshes();
            if (!settled && progress->ticks_waited < longest_mesh_wait) {
                return std::nullopt;
            }

            const gfx::frame_capture_request request{
                .with_interface = panels, .with_overlays = overlays
            };
            if (!renderer.request_capture(request)) {
                return tool_failure("this display cannot be captured: its surface format is not supported");
            }
            progress->requested = true;
            return std::nullopt;
        }

        const auto frame = renderer.take_capture();
        if (!frame) {
            if (++progress->frames_waited > longest_frame_wait) {
                return tool_failure("the frame was not captured in time; try again");
            }
            return std::nullopt;
        }

        const gfx::image_rgba picture = gfx::shrunk_to_fit(*frame, longest_side);
        auto encoded                  = gfx::encode_png(picture);
        if (!encoded) {
            return tool_failure("the picture could not be encoded");
        }

        json::object described = describe_camera(bindings);
        described.set("width", picture.width);
        described.set("height", picture.height);
        if (bindings.engine->get_camera().is_orthographic()) {
            const float32 voxels_high = bindings.views->report().height;
            described.set(
                "pixels_per_voxel", json_of(static_cast<float32>(picture.height) / voxels_high)
            );
        }

        tool_outcome shot = tool_success(described);
        shot.image = tool_image{.media_type = "image/png", .bytes = std::move(*encoded)};
        return shot;
    };
    return waiting;
}

}  // namespace

auto append_view_tools(std::vector<tool>& tools, const editor_bindings& bindings) -> void {
    tools.push_back(tool{
        .name = "view_set",
        .description =
            "Point the editor's camera at a node or at the whole prefab, from a named side or "
            "by yaw and pitch, framed so that the target fills the view, in perspective or "
            "orthographic projection. y is up and a character faces +z. Seen from '-z' x grows "
            "to the right, as in the layer text of volume_get; seen from '+z', the front, the "
            "picture is mirrored and x = 0 is on the right. The camera is shared with the user "
            "and keeps the projection until it is set again. Use it before view_screenshot.",
        .input_schema = set_schema,
        .run          = when_idle(
            bindings,
            [bindings](const json::value& arguments) -> tool_outcome {
                return set_view(bindings, arguments);
            }
        ),
    });

    tools.push_back(tool{
        .name = "view_hide",
        .description =
            "Hide nodes in the viewport, to look at what they cover. It changes what is drawn, "
            "not the prefab: nothing is saved and there is nothing to undo. The user sees it "
            "too, so show everything again with an empty list when done.",
        .input_schema = hide_schema,
        .run          = when_idle(
            bindings,
            [bindings](const json::value& arguments) -> tool_outcome {
                argument_reader in{arguments};
                in.allow({"nodes"});
                const std::vector<std::string> nodes = in.text_list("nodes");
                if (in.failed()) {
                    return tool_failure(in.error());
                }

                const auto hidden = bindings.views->set_hidden(nodes);
                if (!hidden) {
                    return tool_failure(hidden.error());
                }

                json::array listed;
                for (const std::string& name : bindings.views->hidden()) {
                    listed.emplace_back(name);
                }
                return tool_success(json::object{{"hidden", std::move(listed)}});
            }
        ),
    });

    tools.push_back(tool{
        .name = "socket_preview",
        .description =
            "Show another prefab in a socket of a node, the way the game attaches it: a sword "
            "in a hand, a hat on a head. It is a preview for looking and for placing the "
            "socket, not part of the prefab: nothing is saved and there is nothing to undo. "
            "The preview follows the socket when node_set_components moves it.",
        .input_schema = preview_schema,
        .run          = when_idle(
            bindings,
            [bindings](const json::value& arguments) -> tool_outcome {
                argument_reader in{arguments};
                in.allow({"node", "socket", "prefab"});
                const std::string node   = in.text("node");
                const std::string socket = in.text("socket");
                const auto prefab        = in.optional_text("prefab");
                if (in.failed()) {
                    return tool_failure(in.error());
                }

                const auto done = prefab ? bindings.previews->show(node, socket, *prefab)
                                         : bindings.previews->hide(node, socket);
                if (!done) {
                    return tool_failure(done.error());
                }
                return tool_success(describe_node(bindings, node));
            }
        ),
    });

    tools.push_back(tool{
        .name = "view_screenshot",
        .description =
            "Take a picture of the editor's viewport as it is drawn now, without the panels "
            "unless asked for. Waits for the meshes of the last edits. Needs the window "
            "visible: a minimised editor draws nothing.",
        .input_schema = screenshot_schema,
        .run          = [bindings](const json::value& arguments) -> tool_outcome {
            return take_screenshot(bindings, arguments);
        },
    });
}

}  // namespace vw::sculptor::mcp
