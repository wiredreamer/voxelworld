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

constexpr float32 handle_half_of_view_distance = 0.009F;
constexpr float32 handle_gap_in_halves         = 2.0F;
constexpr float32 handle_reach_in_halves       = 1.8F;

constexpr std::size_t axis_count = 3;

auto to_float(vec3i value) -> vec3f {
    return vec3f{
        static_cast<float32>(value.x),
        static_cast<float32>(value.y),
        static_cast<float32>(value.z),
    };
}

}  // namespace

select_box_tool::select_box_tool(
    engine_type& eng, app_state& st, operation_manager&
)
    : engine_(&eng), state_(&st) {}

auto select_box_tool::edited_entity_() const -> ecs::entity {
    const auto it = state_->scene.name_to_entity.find(state_->edited_node());
    if (it == state_->scene.name_to_entity.end()) {
        return ecs::invalid_entity;
    }

    const auto& world        = engine_->get_world();
    const bool is_renderable = world.has<ecs::transform_component>(it->second) &&
                               world.has<ecs::model_component>(it->second);
    return is_renderable ? it->second : ecs::invalid_entity;
}

auto select_box_tool::cursor_ray_() const -> spatial::ray {
    const auto& window = engine_->get_window();
    const auto& camera = engine_->get_camera();
    return camera.screen_to_world_ray(window.get_cursor_pos(), window.get_size());
}

auto select_box_tool::voxel_under_cursor_() -> std::optional<vec3i> {
    const auto ent = edited_entity_();
    if (!ent.is_valid()) {
        return std::nullopt;
    }

    const auto& world = engine_->get_world();
    const auto hit =
        world.system<ecs::spatial_system>().voxel_ray_cast(cursor_ray_(), ray_cast_entities_);
    if (!hit || hit->ent != ent) {
        return std::nullopt;
    }

    return hit->voxel_pos;
}

auto select_box_tool::shows_handles_() const -> bool {
    const auto& window = engine_->get_window();
    return active_handle_.has_value() ||
           window.is_key_pressed(plat::keyboard::keys::LEFT_SHIFT) ||
           window.is_key_pressed(plat::keyboard::keys::RIGHT_SHIFT);
}

auto select_box_tool::grip_of_(
    face_handle handle
) const -> std::optional<face_grip> {
    const auto ent = edited_entity_();
    if (!ent.is_valid() || !state_->volume.selection) {
        return std::nullopt;
    }

    const auto& world = engine_->get_world();
    const auto volume = ecs::model_matrix(
        world.get<ecs::transform_component>(ent), world.get<ecs::model_component>(ent)
    );

    const auto& box = state_->volume.selection->box;
    const auto low  = to_float(box.min);
    const auto high = to_float(box.max) + vec3f{1.0F, 1.0F, 1.0F};

    auto on_face         = (low + high) * 0.5F;
    on_face[handle.axis] = handle.high ? high[handle.axis] : low[handle.axis];

    auto beyond = on_face;
    beyond[handle.axis] += handle.high ? 1.0F : -1.0F;

    const auto face_world   = volume * on_face;
    const auto beyond_world = volume * beyond;
    const auto step         = beyond_world - face_world;
    const auto step_length  = math::length(step);
    if (step_length <= 0.0F) {
        return std::nullopt;
    }

    face_grip grip;
    grip.outward         = step * (1.0F / step_length);
    grip.world_per_voxel = step_length;
    grip.half            = math::length(face_world - engine_->get_camera().get_position()) *
                           handle_half_of_view_distance;
    grip.center          = face_world + (grip.outward * (grip.half * handle_gap_in_halves));

    return grip;
}

auto select_box_tool::handle_under_cursor_() const -> std::optional<face_handle> {
    if (!shows_handles_()) {
        return std::nullopt;
    }

    const auto r = cursor_ray_();

    std::optional<face_handle> best;
    auto best_depth = std::numeric_limits<float32>::max();

    for (std::size_t axis = 0; axis < axis_count; ++axis) {
        for (const bool high : {false, true}) {
            const auto handle = face_handle{.axis = axis, .high = high};
            const auto grip   = grip_of_(handle);
            if (!grip) {
                continue;
            }

            if (distance_to_ray(grip->center, r) > grip->half * handle_reach_in_halves) {
                continue;
            }

            const auto depth = math::length(grip->center - r.start);
            if (depth < best_depth) {
                best_depth = depth;
                best       = handle;
            }
        }
    }

    return best;
}

auto select_box_tool::select_between_(
    vec3i first, vec3i second
) -> void {
    const auto ent = edited_entity_();
    if (!ent.is_valid()) {
        return;
    }

    asset::voxel_bounds box;
    for (std::size_t i = 0; i < axis_count; ++i) {
        box.min[i] = std::min(first[i], second[i]);
        box.max[i] = std::max(first[i], second[i]);
    }

    state_->volume.selection = volume_selection{
        .box         = box,
        .node_name   = state_->edited_node(),
        .volume_size = engine_->get_world().get<ecs::model_component>(ent).size(),
    };
}

