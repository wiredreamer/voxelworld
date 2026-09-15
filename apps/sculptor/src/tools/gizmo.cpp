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

// Сегментов по большой и малой окружности тора. Манипулятор мелкий на экране, и
// разница между шестнадцатью и тридцатью двумя гранями не видна, а вершин вдвое
// меньше.
constexpr int32 major_segments = 28;
constexpr int32 minor_segments = 6;

// Доля длины оси, которую занимает наконечник.
constexpr float32 head_fraction = 0.25F;

constexpr float32 shaft_radius = 0.018F;
constexpr float32 head_radius  = 0.055F;
constexpr float32 ring_radius  = 0.75F;
constexpr float32 tube_radius  = 0.022F;
constexpr float32 handle_half  = 0.05F;

// Манипулятор занимает примерно эту долю высоты экрана вне зависимости от того,
// как далеко камера.
constexpr float32 screen_size = 0.18F;

// Допуск попадания — тоже доля от размера манипулятора, а не мировая величина:
// так ручка одинаково ловится на любом отдалении камеры.
constexpr float32 pick_tolerance = 0.09F;

constexpr float32 snap_translate = 0.5F;

// Шаг точки вращения — полвокселя, и он не отключается: середина вокселя
// приходится ровно на половину, а осмысленных значений между этими двумя у
// точки вращения нет.
constexpr float32 snap_pivot = 0.5F;
constexpr float32 snap_rotate    = 15.0F * math::deg_to_rad;
constexpr float32 snap_scale     = 0.1F;

constexpr auto axis_x = vec3f{1.0F, 0.0F, 0.0F};
constexpr auto axis_y = vec3f{0.0F, 1.0F, 0.0F};
constexpr auto axis_z = vec3f{0.0F, 0.0F, 1.0F};

// Два орта, дополняющие ось до базиса. Ось единичная, поэтому достаточно взять
// любой неколлинеарный вектор — им служит наименьшая по модулю координатная ось.
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

auto axis_index(gizmo_axis axis) -> std::size_t {
    switch (axis) {
        case gizmo_axis::y: return 1;
        case gizmo_axis::z: return 2;
        default: return 0;
    }
}

// Цвета те же, что у осей сцены в app.cpp: X синяя, Y зелёная, Z красная.
// Расходиться с ними нельзя — рядом на экране это читалось бы как разные оси.
auto axis_color(gizmo_axis axis, bool highlighted) -> color {
    const auto base = [axis]() -> color {
        switch (axis) {
            case gizmo_axis::y: return colors::green_4;
            case gizmo_axis::z: return colors::red_4;
            default: return colors::blue_4;
        }
    }();

    return highlighted ? colors::amber_5 : base;
}

// Параметр вдоль оси, в котором прямая ближе всего к лучу. Вырожденный случай —
// взгляд вдоль самой оси: тогда ближайшая точка не определена, и ручка просто
// не берётся.
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

auto distance_to_ray(const vec3f& point, const spatial::ray& r) -> float32 {
    const auto w = point - r.start;

    // Луч, а не прямая: точка позади камеры обязана меряться до её начала,
    // иначе она «попадает» в ручку, оказавшуюся за спиной.
    const auto t       = std::max(math::dot(w, r.direction), 0.0F);
    const auto closest = r.start + (r.direction * t);
    return math::length(point - closest);
}

// Пересечение луча с плоскостью кольца. Скользящий взгляд отсекается: в нём
// точка пересечения улетает, и угол скачет.
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

gizmo::gizmo(
    engine_type& eng, app_state& st, operation_manager& op_manager, gizmo_target target
)
    : engine_(&eng), state_(&st), op_manager_(&op_manager), target_(target) {}

auto gizmo::set_mode(
    gizmo_mode mode
) -> void {
    // У точки вращения есть только перенос: поворачивать и масштабировать точку
    // нечего, поэтому манипулятор с такой целью режима не меняет вовсе.
    if (dragging_ || target_ == gizmo_target::pivot) {
        return;
    }
    mode_ = mode;
}

