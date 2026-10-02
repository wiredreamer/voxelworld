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

constexpr float32 fit_margin        = 1.05F;
constexpr float32 steepest_pitch    = 90.0F;
constexpr float32 smallest_distance = 2.0F;
constexpr float32 smallest_height   = 1.0F;
constexpr float32 unframed_depth    = 50.0F;
constexpr float32 clear_of_target   = 1.0F;

struct world_box {
    vec3f low{
        std::numeric_limits<float32>::max(),
        std::numeric_limits<float32>::max(),
        std::numeric_limits<float32>::max()
    };
    vec3f high{
        std::numeric_limits<float32>::lowest(),
        std::numeric_limits<float32>::lowest(),
        std::numeric_limits<float32>::lowest()
    };
    bool filled = false;

    auto include(
        const vec3f& point
    ) -> void {
        low = vec3f{std::min(low.x, point.x), std::min(low.y, point.y), std::min(low.z, point.z)};
        high =
            vec3f{std::max(high.x, point.x), std::max(high.y, point.y), std::max(high.z, point.z)};
        filled = true;
    }

    [[nodiscard]] auto centre() const -> vec3f {
        return vec3f{(low.x + high.x) / 2.0F, (low.y + high.y) / 2.0F, (low.z + high.z) / 2.0F};
    }

    [[nodiscard]] auto radius() const -> float32 {
        return math::length(vec3f{high.x - low.x, high.y - low.y, high.z - low.z}) / 2.0F;
    }

    [[nodiscard]] auto half_extent_along(
        const vec3f& axis
    ) const -> float32 {
        const vec3f half{(high.x - low.x) / 2.0F, (high.y - low.y) / 2.0F, (high.z - low.z) / 2.0F};
        return (std::abs(axis.x) * half.x) + (std::abs(axis.y) * half.y) +
            (std::abs(axis.z) * half.z);
    }
};

