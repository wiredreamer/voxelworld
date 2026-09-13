module vw.sculptor;

import std;

import vw.core;
import vw.ecs;
import vw.world;
import vw.platform;
import vw.gfx;

namespace vw::sculptor {

select_entity_tool::select_entity_tool(
    engine_type& eng, app_state& st
)
    : engine_(&eng), state_(&st), hovered_entity_(ecs::invalid_entity) {}

auto select_entity_tool::render(
    [[maybe_unused]] float delta_time
) -> void {
    const bool has_selected =
        state_->scene.name_to_entity.contains(state_->scene.selected_name);

    if (has_selected) {
        const auto selected_ent = state_->scene.name_to_entity[state_->scene.selected_name];
        draw_entity_box_(selected_ent, colors::green_4);
    }

    if (hovered_entity_.is_valid() &&
        !(has_selected &&
          hovered_entity_ == state_->scene.name_to_entity[state_->scene.selected_name])) {
        draw_entity_box_(hovered_entity_, colors::black);
    }
}

auto select_entity_tool::on_key_press(
    [[maybe_unused]] const plat::key_press_event& ev
) -> void {}

auto select_entity_tool::on_mouse_move(
    [[maybe_unused]] const plat::mouse_move_event& ev
) -> void {
    update_hovered_entity_();
}

auto select_entity_tool::on_mouse_press(
    const plat::mouse_press_event& ev
) -> void {
    if (ev.button != plat::mouse::buttons::LEFT) {
        return;
    }

    if (!hovered_entity_.is_valid()) {
        return;
    }

    if (state_->scene.entity_to_name.contains(hovered_entity_)) {
        state_->scene.selected_name = state_->scene.entity_to_name[hovered_entity_];
    }
}

auto select_entity_tool::on_mouse_release(
    [[maybe_unused]] const plat::mouse_release_event& ev
) -> void {}

auto select_entity_tool::on_activate() -> void {
    update_hovered_entity_();
}

auto select_entity_tool::update_hovered_entity_() -> void {
    const auto& world  = engine_->get_world();
    const auto& window = engine_->get_window();
    const auto& camera = engine_->get_camera();

    const auto ray = camera.screen_to_world_ray(window.get_cursor_pos(), window.get_size());
    const auto hit = world.system<ecs::spatial_system>().voxel_ray_cast(ray, ray_cast_entities_);

    if (!hit) {
        hovered_entity_ = ecs::invalid_entity;
        return;
    }

    hovered_entity_ = hit->ent;
}

auto select_entity_tool::draw_entity_box_(
    ecs::entity ent, color col
) -> void {
    auto& world = engine_->get_world();

    const bool is_renderable =
        world.has<ecs::transform_component>(ent) &&
        world.has<ecs::model_component>(ent);
    if (!is_renderable) {
        return;
    }

    const auto& tc = world.get<ecs::transform_component>(ent);
    const auto& mc = world.get<ecs::model_component>(ent);
    if (!mc.has_model()) {
        return;
    }

    const auto model_size = vec3f{
        static_cast<float32>(mc.width()),
        static_cast<float32>(mc.height()),
        static_cast<float32>(mc.depth())
    };

    const auto padding = 0.1f;
    const auto offset  = vec3f{-padding, -padding, -padding};

    const auto box_matrix =
        tc.get_world_matrix() *
        math::translation_matrix(offset);

    const auto box_size = vec3f{
        model_size.x + padding * 2.f,
        model_size.y + padding * 2.f,
        model_size.z + padding * 2.f
    };

    auto& renderer = engine_->get_renderer();
    renderer.draw_box(box_matrix, box_size, col);
}

}  // namespace vw::sculptor
