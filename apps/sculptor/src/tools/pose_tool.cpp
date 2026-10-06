module vw.sculptor;

import std;

import vw.core;
import vw.ecs;
import vw.world;
import vw.platform;
import vw.gfx;

namespace vw::sculptor {

pose_tool::pose_tool(
    engine_type& eng, app_state& st, operation_manager& op_manager
)
    : engine_(&eng)
    , state_(&st)
    , gizmo_(eng, st, op_manager, gizmo_target::node, gizmo_commit::preview) {}

auto pose_tool::render(
    [[maybe_unused]] float delta_time
) -> void {
    const auto it = state_->scene.name_to_entity.find(state_->scene.selected_name);
    if (it == state_->scene.name_to_entity.end()) {
        return;
    }

    draw_entity_box_(it->second, colors::green_8);
    gizmo_.render(it->second);
}

auto pose_tool::on_key_press(
    [[maybe_unused]] const plat::key_press_event& ev
) -> void {}

auto pose_tool::on_mouse_move(
    [[maybe_unused]] const plat::mouse_move_event& ev
) -> void {
    const auto it = state_->scene.name_to_entity.find(state_->scene.selected_name);
    if (it != state_->scene.name_to_entity.end()) {
        gizmo_.on_mouse_move(it->second);
    }
}

auto pose_tool::on_mouse_press(
    const plat::mouse_press_event& ev
) -> void {
    if (ev.button != plat::mouse::buttons::LEFT) {
        return;
    }

    const auto it = state_->scene.name_to_entity.find(state_->scene.selected_name);
    if (it != state_->scene.name_to_entity.end() && gizmo_.on_mouse_press(it->second)) {
        return;
    }

    const auto& world  = engine_->get_world();
    const auto& window = engine_->get_window();
    const auto& camera = engine_->get_camera();

    const auto ray = camera.screen_to_world_ray(window.get_cursor_pos(), window.get_size());
    const auto hit = world.system<ecs::spatial_system>().voxel_ray_cast(ray, ray_cast_entities_);
    if (!hit) {
        return;
    }

    const auto named = state_->scene.entity_to_name.find(hit->ent);
    if (named == state_->scene.entity_to_name.end()) {
        return;
    }

    state_->scene.selected_name      = named->second;
    state_->anim.selected_track_name = named->second;
}

auto pose_tool::on_mouse_release(
    const plat::mouse_release_event& ev
) -> void {
    if (ev.button == plat::mouse::buttons::LEFT) {
        gizmo_.on_mouse_release();
    }
}

auto pose_tool::on_activate() -> void {}

auto pose_tool::draw_entity_box_(
    ecs::entity ent, color col
) -> void {
    auto& world = engine_->get_world();

    if (!world.has<ecs::transform_component>(ent) || !world.has<ecs::model_component>(ent)) {
        return;
    }

    const auto& tc = world.get<ecs::transform_component>(ent);
    const auto& mc = world.get<ecs::model_component>(ent);
    if (!mc.has_model()) {
        return;
    }

    constexpr float32 padding = 0.1f;

    const auto box_matrix =
        ecs::model_matrix(tc, mc) * math::translation_matrix(vec3f{-padding, -padding, -padding});

    const auto box_size = vec3f{
        static_cast<float32>(mc.width()) + (padding * 2.f),
        static_cast<float32>(mc.height()) + (padding * 2.f),
        static_cast<float32>(mc.depth()) + (padding * 2.f),
    };

    engine_->get_renderer().draw_box(box_matrix, box_size, col);
}

}  // namespace vw::sculptor
