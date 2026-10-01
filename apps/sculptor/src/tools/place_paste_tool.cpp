module vw.sculptor;

import std;

import vw.core;
import vw.asset;
import vw.ecs;
import vw.world;
import vw.platform;
import vw.gfx;

namespace vw::sculptor {

place_paste_tool::place_paste_tool(
    engine_type& eng, app_state& st, operation_manager& op_manager
)
    : engine_(&eng)
    , state_(&st)
    , gizmo_(eng, st, op_manager, gizmo_target::fragment, gizmo_commit::preview) {}

auto place_paste_tool::target_entity_() const -> ecs::entity {
    if (!state_->paste.active()) {
        return ecs::invalid_entity;
    }

    const auto it = state_->scene.name_to_entity.find(state_->paste.node_name);
    if (it == state_->scene.name_to_entity.end()) {
        return ecs::invalid_entity;
    }

    return engine_->get_world().has<ecs::transform_component>(it->second) ? it->second
                                                                           : ecs::invalid_entity;
}

auto place_paste_tool::render(
    float
) -> void {
    const auto ent = target_entity_();
    if (!ent.is_valid()) {
        return;
    }

    const auto& paste = state_->paste;
    const auto& node  = engine_->get_world().get<ecs::transform_component>(ent);

    const auto corner = vec3f{
        static_cast<float32>(paste.origin.x),
        static_cast<float32>(paste.origin.y),
        static_cast<float32>(paste.origin.z),
    };
    const auto size = vec3f{
        static_cast<float32>(paste.clip.size.x),
        static_cast<float32>(paste.clip.size.y),
        static_cast<float32>(paste.clip.size.z),
    };

    engine_->get_renderer().draw_box(
        paste.base_matrix(node.get_world_matrix()) * math::translation_matrix(corner), size,
        colors::white
    );

    gizmo_.render(ent);
}

auto place_paste_tool::on_key_press(
    const plat::key_press_event&
) -> void {}

auto place_paste_tool::on_mouse_move(
    const plat::mouse_move_event&
) -> void {
    const bool holds_button =
        engine_->get_window().is_mouse_button_pressed(plat::mouse::buttons::LEFT);
    if (gizmo_.is_dragging() && !holds_button) {
        gizmo_.on_mouse_release();
    }

    const auto ent = target_entity_();
    if (ent.is_valid()) {
        gizmo_.on_mouse_move(ent);
    }
}

auto place_paste_tool::on_mouse_press(
    const plat::mouse_press_event& ev
) -> void {
    if (ev.button != plat::mouse::buttons::LEFT) {
        return;
    }

    const auto ent = target_entity_();
    if (ent.is_valid()) {
        gizmo_.on_mouse_press(ent);
    }
}

auto place_paste_tool::on_mouse_release(
    const plat::mouse_release_event& ev
) -> void {
    if (ev.button == plat::mouse::buttons::LEFT) {
        gizmo_.on_mouse_release();
    }
}

auto place_paste_tool::on_activate() -> void {
    gizmo_.on_mouse_release();
}

}  // namespace vw::sculptor