auto gizmo::build_frame_(
    ecs::entity ent
) const -> std::optional<frame> {
    auto& world = engine_->get_world();
    if (!world.has<ecs::transform_component>(ent)) {
        return std::nullopt;
    }

    const auto& tc = world.get<ecs::transform_component>(ent);

    // Начало координат узла и есть его точка вращения: объём висит вокруг неё
    // со сдвигом на собственный pivot, но сама она в матрице узла — это ноль.
    const auto pivot = tc.get_world_matrix() * vec3f{0.0F, 0.0F, 0.0F};

    frame fr;
    fr.pivot = pivot;
    fr.axes  = {axis_x, axis_y, axis_z};

    // Точка вращения задана в вокселях объёма, а объём повёрнут вместе с самим
    // узлом — значит, и ручки ходят по его осям, а не по родительским.
    if (target_ == gizmo_target::pivot) {
        const auto& node_matrix = tc.get_world_matrix();
        const auto base         = node_matrix * vec3f{0.0F, 0.0F, 0.0F};
        for (std::size_t i = 0; i < fr.axes.size(); ++i) {
            const auto tip = node_matrix * fr.axes[i];
            fr.axes[i]     = math::normalize(tip - base);
        }
    }

    // Позиция и поворот узла заданы в пространстве родителя, поэтому и ручки
    // ходят по его осям: иначе дельта по мировой оси легла бы в локальные поля
    // криво, стоит родителю повернуться.
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
    fr.scale = math::length(pivot - camera.get_position()) * screen_size;

    return fr;
}

