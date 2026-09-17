module vw.sculptor;

import std;

import vw.core;
import vw.asset;
import vw.ecs;
import vw.world;
import vw.platform;
import vw.gfx;

namespace vw::sculptor {

move_pivot_tool::move_pivot_tool(
    engine_type& eng, app_state& st, operation_manager& op_manager
)
    : engine_(&eng), state_(&st), gizmo_(eng, st, op_manager, gizmo_target::pivot) {}

auto move_pivot_tool::target_entity_() const -> ecs::entity {
    const auto it = state_->scene.name_to_entity.find(state_->edited_node());
    if (it == state_->scene.name_to_entity.end()) {
        return ecs::invalid_entity;
    }

    return engine_->get_world().has<ecs::model_component>(it->second) ? it->second :
                                                                       ecs::invalid_entity;
}

auto move_pivot_tool::render(
    [[maybe_unused]] float delta_time
) -> void {
    const auto ent = target_entity_();
    if (ent.is_valid()) {
        gizmo_.render(ent);
    }
}

auto move_pivot_tool::on_key_press(
    const plat::key_press_event&
) -> void {}

auto move_pivot_tool::on_mouse_move(
    const plat::mouse_move_event&
) -> void {
    const auto ent = target_entity_();
    if (ent.is_valid()) {
        gizmo_.on_mouse_move(ent);
    }
}

auto move_pivot_tool::on_mouse_press(
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

auto move_pivot_tool::on_mouse_release(
    const plat::mouse_release_event& ev
) -> void {
    if (ev.button == plat::mouse::buttons::LEFT) {
        gizmo_.on_mouse_release();
    }
}

auto move_pivot_tool::on_activate() -> void {}

}  // namespace vw::sculptor
