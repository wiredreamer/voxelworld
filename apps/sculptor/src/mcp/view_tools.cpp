module vw.sculptor;

import std;

import vw.core;
import vw.asset;
import vw.ecs;
import vw.world;
import vw.gfx;

namespace vw::sculptor {

namespace {

constexpr uint32 default_shot_side = 1024;
constexpr uint32 smallest_shot_side = 64;
constexpr uint32 largest_shot_side  = 2048;

constexpr uint32 settle_ticks       = 3;
constexpr uint32 longest_mesh_wait  = 240;
constexpr uint32 longest_frame_wait = 120;

constexpr float32 fit_margin        = 1.05F;
constexpr float32 steepest_pitch    = 89.0F;
constexpr float32 smallest_distance = 2.0F;

constexpr std::string_view minimised_reason =
    "the editor window is minimised and draws nothing; restore it to take a screenshot";

constexpr std::string_view set_schema = R"({
    "type": "object",
    "properties": {
        "node": {"type": "string", "description": "The node to look at, with everything under it. Defaults to the whole prefab."},
        "from": {"enum": ["iso", "+x", "-x", "+y", "-y", "+z", "-z"], "description": "The side the camera stands on, looking back at the target: '+y' is from above, 'iso' is from +x +y +z. Default 'iso'."},
        "yaw_degrees": {"type": "number", "description": "Heading of the view direction, overrides 'from': 0 looks along +z, 90 along +x."},
        "pitch_degrees": {"type": "number", "description": "Tilt of the view direction, overrides 'from': negative looks down."},
        "distance": {"type": "number", "description": "Distance from the target in voxels. By default the target fills the view."}
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

struct world_box {
    vec3f low{
        std::numeric_limits<float32>::max(), std::numeric_limits<float32>::max(),
        std::numeric_limits<float32>::max()
    };
    vec3f high{
        std::numeric_limits<float32>::lowest(), std::numeric_limits<float32>::lowest(),
        std::numeric_limits<float32>::lowest()
    };
    bool filled = false;

    auto include(const vec3f& point) -> void {
        low    = vec3f{std::min(low.x, point.x), std::min(low.y, point.y), std::min(low.z, point.z)};
        high   = vec3f{std::max(high.x, point.x), std::max(high.y, point.y), std::max(high.z, point.z)};
        filled = true;
    }

    [[nodiscard]] auto centre() const -> vec3f {
        return vec3f{(low.x + high.x) / 2.0F, (low.y + high.y) / 2.0F, (low.z + high.z) / 2.0F};
    }

    [[nodiscard]] auto radius() const -> float32 {
        return math::length(vec3f{high.x - low.x, high.y - low.y, high.z - low.z}) / 2.0F;
    }
};

auto include_volume(ecs::world& world, ecs::entity ent, world_box& box) -> void {
    if (!world.has<ecs::model_component>(ent) || !world.has<ecs::transform_component>(ent)) {
        return;
    }

    const auto& model_comp = world.get<ecs::model_component>(ent);
    if (!model_comp.has_model() || !model_comp.is_visible()) {
        return;
    }

    const asset::model& model = *model_comp.get_model();
    const vec3i size          = model.size();

    const asset::voxel_bounds whole{.min = {}, .max = {size.x - 1, size.y - 1, size.z - 1}};
    const asset::voxel_bounds solid = asset::occupied_bounds(model).value_or(whole);

    const mat4f placement =
        ecs::model_matrix(world.get<ecs::transform_component>(ent), model_comp);

    for (const int32 x : {solid.min.x, solid.max.x + 1}) {
        for (const int32 y : {solid.min.y, solid.max.y + 1}) {
            for (const int32 z : {solid.min.z, solid.max.z + 1}) {
                box.include(
                    placement *
                    vec3f{static_cast<float32>(x), static_cast<float32>(y), static_cast<float32>(z)}
                );
            }
        }
    }
}

[[nodiscard]] auto box_of_subtree(ecs::world& world, ecs::entity top) -> world_box {
    world_box box;

    std::vector<ecs::entity> found{top};
    for (std::size_t at = 0; at < found.size(); ++at) {
        const ecs::entity ent = found[at];
        include_volume(world, ent, box);

        if (world.has<ecs::hierarchy_component>(ent)) {
            const std::vector<ecs::entity> children =
                world.get<ecs::hierarchy_component>(ent).get_children();
            found.insert(found.end(), children.begin(), children.end());
        }
    }

    if (!box.filled) {
        const vec3f origin = world.get<ecs::transform_component>(top).get_world_matrix() * vec3f{};
        box.include(vec3f{origin.x - 1.0F, origin.y - 1.0F, origin.z - 1.0F});
        box.include(vec3f{origin.x + 1.0F, origin.y + 1.0F, origin.z + 1.0F});
    }
    return box;
}

struct view_direction {
    float32 yaw_degrees   = 0.0F;
    float32 pitch_degrees = 0.0F;
};

[[nodiscard]] auto direction_from(std::string_view side) -> std::optional<view_direction> {
    if (side == "iso") {
        return view_direction{.yaw_degrees = -135.0F, .pitch_degrees = -30.0F};
    }
    if (side == "+x") {
        return view_direction{.yaw_degrees = -90.0F, .pitch_degrees = 0.0F};
    }
    if (side == "-x") {
        return view_direction{.yaw_degrees = 90.0F, .pitch_degrees = 0.0F};
    }
    if (side == "+z") {
        return view_direction{.yaw_degrees = 180.0F, .pitch_degrees = 0.0F};
    }
    if (side == "-z") {
        return view_direction{.yaw_degrees = 0.0F, .pitch_degrees = 0.0F};
    }
    if (side == "+y") {
        return view_direction{.yaw_degrees = 180.0F, .pitch_degrees = -steepest_pitch};
    }
    if (side == "-y") {
        return view_direction{.yaw_degrees = 180.0F, .pitch_degrees = steepest_pitch};
    }
    return std::nullopt;
}

[[nodiscard]] auto describe_camera(const gfx::camera& camera) -> json::object {
    return json::object{
        {"position", json_of(camera.get_position())},
        {"yaw_degrees", json_of(camera.get_yaw())},
        {"pitch_degrees", json_of(camera.get_pitch())},
    };
}

[[nodiscard]] auto set_view(const mcp_bindings& bindings, const json::value& arguments)
    -> tool_outcome {
    argument_reader in{arguments};
    in.allow({"node", "from", "yaw_degrees", "pitch_degrees", "distance"});

    const auto node = in.optional_text("node");
    const auto side = in.optional_text("from").value_or(std::string{"iso"});

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
    if (in.failed()) {
        return tool_failure(in.error());
    }

    auto direction = direction_from(side);
    if (!direction) {
        return tool_failure(std::format(
            "arguments.from: expected iso, +x, -x, +y, -y, +z or -z, found '{}'", side
        ));
    }
    if (distance && *distance <= 0.0F) {
        return tool_failure("arguments.distance: must be above zero");
    }

    const auto& scene       = bindings.state->scene;
    const std::string focus = node.value_or(scene.root_name);
    const auto found        = scene.name_to_entity.find(focus);
    if (found == scene.name_to_entity.end()) {
        if (!node) {
            return tool_failure("there is nothing to look at: no prefab with nodes is open");
        }
        std::string listed;
        for (const std::string& name : list_node_names(*bindings.state)) {
            listed += listed.empty() ? name : std::format(", {}", name);
        }
        return tool_failure(std::format("there is no node '{}'; the nodes are: {}", focus, listed));
    }

    direction->yaw_degrees   = yaw.value_or(direction->yaw_degrees);
    direction->pitch_degrees =
        std::clamp(pitch.value_or(direction->pitch_degrees), -steepest_pitch, steepest_pitch);

    auto& camera = bindings.engine->get_camera();
    const world_box box = box_of_subtree(bindings.engine->get_world(), found->second);

    const float32 half_view = math::radians(camera.get_fov()) / 2.0F;
    const float32 fitted    = (box.radius() / std::sin(half_view)) * fit_margin;
    const float32 away      = distance.value_or(std::max(fitted, smallest_distance));

    camera.set_rotation(direction->pitch_degrees, direction->yaw_degrees);

    const vec3f forward = camera.get_forward();
    const vec3f centre  = box.centre();
    camera.set_position(
        vec3f{centre.x - forward.x * away, centre.y - forward.y * away, centre.z - forward.z * away}
    );

    json::object described = describe_camera(camera);
    described.set("looking_at", focus);
    described.set("target", json_of(centre));
    described.set("distance", json_of(away));
    return tool_success(described);
}

struct shot_progress {
    uint32 ticks_waited  = 0;
    uint32 frames_waited = 0;
    bool requested       = false;
};

[[nodiscard]] auto take_screenshot(const mcp_bindings& bindings, const json::value& arguments)
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

        json::object described = describe_camera(bindings.engine->get_camera());
        described.set("width", picture.width);
        described.set("height", picture.height);

        tool_outcome shot = tool_success(described);
        shot.image = tool_image{.media_type = "image/png", .bytes = std::move(*encoded)};
        return shot;
    };
    return waiting;
}

}  // namespace

auto append_view_tools(std::vector<mcp_tool>& tools, const mcp_bindings& bindings) -> void {
    tools.push_back(mcp_tool{
        .name = "view_set",
        .description =
            "Point the editor's camera at a node or at the whole prefab, from a named side or "
            "by yaw and pitch, far enough for the target to fill the view. x is right, y is up. "
            "Use it before view_screenshot to look at the model from the side that matters.",
        .input_schema = set_schema,
        .run          = when_idle(
            bindings,
            [bindings](const json::value& arguments) -> tool_outcome {
                return set_view(bindings, arguments);
            }
        ),
    });

    tools.push_back(mcp_tool{
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

}  // namespace vw::sculptor
