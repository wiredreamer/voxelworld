module vw.sculptor;

import std;

import vw.core;
import vw.ecs;
import vw.world;
import vw.platform;
import vw.gfx;

namespace vw::sculptor {

color_picker_tool::color_picker_tool(
    engine_type& eng, app_state& st, operation_manager& op_manager
)
    : engine_(&eng), state_(&st), op_manager_(&op_manager) {}

auto color_picker_tool::render(
    [[maybe_unused]] float delta_time
) -> void {
    const bool is_hovered = hovered_voxel_ != vec3i{-1, -1, -1};
    const bool has_edited_entity =
        state_->scene.name_to_entity.contains(state_->edited_node());
    if (!is_hovered || !has_edited_entity) {
        return;
    }

    auto& world    = engine_->get_world();
    const auto ent = state_->scene.name_to_entity[state_->edited_node()];

    const bool is_renderable =  //
        world.has<ecs::transform_component>(ent) &&
        world.has<ecs::model_component>(ent);
    if (!is_renderable) {
        return;
    }

    const vec3f voxel_local_pos{
        static_cast<float>(hovered_voxel_.x),
        static_cast<float>(hovered_voxel_.y),
        static_cast<float>(hovered_voxel_.z)
    };

    const auto voxel_world_pos =  //
        ecs::model_matrix(
            world.get<ecs::transform_component>(ent), world.get<ecs::model_component>(ent)
        ) *
        math::translation_matrix(voxel_local_pos) *       //
        math::scale_matrix(vec3f{1.01f, 1.01f, 1.01f}) *  //
        math::translation_matrix(vec3f{-0.005f, -0.005f, -0.005f});

    auto& renderer = engine_->get_renderer();
    renderer.draw_box(voxel_world_pos, vec3f{1.f, 1.f, 1.f}, colors::black);
}

auto color_picker_tool::on_key_press(
    [[maybe_unused]] const plat::key_press_event& ev
) -> void {}

auto color_picker_tool::on_mouse_move(
    [[maybe_unused]] const plat::mouse_move_event& ev
) -> void {
    update_hovered_voxel_();
}

auto color_picker_tool::on_mouse_press(
    const plat::mouse_press_event& ev
) -> void {
    if (ev.button != plat::mouse::buttons::LEFT) {
        return;
    }

    const bool is_hovered = hovered_voxel_ != vec3i{-1, -1, -1};
    const bool has_edited_entity =
        state_->scene.name_to_entity.contains(state_->edited_node());
    if (!is_hovered || !has_edited_entity) {
        return;
    }

    auto& world    = engine_->get_world();
    const auto ent = state_->scene.name_to_entity[state_->edited_node()];

    const bool is_renderable =  //
        world.has<ecs::transform_component>(ent) &&
        world.has<ecs::model_component>(ent);
    if (!is_renderable) {
        return;
    }

    const auto& model_comp = world.get<ecs::model_component>(ent);
    if (!model_comp.has_model()) {
        return;
    }

    state_->tool.selected_voxel = model_comp.get_voxel(hovered_voxel_);
}

auto color_picker_tool::on_mouse_release(
    [[maybe_unused]] const plat::mouse_release_event& ev
) -> void {}

auto color_picker_tool::on_activate() -> void {
    update_hovered_voxel_();
}

auto color_picker_tool::update_hovered_voxel_() -> void {
    const auto& world  = engine_->get_world();
    const auto& window = engine_->get_window();
    const auto& camera = engine_->get_camera();

    const auto ray = camera.screen_to_world_ray(window.get_cursor_pos(), window.get_size());
    const auto hit = world.system<ecs::spatial_system>().voxel_ray_cast(ray, ray_cast_entities_);
    if (!hit) {
        hovered_voxel_ = vec3i{-1, -1, -1};
        return;
    }

    const bool is_edited_entity =  //
        state_->scene.name_to_entity.contains(state_->edited_node()) &&
        hit->ent == state_->scene.name_to_entity[state_->edited_node()];
    if (is_edited_entity) {
        hovered_voxel_ = hit->voxel_pos;
    }
}

}  // namespace vw::sculptor