auto gizmo::pick_(
    const frame& fr
) const -> gizmo_axis {
    const auto& window = engine_->get_window();
    const auto& camera = engine_->get_camera();
    const auto r = camera.screen_to_world_ray(window.get_cursor_pos(), window.get_size());

    const auto tolerance = pick_tolerance * fr.scale;

    auto best      = gizmo_axis::none;
    auto best_dist = std::numeric_limits<float32>::max();

    constexpr std::array<gizmo_axis, 3> axes{gizmo_axis::x, gizmo_axis::y, gizmo_axis::z};

    for (const auto axis_id : axes) {
        const auto& axis = fr.axes[axis_index(axis_id)];

        if (mode_ == gizmo_mode::rotate) {
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

        // Ручка масштаба — кубик на конце оси, и меряться надо до его центра.
        // Отрезком его не описать: куб стоит поперёк оси и выступает за её
        // конец, так что ограничение по длине резало ровно половину ручки.
        if (mode_ == gizmo_mode::scale) {
            const auto center = fr.pivot + (axis * fr.scale);
            const auto reach  = std::max(handle_half * 1.8F, pick_tolerance) * fr.scale;
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

    return best;
}

// Шаг — по требованию, а не по умолчанию: манипулятором чаще всего правят позу
// для кейфрейма, а её подбирают на глаз.
auto gizmo::snap_enabled_() const -> bool {
    return engine_->get_window().is_key_pressed(plat::keyboard::keys::LEFT_CONTROL);
}

auto gizmo::on_mouse_move(
    ecs::entity ent
) -> void {
    const auto fr = build_frame_(ent);
    if (!fr.has_value()) {
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

    switch (mode_) {
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
    if (axis_id == gizmo_axis::none) {
        return false;
    }

    auto& world = engine_->get_world();

    active_          = axis_id;
    hovered_         = axis_id;
    dragging_        = true;
    drag_frame_      = *fr;
    start_transform_ = world.get<ecs::transform_component>(ent).get_transform();

    if (target_ == gizmo_target::pivot && world.has<ecs::model_component>(ent)) {
        start_pivot_ = world.get<ecs::model_component>(ent).get_pivot();
    }

    const auto& window = engine_->get_window();
    const auto& camera = engine_->get_camera();
    const auto r       = camera.screen_to_world_ray(window.get_cursor_pos(), window.get_size());
    const auto& axis   = fr->axes[axis_index(axis_id)];

    if (mode_ == gizmo_mode::rotate) {
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
    active_   = gizmo_axis::none;

    const auto& name = state_->edited_node();
    if (!state_->scene.name_to_entity.contains(name)) {
        return;
    }

    auto& world    = engine_->get_world();
    const auto ent = state_->scene.name_to_entity[name];

    // Жест — одна запись в истории. Пока тянут, правка идёт напрямую, и только
    // на отпускании операция получает пару «было / стало»: иначе Ctrl+Z
    // откатывал бы по кадру.
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

auto gizmo::apply_translate_(
    ecs::entity ent, const frame& fr
) -> void {
    const auto& window = engine_->get_window();
    const auto& camera = engine_->get_camera();
    const auto r       = camera.screen_to_world_ray(window.get_cursor_pos(), window.get_size());
    const auto& axis   = fr.axes[axis_index(active_)];

    float32 t = 0.0F;
    if (!closest_on_axis(fr.pivot, axis, r, t)) {
        return;
    }

    auto delta = t - start_offset_;
    if (snap_enabled_()) {
        delta = snapped(delta, snap_translate);
    }

    auto next = start_transform_.get_position();
    switch (active_) {
        case gizmo_axis::x: next.x += delta; break;
        case gizmo_axis::y: next.y += delta; break;
        case gizmo_axis::z: next.z += delta; break;
        default: return;
    }

    engine_->get_world().system<ecs::transform_system>().modify(ent).set_position(next);
}

auto gizmo::apply_pivot_(
    ecs::entity ent, const frame& fr
) -> void {
    const auto& window = engine_->get_window();
    const auto& camera = engine_->get_camera();
    const auto r       = camera.screen_to_world_ray(window.get_cursor_pos(), window.get_size());
    const auto& axis   = fr.axes[axis_index(active_)];

    float32 t = 0.0F;
    if (!closest_on_axis(fr.pivot, axis, r, t)) {
        return;
    }

    const auto delta = snapped(t - start_offset_, snap_pivot);

    auto next = start_pivot_;
    switch (active_) {
        case gizmo_axis::x: next.x += delta; break;
        case gizmo_axis::y: next.y += delta; break;
        case gizmo_axis::z: next.z += delta; break;
        default: return;
    }

    // Сам маркер при этом не двигается: он стоит в начале координат узла, а
    // переезжает объём. Точку ставят не «сюда», а «на этот воксель модели».
    engine_->get_world().system<ecs::model_system>().modify(ent).set_pivot(next);
}

auto gizmo::apply_rotate_(
    ecs::entity ent, const frame& fr
) -> void {
    const auto& window = engine_->get_window();
    const auto& camera = engine_->get_camera();
    const auto r       = camera.screen_to_world_ray(window.get_cursor_pos(), window.get_size());
    const auto& axis   = fr.axes[axis_index(active_)];

    vec3f hit;
    if (!ray_plane(fr.pivot, axis, r, hit)) {
        return;
    }

    const auto [u, v] = basis_of(axis);
    auto delta        = angle_on_plane(hit - fr.pivot, u, v) - start_angle_;

    if (snap_enabled_()) {
        delta = snapped(delta, snap_rotate);
    }

    // Ось берётся локальная: поворот узла задан относительно родителя, и
    // накладывается он слева, как вращение всей его системы координат.
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
    const auto& window = engine_->get_window();
    const auto& camera = engine_->get_camera();
    const auto r       = camera.screen_to_world_ray(window.get_cursor_pos(), window.get_size());
    const auto& axis   = fr.axes[axis_index(active_)];

    float32 t = 0.0F;
    if (!closest_on_axis(fr.pivot, axis, r, t)) {
        return;
    }

    // Тянут на длину манипулятора — масштаб меняется вдвое: так жест одинаково
    // ощущается и вблизи, и издали, где сам манипулятор крупнее в мире.
    auto factor = 1.0F + ((t - start_offset_) / fr.scale);
    if (snap_enabled_()) {
        factor = snapped(factor, snap_scale);
    }

    constexpr float32 min_factor = 0.01F;
    factor = std::max(factor, min_factor);

    auto next = start_transform_.get_scale();
    switch (active_) {
        case gizmo_axis::x: next.x *= factor; break;
        case gizmo_axis::y: next.y *= factor; break;
        case gizmo_axis::z: next.z *= factor; break;
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

    constexpr std::array<gizmo_axis, 3> axes{gizmo_axis::x, gizmo_axis::y, gizmo_axis::z};

    for (const auto axis_id : axes) {
        const auto& axis = fr->axes[axis_index(axis_id)];
        const auto col   = axis_color(axis_id, axis_id == lit);

        switch (mode_) {
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

    // Тор, а не лента: плоское кольцо исчезает, когда смотришь на него с ребра,
    // а целятся в него чаще всего именно в этом положении.
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
    auto& renderer = engine_->get_renderer();

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

}  // namespace vw::sculptor
