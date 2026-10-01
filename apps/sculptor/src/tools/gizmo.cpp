module vw.sculptor;

import std;

import vw.core;
import vw.ecs;
import vw.world;
import vw.platform;
import vw.gfx;

namespace vw::sculptor {

namespace {

constexpr float32 two_pi = 2.0F * math::pi;

constexpr int32 major_segments = 28;
constexpr int32 minor_segments = 6;

constexpr float32 head_fraction = 0.25F;

constexpr float32 shaft_radius = 0.018F;
constexpr float32 head_radius  = 0.055F;
constexpr float32 ring_radius  = 0.75F;
constexpr float32 tube_radius  = 0.022F;
constexpr float32 handle_half  = 0.05F;

constexpr float32 plane_handle_inner = 0.28F;
constexpr float32 plane_handle_outer = 0.46F;
constexpr float32 plane_min_facing   = 0.15F;

constexpr uint8 plane_alpha             = 0x70;
constexpr uint8 plane_alpha_highlighted = 0xD0;

constexpr float32 screen_height_fraction = 0.18F;

constexpr float32 pick_tolerance_of_gizmo_size = 0.09F;

constexpr float32 snap_translate = 0.5F;

constexpr float32 snap_pivot    = 0.5F;
constexpr float32 snap_fragment = 1.0F;
constexpr float32 snap_rotate   = 15.0F * math::deg_to_rad;
constexpr float32 snap_scale     = 0.1F;

constexpr auto axis_x = vec3f{1.0F, 0.0F, 0.0F};
constexpr auto axis_y = vec3f{0.0F, 1.0F, 0.0F};
constexpr auto axis_z = vec3f{0.0F, 0.0F, 1.0F};

constexpr std::array<gizmo_handle, 3> axis_handles{gizmo_handle::x, gizmo_handle::y, gizmo_handle::z};
constexpr std::array<gizmo_handle, 3> plane_handles{
    gizmo_handle::xy, gizmo_handle::yz, gizmo_handle::zx
};

struct plane_axes {
    std::size_t first;
    std::size_t second;
    std::size_t normal;
};

auto is_plane(gizmo_handle handle) -> bool {
    return handle == gizmo_handle::xy || handle == gizmo_handle::yz || handle == gizmo_handle::zx;
}

auto plane_axes_of(gizmo_handle plane) -> plane_axes {
    switch (plane) {
        case gizmo_handle::yz: return {.first = 1, .second = 2, .normal = 0};
        case gizmo_handle::zx: return {.first = 2, .second = 0, .normal = 1};
        default: return {.first = 0, .second = 1, .normal = 2};
    }
}

auto with_alpha(color col, uint8 alpha) -> color {
    return color{col.r(), col.g(), col.b(), alpha};
}

auto basis_of(const vec3f& axis) -> std::pair<vec3f, vec3f> {
    const auto ax = std::abs(axis.x);
    const auto ay = std::abs(axis.y);
    const auto az = std::abs(axis.z);

    const vec3f seed = (ax <= ay && ax <= az) ? axis_x : (ay <= az ? axis_y : axis_z);

    const auto u = math::normalize(math::cross(axis, seed));
    const auto v = math::cross(axis, u);
    return {u, v};
}

auto quat_from_axis_angle(const vec3f& axis, float32 angle) -> quat {
    const auto half = angle * 0.5F;
    const auto s    = std::sin(half);
    return quat{axis.x * s, axis.y * s, axis.z * s, std::cos(half)};
}

auto axis_index(gizmo_handle axis) -> std::size_t {
    switch (axis) {
        case gizmo_handle::y: return 1;
        case gizmo_handle::z: return 2;
        default: return 0;
    }
}

auto axis_color(gizmo_handle axis, bool highlighted) -> color {
    const auto base = [axis]() -> color {
        switch (axis) {
            case gizmo_handle::y: return colors::green_4;
            case gizmo_handle::z: return colors::red_4;
            default: return colors::blue_4;
        }
    }();

    return highlighted ? colors::amber_5 : base;
}

auto ray_plane(
    const vec3f& origin, const vec3f& normal, const spatial::ray& r, vec3f& hit_out
) -> bool {
    const auto denom = math::dot(normal, r.direction);
    if (std::abs(denom) < 1e-4F) {
        return false;
    }

    const auto t = math::dot(normal, origin - r.start) / denom;
    if (t < 0.0F) {
        return false;
    }

    hit_out = r.start + (r.direction * t);
    return true;
}

auto angle_on_plane(const vec3f& from_center, const vec3f& u, const vec3f& v) -> float32 {
    return std::atan2(math::dot(from_center, v), math::dot(from_center, u));
}

auto snapped(float32 value, float32 step) -> float32 {
    return std::round(value / step) * step;
}

}  // namespace

auto closest_on_axis(
    const vec3f& origin, const vec3f& axis, const spatial::ray& r, float32& t_out
) -> bool {
    const auto& d = r.direction;
    const auto w  = origin - r.start;

    const auto ad = math::dot(axis, d);
    const auto det = 1.0F - (ad * ad);
    if (std::abs(det) < 1e-5F) {
        return false;
    }

    const auto aw = math::dot(axis, w);
    const auto dw = math::dot(d, w);

    t_out = ((ad * dw) - aw) / det;
    return true;
}

auto distance_to_ray(
    const vec3f& point, const spatial::ray& r
) -> float32 {
    const auto w = point - r.start;

    const auto t       = std::max(math::dot(w, r.direction), 0.0F);
    const auto closest = r.start + (r.direction * t);
    return math::length(point - closest);
}

auto draw_handle_box(
    gfx::engine& engine, const vec3f& center, float32 half, color col
) -> void {
    auto& renderer = engine.get_renderer();

    const auto p = [&](float32 x, float32 y, float32 z) -> vec3f {
        return center + vec3f{x * half, y * half, z * half};
    };

    const auto v000 = p(-1, -1, -1);
    const auto v100 = p(+1, -1, -1);
    const auto v110 = p(+1, +1, -1);
    const auto v010 = p(-1, +1, -1);
    const auto v001 = p(-1, -1, +1);
    const auto v101 = p(+1, -1, +1);
    const auto v111 = p(+1, +1, +1);
    const auto v011 = p(-1, +1, +1);

    renderer.draw_quad(v000, v010, v110, v100, col);
    renderer.draw_quad(v001, v101, v111, v011, col);
    renderer.draw_quad(v000, v100, v101, v001, col);
    renderer.draw_quad(v010, v011, v111, v110, col);
    renderer.draw_quad(v000, v001, v011, v010, col);
    renderer.draw_quad(v100, v110, v111, v101, col);
}

gizmo::gizmo(
    engine_type& eng, app_state& st, operation_manager& op_manager, gizmo_target target,
    gizmo_commit commit
)
    : engine_(&eng)
    , state_(&st)
    , op_manager_(&op_manager)
    , target_(target)
    , commit_(commit) {}

auto gizmo::mode_() const -> gizmo_mode {
    if (dragging_) {
        return drag_mode_;
    }
    return target_ == gizmo_target::node ? state_->tool.gizmo : gizmo_mode::translate;
}

auto gizmo::build_frame_(
    ecs::entity ent
) const -> std::optional<frame> {
    auto& world = engine_->get_world();
    if (!world.has<ecs::transform_component>(ent)) {
        return std::nullopt;
    }

    const auto& tc = world.get<ecs::transform_component>(ent);

    if (target_ == gizmo_target::fragment && !state_->paste.active()) {
        return std::nullopt;
    }

    const auto pivot = target_ == gizmo_target::fragment
                           ? state_->paste.base_matrix(tc.get_world_matrix()) *
                                 state_->paste.centre()
                           : tc.get_world_matrix() * vec3f{0.0F, 0.0F, 0.0F};

    frame fr;
    fr.pivot = pivot;
    fr.axes  = {axis_x, axis_y, axis_z};

    if (target_ != gizmo_target::node) {
        const auto& node_matrix = tc.get_world_matrix();
        const auto base         = node_matrix * vec3f{0.0F, 0.0F, 0.0F};
        for (std::size_t i = 0; i < fr.axes.size(); ++i) {
            const auto tip = node_matrix * fr.axes[i];
            fr.axes[i]     = math::normalize(tip - base);
        }
    }

    if (target_ == gizmo_target::node && world.has<ecs::hierarchy_component>(ent)) {
        const auto parent = world.get<ecs::hierarchy_component>(ent).get_parent();
        if (parent.is_valid() && world.has<ecs::transform_component>(parent)) {
            const auto& parent_matrix = world.get<ecs::transform_component>(parent).get_world_matrix();
            const auto base           = parent_matrix * vec3f{0.0F, 0.0F, 0.0F};
            for (std::size_t i = 0; i < fr.axes.size(); ++i) {
                const auto tip = parent_matrix * fr.axes[i];
                fr.axes[i]     = math::normalize(tip - base);
            }
        }
    }

    const auto& camera = engine_->get_camera();
    fr.scale = math::length(pivot - camera.get_position()) * screen_height_fraction;

    return fr;
}

auto gizmo::cursor_ray_() const -> spatial::ray {
    const auto& window = engine_->get_window();
    const auto& camera = engine_->get_camera();
    return camera.screen_to_world_ray(window.get_cursor_pos(), window.get_size());
}

auto gizmo::plane_faces_camera_(
    const frame& fr, gizmo_handle plane
) const -> bool {
    const auto& normal = fr.axes[plane_axes_of(plane).normal];
    const auto view    = math::normalize(fr.pivot - engine_->get_camera().get_position());
    return std::abs(math::dot(normal, view)) > plane_min_facing;
}

auto gizmo::pick_(
    const frame& fr
) const -> gizmo_handle {
    const auto r = cursor_ray_();

    const auto tolerance = pick_tolerance_of_gizmo_size * fr.scale;

    auto best      = gizmo_handle::none;
    auto best_dist = std::numeric_limits<float32>::max();

    for (const auto axis_id : axis_handles) {
        const auto& axis = fr.axes[axis_index(axis_id)];

        if (mode_() == gizmo_mode::rotate) {
            vec3f hit;
            if (!ray_plane(fr.pivot, axis, r, hit)) {
                continue;
            }

            const auto radius = math::length(hit - fr.pivot);
            if (std::abs(radius - (ring_radius * fr.scale)) > tolerance) {
                continue;
            }

            const auto depth = math::length(hit - r.start);
            if (depth < best_dist) {
                best_dist = depth;
                best      = axis_id;
            }
            continue;
        }

        if (mode_() == gizmo_mode::scale) {
            const auto center = fr.pivot + (axis * fr.scale);
            const auto reach =
                std::max(handle_half * 1.8F, pick_tolerance_of_gizmo_size) * fr.scale;
            if (distance_to_ray(center, r) > reach) {
                continue;
            }

            const auto depth = math::length(center - r.start);
            if (depth < best_dist) {
                best_dist = depth;
                best      = axis_id;
            }
            continue;
        }

        float32 t = 0.0F;
        if (!closest_on_axis(fr.pivot, axis, r, t)) {
            continue;
        }

        if (t < 0.0F || t > fr.scale) {
            continue;
        }

        const auto point = fr.pivot + (axis * t);
        if (distance_to_ray(point, r) > tolerance) {
            continue;
        }

        const auto depth = math::length(point - r.start);
        if (depth < best_dist) {
            best_dist = depth;
            best      = axis_id;
        }
    }

    if (mode_() != gizmo_mode::translate) {
        return best;
    }

    for (const auto plane_id : plane_handles) {
        if (!plane_faces_camera_(fr, plane_id)) {
            continue;
        }

        const auto plane = plane_axes_of(plane_id);

        vec3f hit;
        if (!ray_plane(fr.pivot, fr.axes[plane.normal], r, hit)) {
            continue;
        }

        const auto offset       = hit - fr.pivot;
        const auto along_first  = math::dot(offset, fr.axes[plane.first]) / fr.scale;
        const auto along_second = math::dot(offset, fr.axes[plane.second]) / fr.scale;

        const auto inside = [](float32 along) -> bool {
            return along >= plane_handle_inner && along <= plane_handle_outer;
        };
        if (!inside(along_first) || !inside(along_second)) {
            continue;
        }

        const auto depth = math::length(hit - r.start);
        if (depth < best_dist) {
            best_dist = depth;
            best      = plane_id;
        }
    }

    return best;
}

auto gizmo::snap_enabled_() const -> bool {
    return engine_->get_window().is_key_pressed(plat::keyboard::keys::LEFT_CONTROL);
}

auto gizmo::on_mouse_move(
    ecs::entity ent
) -> void {
    const auto fr = build_frame_(ent);
    if (!fr.has_value()) {
        if (!dragging_) {
            hovered_ = gizmo_handle::none;
        }
        return;
    }

    if (!dragging_) {
        hovered_ = pick_(*fr);
        return;
    }

    if (target_ == gizmo_target::pivot) {
        apply_pivot_(ent, drag_frame_);
        return;
    }

    if (target_ == gizmo_target::fragment) {
        apply_fragment_(drag_frame_);
        return;
    }

    switch (mode_()) {
        case gizmo_mode::translate: apply_translate_(ent, drag_frame_); break;
        case gizmo_mode::rotate: apply_rotate_(ent, drag_frame_); break;
        case gizmo_mode::scale: apply_scale_(ent, drag_frame_); break;
    }
}

auto gizmo::on_mouse_press(
    ecs::entity ent
) -> bool {
    const auto fr = build_frame_(ent);
    if (!fr.has_value()) {
        return false;
    }

    const auto axis_id = pick_(*fr);
    if (axis_id == gizmo_handle::none) {
        return false;
    }

    auto& world = engine_->get_world();

    active_          = axis_id;
    hovered_         = axis_id;
    drag_mode_       = mode_();
    dragging_        = true;
    drag_frame_      = *fr;
    start_transform_ = world.get<ecs::transform_component>(ent).get_transform();

    if (target_ == gizmo_target::pivot && world.has<ecs::model_component>(ent)) {
        start_pivot_ = world.get<ecs::model_component>(ent).get_pivot();
    }

    if (target_ == gizmo_target::fragment) {
        start_origin_ = state_->paste.origin;
    }

    const auto r = cursor_ray_();

    if (is_plane(axis_id)) {
        start_plane_point_ = fr->pivot;
        ray_plane(fr->pivot, fr->axes[plane_axes_of(axis_id).normal], r, start_plane_point_);
        return true;
    }

    const auto& axis = fr->axes[axis_index(axis_id)];

    if (mode_() == gizmo_mode::rotate) {
        vec3f hit;
        if (ray_plane(fr->pivot, axis, r, hit)) {
            const auto [u, v] = basis_of(axis);
            start_angle_      = angle_on_plane(hit - fr->pivot, u, v);
        }
    } else {
        float32 t = 0.0F;
        if (closest_on_axis(fr->pivot, axis, r, t)) {
            start_offset_ = t;
        }
    }

    return true;
}

auto gizmo::on_mouse_release() -> void {
    if (!dragging_) {
        return;
    }

    dragging_ = false;
    active_   = gizmo_handle::none;

    if (commit_ == gizmo_commit::preview) {
        return;
    }

    const auto& name = state_->edited_node();
    if (!state_->scene.name_to_entity.contains(name)) {
        return;
    }

    auto& world    = engine_->get_world();
    const auto ent = state_->scene.name_to_entity[name];

    if (target_ == gizmo_target::pivot) {
        const auto final_pivot = world.get<ecs::model_component>(ent).get_pivot();
        world.system<ecs::model_system>().modify(ent).set_pivot(start_pivot_);

        op_manager_->execute(
            std::make_unique<set_pivot_operation>(
                *engine_, *state_, set_pivot_params{.name = name, .new_pivot = final_pivot}
            )
        );
        return;
    }

    const auto final_transform = world.get<ecs::transform_component>(ent).get_transform();
    world.system<ecs::transform_system>().modify(ent).set_transform(start_transform_);

    auto op = std::make_unique<set_transform_operation>(
        *engine_, *state_,
        set_transform_params{.name = name, .new_transform = final_transform}
    );
    op_manager_->execute(std::move(op));
}

auto gizmo::translation_delta_(
    const frame& fr, std::optional<float32> snap_step
) const -> std::optional<vec3f> {
    if (active_ == gizmo_handle::none) {
        return std::nullopt;
    }

    const auto r = cursor_ray_();

    std::array<float32, 3> delta{};

    if (is_plane(active_)) {
        const auto plane = plane_axes_of(active_);

        vec3f hit;
        if (!ray_plane(fr.pivot, fr.axes[plane.normal], r, hit)) {
            return std::nullopt;
        }

        const auto offset   = hit - start_plane_point_;
        delta[plane.first]  = math::dot(offset, fr.axes[plane.first]);
        delta[plane.second] = math::dot(offset, fr.axes[plane.second]);
    } else {
        float32 t = 0.0F;
        if (!closest_on_axis(fr.pivot, fr.axes[axis_index(active_)], r, t)) {
            return std::nullopt;
        }

        delta[axis_index(active_)] = t - start_offset_;
    }

    if (snap_step.has_value()) {
        for (auto& component : delta) {
            component = snapped(component, *snap_step);
        }
    }

    return vec3f{delta[0], delta[1], delta[2]};
}

auto gizmo::apply_translate_(
    ecs::entity ent, const frame& fr
) -> void {
    const auto delta = translation_delta_(
        fr, snap_enabled_() ? std::optional<float32>{snap_translate} : std::nullopt
    );
    if (!delta.has_value()) {
        return;
    }

    engine_->get_world().system<ecs::transform_system>().modify(ent).set_position(
        start_transform_.get_position() + *delta
    );
}

auto gizmo::apply_pivot_(
    ecs::entity ent, const frame& fr
) -> void {
    const auto delta = translation_delta_(fr, snap_pivot);
    if (!delta.has_value()) {
        return;
    }

    engine_->get_world().system<ecs::model_system>().modify(ent).set_pivot(start_pivot_ + *delta);
}

auto gizmo::apply_fragment_(
    const frame& fr
) -> void {
    const auto delta = translation_delta_(fr, snap_fragment);
    if (!delta.has_value() || !state_->paste.active()) {
        return;
    }

    const auto origin = vec3i{
        start_origin_.x + static_cast<int32>(std::lround(delta->x)),
        start_origin_.y + static_cast<int32>(std::lround(delta->y)),
        start_origin_.z + static_cast<int32>(std::lround(delta->z)),
    };
    if (origin == state_->paste.origin) {
        return;
    }

    state_->paste.origin        = origin;
    state_->paste.preview_stale = true;
}

auto gizmo::apply_rotate_(
    ecs::entity ent, const frame& fr
) -> void {
    const auto r     = cursor_ray_();
    const auto& axis = fr.axes[axis_index(active_)];

    vec3f hit;
    if (!ray_plane(fr.pivot, axis, r, hit)) {
        return;
    }

    const auto [u, v] = basis_of(axis);
    auto delta        = angle_on_plane(hit - fr.pivot, u, v) - start_angle_;

    if (snap_enabled_()) {
        delta = snapped(delta, snap_rotate);
    }

    const std::array<vec3f, 3> local_axes{axis_x, axis_y, axis_z};
    const auto rotation =
        quat_from_axis_angle(local_axes[axis_index(active_)], delta) * start_transform_.get_rotation();

    engine_->get_world().system<ecs::transform_system>().modify(ent).set_rotation(
        math::normalize(rotation)
    );
}

auto gizmo::apply_scale_(
    ecs::entity ent, const frame& fr
) -> void {
    const auto r     = cursor_ray_();
    const auto& axis = fr.axes[axis_index(active_)];

    float32 t = 0.0F;
    if (!closest_on_axis(fr.pivot, axis, r, t)) {
        return;
    }

    auto factor = 1.0F + ((t - start_offset_) / fr.scale);
    if (snap_enabled_()) {
        factor = snapped(factor, snap_scale);
    }

    constexpr float32 min_factor = 0.01F;
    factor = std::max(factor, min_factor);

    auto next = start_transform_.get_scale();
    switch (active_) {
        case gizmo_handle::x: next.x *= factor; break;
        case gizmo_handle::y: next.y *= factor; break;
        case gizmo_handle::z: next.z *= factor; break;
        default: return;
    }

    engine_->get_world().system<ecs::transform_system>().modify(ent).set_scale(next);
}

auto gizmo::render(
    ecs::entity ent
) -> void {
    const auto fr = build_frame_(ent);
    if (!fr.has_value()) {
        return;
    }

    const auto lit = dragging_ ? active_ : hovered_;

    for (const auto axis_id : axis_handles) {
        const auto& axis = fr->axes[axis_index(axis_id)];
        const auto col   = axis_color(axis_id, axis_id == lit);

        switch (mode_()) {
            case gizmo_mode::translate: draw_arrow_(*fr, axis, col); break;
            case gizmo_mode::rotate: draw_torus_(*fr, axis, col); break;
            case gizmo_mode::scale: {
                const auto tip = fr->pivot + (axis * fr->scale);
                engine_->get_renderer().draw_line(fr->pivot, tip, col);
                draw_handle_box_(tip, handle_half * fr->scale, col);
                break;
            }
        }
    }

    if (mode_() != gizmo_mode::translate) {
        return;
    }

    for (const auto plane_id : plane_handles) {
        const bool highlighted = plane_id == lit;
        if (!highlighted && !plane_faces_camera_(*fr, plane_id)) {
            continue;
        }

        const auto normal_axis = axis_handles[plane_axes_of(plane_id).normal];
        draw_plane_handle_(
            *fr, plane_id,
            with_alpha(
                axis_color(normal_axis, highlighted),
                highlighted ? plane_alpha_highlighted : plane_alpha
            )
        );
    }
}

auto gizmo::draw_plane_handle_(
    const frame& fr, gizmo_handle plane_id, color col
) -> void {
    const auto plane = plane_axes_of(plane_id);

    const auto first_inner  = fr.axes[plane.first] * (plane_handle_inner * fr.scale);
    const auto first_outer  = fr.axes[plane.first] * (plane_handle_outer * fr.scale);
    const auto second_inner = fr.axes[plane.second] * (plane_handle_inner * fr.scale);
    const auto second_outer = fr.axes[plane.second] * (plane_handle_outer * fr.scale);

    engine_->get_renderer().draw_quad(
        fr.pivot + first_inner + second_inner, fr.pivot + first_outer + second_inner,
        fr.pivot + first_outer + second_outer, fr.pivot + first_inner + second_outer, col
    );
}

auto gizmo::draw_arrow_(
    const frame& fr, const vec3f& axis, color col
) -> void {
    auto& renderer = engine_->get_renderer();

    const auto [u, v] = basis_of(axis);

    const auto shaft_end = fr.pivot + (axis * (fr.scale * (1.0F - head_fraction)));
    const auto tip       = fr.pivot + (axis * fr.scale);

    const auto shaft_r = shaft_radius * fr.scale;
    const auto head_r  = head_radius * fr.scale;

    for (int32 i = 0; i < major_segments; ++i) {
        const auto a0 = two_pi * static_cast<float32>(i) / major_segments;
        const auto a1 = two_pi * static_cast<float32>(i + 1) / major_segments;

        const auto d0 = (u * std::cos(a0)) + (v * std::sin(a0));
        const auto d1 = (u * std::cos(a1)) + (v * std::sin(a1));

        renderer.draw_quad(
            fr.pivot + (d0 * shaft_r), shaft_end + (d0 * shaft_r), shaft_end + (d1 * shaft_r),
            fr.pivot + (d1 * shaft_r), col
        );

        renderer.draw_triangle(shaft_end + (d0 * head_r), tip, shaft_end + (d1 * head_r), col);
        renderer.draw_triangle(shaft_end, shaft_end + (d0 * head_r), shaft_end + (d1 * head_r), col);
    }
}

auto gizmo::draw_torus_(
    const frame& fr, const vec3f& axis, color col
) -> void {
    auto& renderer    = engine_->get_renderer();
    const auto [u, v] = basis_of(axis);

    const auto radius = ring_radius * fr.scale;
    const auto tube   = tube_radius * fr.scale;

    for (int32 i = 0; i < major_segments; ++i) {
        const auto a0 = two_pi * static_cast<float32>(i) / major_segments;
        const auto a1 = two_pi * static_cast<float32>(i + 1) / major_segments;

        const auto dir0 = (u * std::cos(a0)) + (v * std::sin(a0));
        const auto dir1 = (u * std::cos(a1)) + (v * std::sin(a1));

        const auto center0 = fr.pivot + (dir0 * radius);
        const auto center1 = fr.pivot + (dir1 * radius);

        for (int32 j = 0; j < minor_segments; ++j) {
            const auto b0 = two_pi * static_cast<float32>(j) / minor_segments;
            const auto b1 = two_pi * static_cast<float32>(j + 1) / minor_segments;

            const auto off00 = (dir0 * std::cos(b0)) + (axis * std::sin(b0));
            const auto off01 = (dir0 * std::cos(b1)) + (axis * std::sin(b1));
            const auto off10 = (dir1 * std::cos(b0)) + (axis * std::sin(b0));
            const auto off11 = (dir1 * std::cos(b1)) + (axis * std::sin(b1));

            renderer.draw_quad(
                center0 + (off00 * tube), center1 + (off10 * tube), center1 + (off11 * tube),
                center0 + (off01 * tube), col
            );
        }
    }
}

auto gizmo::draw_handle_box_(
    const vec3f& center, float32 half, color col
) -> void {
    draw_handle_box(*engine_, center, half, col);
}

}  // namespace vw::sculptor