auto select_box_tool::drag_face_() -> void {
    auto& selection = state_->volume.selection;
    if (!active_handle_ || !selection) {
        return;
    }

    float32 along = 0.0F;
    if (!closest_on_axis(active_grip_.center, active_grip_.outward, cursor_ray_(), along)) {
        return;
    }

    const auto moved_voxels = static_cast<int32>(
        std::lround((along - active_start_along_) / active_grip_.world_per_voxel)
    );

    const auto axis = active_handle_->axis;
    auto& box       = selection->box;

    if (active_handle_->high) {
        box.max[axis] = std::clamp(
            active_start_face_ + moved_voxels, box.min[axis], selection->volume_size[axis] - 1
        );
    } else {
        box.min[axis] = std::clamp(active_start_face_ - moved_voxels, 0, box.max[axis]);
    }
}

auto select_box_tool::render(
    float
) -> void {
    const auto ent = edited_entity_();
    if (!ent.is_valid()) {
        return;
    }

    const bool with_handles = shows_handles_();

    for (std::size_t axis = 0; with_handles && axis < axis_count; ++axis) {
        for (const bool high : {false, true}) {
            const auto handle = face_handle{.axis = axis, .high = high};
            const auto grip   = grip_of_(handle);
            if (!grip) {
                continue;
            }

            const bool lit = active_handle_ ? handle == *active_handle_
                                            : hovered_handle_ && handle == *hovered_handle_;
            draw_handle_box(*engine_, grip->center, grip->half, lit ? colors::amber_5 : colors::white);
        }
    }

    const bool on_handle = with_handles && hovered_handle_.has_value();
    if (!hovered_voxel_ || dragging_ || active_handle_ || on_handle) {
        return;
    }

    auto& world = engine_->get_world();

    const auto outline =
        ecs::model_matrix(
            world.get<ecs::transform_component>(ent), world.get<ecs::model_component>(ent)
        ) *
        math::translation_matrix(to_float(*hovered_voxel_)) *  //
        math::scale_matrix(vec3f{1.1f, 1.1f, 1.1f}) *          //
        math::translation_matrix(vec3f{-0.05f, -0.05f, -0.05f});

    engine_->get_renderer().draw_box(outline, vec3f{1.f, 1.f, 1.f}, colors::black);
}

auto select_box_tool::on_key_press(
    const plat::key_press_event&
) -> void {}

auto select_box_tool::on_mouse_move(
    const plat::mouse_move_event&
) -> void {
    const bool holds_button =
        engine_->get_window().is_mouse_button_pressed(plat::mouse::buttons::LEFT);
    if (!holds_button) {
        dragging_ = false;
        active_handle_.reset();
    }

    if (active_handle_) {
        drag_face_();
        return;
    }

    hovered_handle_ = dragging_ ? std::nullopt : handle_under_cursor_();
    hovered_voxel_  = voxel_under_cursor_();

    if (dragging_ && hovered_voxel_) {
        select_between_(drag_start_, *hovered_voxel_);
    }
}

auto select_box_tool::on_mouse_press(
    const plat::mouse_press_event& ev
) -> void {
    if (ev.button != plat::mouse::buttons::LEFT) {
        return;
    }

    if (const auto handle = handle_under_cursor_()) {
        const auto grip = grip_of_(*handle);
        if (!grip) {
            return;
        }

        const auto& box = state_->volume.selection->box;

        active_handle_     = handle;
        active_grip_       = *grip;
        active_start_face_ = handle->high ? box.max[handle->axis] : box.min[handle->axis];

        active_start_along_ = 0.0F;
        static_cast<void>(
            closest_on_axis(grip->center, grip->outward, cursor_ray_(), active_start_along_)
        );
        return;
    }

    hovered_voxel_ = voxel_under_cursor_();
    if (!hovered_voxel_) {
        state_->volume.selection.reset();
        return;
    }

    const auto& selection = state_->volume.selection;
    if ((ev.mods & plat::keyboard::mods::SHIFT) && selection) {
        const auto box = selection->box;

        vec3i low;
        vec3i high;
        for (std::size_t i = 0; i < axis_count; ++i) {
            low[i]  = std::min(box.min[i], (*hovered_voxel_)[i]);
            high[i] = std::max(box.max[i], (*hovered_voxel_)[i]);
        }

        select_between_(low, high);
        return;
    }

    drag_start_ = *hovered_voxel_;
    dragging_   = true;
    select_between_(drag_start_, drag_start_);
}

auto select_box_tool::on_mouse_release(
    const plat::mouse_release_event& ev
) -> void {
    if (ev.button == plat::mouse::buttons::LEFT) {
        dragging_ = false;
        active_handle_.reset();
    }
}

auto select_box_tool::on_activate() -> void {
    dragging_ = false;
    active_handle_.reset();
    hovered_handle_ = handle_under_cursor_();
    hovered_voxel_  = voxel_under_cursor_();
}

}  // namespace vw::sculptor
