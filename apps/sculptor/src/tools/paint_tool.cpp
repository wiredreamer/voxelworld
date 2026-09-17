module vw.sculptor;

import std;

import vw.core;
import vw.ecs;
import vw.world;
import vw.platform;
import vw.gfx;

namespace vw::sculptor {

paint_tool::paint_tool(
    engine_type& eng, app_state& st, operation_manager& op_manager
)
    : engine_(&eng), state_(&st), op_manager_(&op_manager) {}

auto paint_tool::render(
    [[maybe_unused]] float delta_time
) -> void {
    if (hovered_voxel_ == vec3i{-1, -1, -1}) {
        return;
    }

    if (!state_->scene.name_to_entity.contains(state_->edited_node())) {
        return;
    }

    auto ent = state_->scene.name_to_entity[state_->edited_node()];

    auto& world        = engine_->get_world();
    bool is_renderable =  //
        world.has<ecs::transform_component>(ent) &&
        world.has<ecs::model_component>(ent);
    if (!is_renderable) {
        return;
    }

    auto& renderer       = engine_->get_renderer();
    auto voxel_local_pos = vec3f{
        static_cast<float>(hovered_voxel_.x),
        static_cast<float>(hovered_voxel_.y),
        static_cast<float>(hovered_voxel_.z)
    };

    auto voxel_world_pos =  //
        ecs::model_matrix(
            world.get<ecs::transform_component>(ent), world.get<ecs::model_component>(ent)
        ) *
        math::translation_matrix(voxel_local_pos) *       //
        math::scale_matrix(vec3f{1.01f, 1.01f, 1.01f}) *  //
        math::translation_matrix(vec3f{-0.005f, -0.005f, -0.005f});

    renderer.draw_box(voxel_world_pos, vec3f{1.f, 1.f, 1.f}, colors::black);
}

auto paint_tool::on_key_press(
    [[maybe_unused]] const plat::key_press_event& ev
) -> void {}

auto paint_tool::on_mouse_move(
    [[maybe_unused]] const plat::mouse_move_event& ev
) -> void {
    update_hovered_voxel_();
}

auto paint_tool::on_mouse_press(
    const plat::mouse_press_event& ev
) -> void {
    using buttons = plat::mouse::buttons;

    if (ev.button == buttons::LEFT) {
        if (hovered_voxel_ == vec3i{-1, -1, -1}) {
            return;
        }

        if (!state_->scene.name_to_entity.contains(state_->edited_node())) {
            return;
        }

        const auto ent           = state_->scene.name_to_entity[state_->edited_node()];
        auto& world        = engine_->get_world();
        const bool is_renderable =  //
            world.has<ecs::transform_component>(ent) &&
            world.has<ecs::model_component>(ent);
        if (!is_renderable) {
            return;
        }

        const auto& model_comp = world.get<ecs::model_component>(ent);
        const bool has_model   = model_comp.has_model();
        const bool is_same_voxel =
            has_model && model_comp.get_voxel(hovered_voxel_) == state_->tool.selected_voxel;
        if (is_same_voxel) {
            return;
        }

        paint_voxel_params params;
        params.name      = state_->edited_node();
        params.position  = hovered_voxel_;
        params.new_voxel = state_->tool.selected_voxel;

        auto op = std::make_unique<paint_voxel_operation>(*engine_, *state_, params);
        op_manager_->execute(std::move(op));
    }
}

auto paint_tool::on_mouse_release(
    [[maybe_unused]] const plat::mouse_release_event& ev
) -> void {}

auto paint_tool::on_activate() -> void {
    update_hovered_voxel_();
}

auto paint_tool::update_hovered_voxel_() -> void {
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