auto include_volume(
    ecs::world& world, ecs::entity ent, world_box& box
) -> void {
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

    const mat4f placement = ecs::model_matrix(world.get<ecs::transform_component>(ent), model_comp);

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

[[nodiscard]] auto box_of_subtree(
    ecs::world& world, ecs::entity top
) -> world_box {
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

[[nodiscard]] auto refuse(
    std::string message
) -> std::unexpected<std::string> {
    return std::unexpected(std::move(message));
}

}  // namespace

auto direction_of(
    view_side side
) -> view_direction {
    switch (side) {
        case view_side::iso:
            return view_direction{.yaw_degrees = -135.0F, .pitch_degrees = -30.0F};
        case view_side::plus_x:
            return view_direction{.yaw_degrees = -90.0F, .pitch_degrees = 0.0F};
        case view_side::minus_x:
            return view_direction{.yaw_degrees = 90.0F, .pitch_degrees = 0.0F};
        case view_side::plus_z:
            return view_direction{.yaw_degrees = 180.0F, .pitch_degrees = 0.0F};
        case view_side::minus_z:
            return view_direction{.yaw_degrees = 0.0F, .pitch_degrees = 0.0F};
        case view_side::plus_y:
            return view_direction{.yaw_degrees = 180.0F, .pitch_degrees = -steepest_pitch};
        case view_side::minus_y:
            return view_direction{.yaw_degrees = 180.0F, .pitch_degrees = steepest_pitch};
    }
    return view_direction{};
}

view_service::view_service(
    engine_type& eng, app_state& state
)
    : engine_(&eng), state_(&state) {}

auto view_service::look(
    const view_request& request
) -> std::expected<view_report, std::string> {
    auto& camera = engine_->get_camera();

    const gfx::projection_kind projection =
        request.projection.value_or(camera.get_projection_kind());
    const bool flat = projection == gfx::projection_kind::orthographic;

    if (request.distance && *request.distance <= 0.0F) {
        return refuse("distance: must be above zero");
    }
    if (request.height && *request.height <= 0.0F) {
        return refuse("height: must be above zero");
    }
    if (flat && request.distance) {
        return refuse(
            "distance: an orthographic view has no distance; give 'height', the voxels the "
            "picture spans from top to bottom"
        );
    }
    if (!flat && request.height) {
        return refuse(
            "height: a perspective view is framed by 'distance'; 'height' is for projection "
            "'orthographic'"
        );
    }

    const auto& scene       = state_->scene;
    const std::string focus = request.node.value_or(scene.root_name);
    const auto found        = scene.name_to_entity.find(focus);
    if (found == scene.name_to_entity.end()) {
        if (!request.node) {
            return refuse("there is nothing to look at: no prefab with nodes is open");
        }
        std::string listed;
        for (const std::string& name : list_node_names(*state_)) {
            listed += listed.empty() ? name : std::format(", {}", name);
        }
        return refuse(std::format("there is no node '{}'; the nodes are: {}", focus, listed));
    }

    const world_box box = box_of_subtree(engine_->get_world(), found->second);
    const vec3f centre  = box.centre();

    camera.set_rotation(
        std::clamp(request.direction.pitch_degrees, -steepest_pitch, steepest_pitch),
        request.direction.yaw_degrees
    );
    const vec3f forward = camera.get_forward();

    float32 away = 0.0F;
    if (flat) {
        const float32 tall = 2.0F * box.half_extent_along(camera.get_up());
        const float32 wide = 2.0F * box.half_extent_along(camera.get_right());

        const float32 fitted = std::max(tall, wide / camera.get_aspect_ratio()) * fit_margin;
        const float32 height = request.height.value_or(std::max(fitted, smallest_height));

        camera.set_orthographic(height);
        away = std::max(
            camera.view_depth_showing(height), box.radius() + camera.get_near() + clear_of_target
        );
    } else {
        camera.set_perspective();

        const float32 half_view = math::radians(camera.get_fov()) / 2.0F;
        const float32 fitted    = (box.radius() / std::sin(half_view)) * fit_margin;
        away                    = request.distance.value_or(std::max(fitted, smallest_distance));
    }

    camera.set_position(centre - forward * away);

    view_report seen = report();
    seen.looking_at  = focus;
    seen.target      = centre;
    seen.distance    = away;
    seen.height      = camera.view_height_at(away);
    seen.width       = seen.height * camera.get_aspect_ratio();
    return seen;
}

auto view_service::look_from(
    view_side side
) -> void {
    const auto& scene = state_->scene;

    view_request request{.direction = direction_of(side)};
    if (const std::string& edited = state_->edited_node();
        !edited.empty() && scene.name_to_entity.contains(edited)) {
        request.node = edited;
    }

    if (look(request)) {
        return;
    }

    const view_direction direction = direction_of(side);
    engine_->get_camera().set_rotation(direction.pitch_degrees, direction.yaw_degrees);
}

auto view_service::set_projection(
    gfx::projection_kind kind
) -> void {
    auto& camera = engine_->get_camera();
    if (camera.get_projection_kind() == kind) {
        return;
    }

    const float32 framed = framed_depth_();

    if (kind == gfx::projection_kind::orthographic) {
        camera.set_orthographic(camera.view_height_at(framed));
        return;
    }

    const float32 shown_from = camera.view_depth_showing(camera.get_orthographic_height());
    camera.set_perspective();
    camera.set_position(camera.get_position() + camera.get_forward() * (framed - shown_from));
}

auto view_service::toggle_projection() -> void {
    const bool flat = engine_->get_camera().is_orthographic();
    set_projection(flat ? gfx::projection_kind::perspective : gfx::projection_kind::orthographic);
}

auto view_service::set_hidden(const std::vector<std::string>& nodes)
    -> std::expected<void, std::string> {
    const auto& known = state_->scene.name_to_entity;

    for (const std::string& name : nodes) {
        if (!known.contains(name)) {
            std::string listed;
            for (const std::string& node : list_node_names(*state_)) {
                listed += listed.empty() ? node : std::format(", {}", node);
            }
            return refuse(std::format("there is no node '{}'; the nodes are: {}", name, listed));
        }
    }

    state_->scene.hidden_nodes = std::unordered_set<std::string>{nodes.begin(), nodes.end()};
    return {};
}

auto view_service::hidden() const -> std::vector<std::string> {
    std::vector<std::string> names{
        state_->scene.hidden_nodes.begin(), state_->scene.hidden_nodes.end()
    };
    std::ranges::sort(names);
    return names;
}

auto view_service::report() const -> view_report {
    const auto& camera = engine_->get_camera();

    const float32 framed = framed_depth_();
    const float32 height = camera.view_height_at(framed);

    return view_report{
        .looking_at = {},
        .target     = camera.get_position() + camera.get_forward() * framed,
        .distance   = framed,
        .height     = height,
        .width      = height * camera.get_aspect_ratio(),
        .projection = camera.get_projection_kind(),
    };
}

auto view_service::framed_depth_() const -> float32 {
    const auto& camera = engine_->get_camera();
    const auto& scene  = state_->scene;

    const auto root = scene.name_to_entity.find(scene.root_name);
    if (root == scene.name_to_entity.end()) {
        return camera.is_orthographic() ?
            camera.view_depth_showing(camera.get_orthographic_height()) :
            unframed_depth;
    }

    const world_box box = box_of_subtree(engine_->get_world(), root->second);
    const float32 depth = math::dot(box.centre() - camera.get_position(), camera.get_forward());
    return std::max(depth, camera.get_near() + clear_of_target);
}

}  // namespace vw::sculptor
