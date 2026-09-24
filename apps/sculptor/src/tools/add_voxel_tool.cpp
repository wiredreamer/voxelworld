module vw.sculptor;

import std;

import vw.core;
import vw.asset;
import vw.ecs;
import vw.world;
import vw.platform;
import vw.gfx;

namespace vw::sculptor {

add_voxel_tool::add_voxel_tool(
    engine_type& eng, app_state& st, operation_manager& op_manager
)
    : engine_(&eng), state_(&st), op_manager_(&op_manager) {}

auto add_voxel_tool::render(
    float
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

    const auto& model_comp = world.get<ecs::model_component>(ent);
    const bool is_outside  =  //
        hovered_voxel_.x < 0 || hovered_voxel_.x >= model_comp.width() || hovered_voxel_.y < 0 ||
        hovered_voxel_.y >= model_comp.height() || hovered_voxel_.z < 0 ||
        hovered_voxel_.z >= model_comp.depth();

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

    const auto draw_color = is_outside ? colors::red_4 : colors::black;

    auto& renderer = engine_->get_renderer();
    renderer.draw_box(voxel_world_pos, vec3f{1.f, 1.f, 1.f}, draw_color);
}

auto add_voxel_tool::on_key_press(
    const plat::key_press_event&
) -> void {}

auto add_voxel_tool::on_mouse_move(
    const plat::mouse_move_event&
) -> void {
    update_hovered_voxel_();
}

auto add_voxel_tool::on_mouse_press(
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

        const auto ent = state_->scene.name_to_entity[state_->edited_node()];

        auto& world        = engine_->get_world();
        const bool is_renderable =  //
            world.has<ecs::transform_component>(ent) &&
            world.has<ecs::model_component>(ent);
        if (!is_renderable) {
            return;
        }

        const auto& model_comp = world.get<ecs::model_component>(ent);
        const bool is_outside  =  //
            hovered_voxel_.x < 0 || hovered_voxel_.x >= model_comp.width() ||
            hovered_voxel_.y < 0 || hovered_voxel_.y >= model_comp.height() ||
            hovered_voxel_.z < 0 || hovered_voxel_.z >= model_comp.depth();
        if (is_outside) {
            const auto is_x_less    = hovered_voxel_.x < 0;
            const auto is_x_greater = hovered_voxel_.x >= model_comp.width();
            const auto x            = is_x_less ? -1 : is_x_greater ? 1 : 0;

            const auto is_y_less    = hovered_voxel_.y < 0;
            const auto is_y_greater = hovered_voxel_.y >= model_comp.height();
            const auto y            = is_y_less ? -1 : is_y_greater ? 1 : 0;

            const auto is_z_less    = hovered_voxel_.z < 0;
            const auto is_z_greater = hovered_voxel_.z >= model_comp.depth();
            const auto z            = is_z_less ? -1 : is_z_greater ? 1 : 0;

            const auto expand_dir = vec3i{x, y, z};

            expand_model_params expand_params = {
                .name = state_->edited_node(),
                .dir  = expand_dir,
            };
            auto expand_op =
                std::make_unique<expand_model_operation>(*engine_, *state_, expand_params);
            op_manager_->execute(std::move(expand_op));

            update_hovered_voxel_();
            return;
        }

        const auto model = model_comp.get_model();
        add_voxel_params params;
        params.name      = state_->edited_node();
        params.position  = hovered_voxel_;
        params.new_voxel = state_->tool.selected_voxel;

        auto op = std::make_unique<add_voxel_operation>(*engine_, *state_, params);
        op_manager_->execute(std::move(op));

        update_hovered_voxel_();
    }
}

auto add_voxel_tool::on_mouse_release(
    const plat::mouse_release_event&
) -> void {}

auto add_voxel_tool::on_activate() -> void {
    update_hovered_voxel_();
}

auto add_voxel_tool::update_hovered_voxel_() -> void {
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
        hovered_voxel_ = hit->empty_pos;
    }
}

}  // namespace vw::sculptor
