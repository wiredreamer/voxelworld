module vw.sculptor;

import std;

import vw.core;
import vw.ecs;
import vw.world;
import vw.platform;
import vw.gfx;

namespace vw::sculptor {

remove_voxel_tool::remove_voxel_tool(
    engine_type& eng, app_state& st, operation_manager& op_manager
)
    : engine_(&eng), state_(&st), op_manager_(&op_manager) {}

auto remove_voxel_tool::render(
    float /*delta_time*/
) -> void {
    if (hovered_voxel_ == vec3i{-1, -1, -1}) {
        return;
    }

    if (!state_->scene.name_to_entity.contains(state_->scene.selected_name)) {
        return;
    }

    auto ent = state_->scene.name_to_entity[state_->scene.selected_name];

    auto& world        = engine_->get_world();
    bool is_renderable =  //
        world.has<ecs::transform_component>(ent) &&
        world.has<ecs::model_component>(ent);
    if (!is_renderable) {
        return;
    }

    auto& renderer = engine_->get_renderer();
    auto voxel_local_pos = vec3f{
        static_cast<float>(hovered_voxel_.x),
        static_cast<float>(hovered_voxel_.y),
        static_cast<float>(hovered_voxel_.z)
    };

    auto voxel_world_pos =  //
        ecs::model_matrix(
            world.get<ecs::transform_component>(ent), world.get<ecs::model_component>(ent)
        ) *
        math::translation_matrix(voxel_local_pos) *         //
        math::scale_matrix(vec3f{1.1f, 1.1f, 1.1f}) *  //
        math::translation_matrix(vec3f{-0.05f, -0.05f, -0.05f});

    renderer.draw_box(voxel_world_pos, vec3f{1.f, 1.f, 1.f}, colors::black);
}

auto remove_voxel_tool::on_key_press(
    const plat::key_press_event& /*ev*/
) -> void {
}

auto remove_voxel_tool::on_mouse_move(
    const plat::mouse_move_event& /*ev*/
) -> void {
   update_hovered_voxel_();
}

auto remove_voxel_tool::on_mouse_press(
    const plat::mouse_press_event& ev
) -> void {
    if (ev.button == plat::mouse::buttons::LEFT) {
        if (hovered_voxel_ == vec3i{-1, -1, -1}) {
            return;
        }

        if (!state_->scene.name_to_entity.contains(state_->scene.selected_name)) {
            return;
        }

        remove_voxel_params params = {
            .name = state_->scene.selected_name,
            .position = hovered_voxel_,
        };

        auto op = std::make_unique<remove_voxel_operation>(
            *engine_, *state_, params
        );
        op_manager_->execute(std::move(op));

        update_hovered_voxel_();
    }
}

auto remove_voxel_tool::on_mouse_release(
    const plat::mouse_release_event& /*ev*/
) -> void {}

auto remove_voxel_tool::on_activate() -> void {
    update_hovered_voxel_();
}

auto remove_voxel_tool::update_hovered_voxel_() -> void {
    const auto& world  = engine_->get_world();
    const auto& window = engine_->get_window();
    const auto& camera = engine_->get_camera();

    const auto ray = camera.screen_to_world_ray(window.get_cursor_pos(), window.get_size());
    const auto hit = world.system<ecs::spatial_system>().voxel_ray_cast(ray, ray_cast_entities_);
    if (!hit) {
        hovered_voxel_ = vec3i{-1, -1, -1};
        return;
    }

    const bool is_selected_entity =  //
        state_->scene.name_to_entity.contains(state_->scene.selected_name) &&
        hit->ent == state_->scene.name_to_entity[state_->scene.selected_name];
    if (is_selected_entity) {
        hovered_voxel_ = hit->voxel_pos;
    }
}

}  // namespace vw::sculptor
