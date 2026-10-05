module;

#include <imgui.h>

module vw.arena;

import std;

import vw.core;
import vw.ecs;
import vw.world;
import vw.game;
import vw.platform;
import vw.gfx;

namespace vw::arena {
namespace {

auto bound_inputs(const game::input_bindings& bindings, game::input_action action) -> std::string {
    std::string out;
    const auto append = [&out](std::string_view name) {
        if (!out.empty()) {
            out += ", ";
        }
        out += name;
    };

    for (const auto& binding : bindings.key_actions) {
        if (binding.action == action) {
            append(keyboard::key_name(binding.key));
        }
    }
    for (const auto& binding : bindings.button_actions) {
        if (binding.action == action) {
            append(mouse::button_name(binding.button));
        }
    }
    return out;
}

auto render_input_state(ecs::world& world, ecs::entity player) -> void {
    const auto* input = world.try_get<game::player_input_component>(player);
    if (input == nullptr) {
        return;
    }

    const auto& frame    = input->get_frame();
    const auto& bindings = world.system<game::input_system>().mapper().bindings();

    ImGui::Text("Input:");
    ImGui::Text("  move %+.2f %+.2f", frame.move_forward, frame.move_right);
    ImGui::Text("  look yaw %.1f pitch %.1f", frame.look_yaw_degrees, frame.look_pitch_degrees);

    for (std::size_t i = 0; i < game::input_action_count; ++i) {
        const auto action = static_cast<game::input_action>(i);
        const auto name   = game::input_action_name(action);
        const auto keys   = bound_inputs(bindings, action);

        if (frame.is_held(action)) {
            ImGui::Text(
                "  %-14.*s %-14s held %.2f s", static_cast<int>(name.size()), name.data(),
                keys.c_str(), input->hold_seconds(action)
            );
        } else {
            ImGui::TextDisabled(
                "  %-14.*s %-14s", static_cast<int>(name.size()), name.data(), keys.c_str()
            );
        }
    }
}

auto render_movement_tuning(ecs::world& world) -> void {
    auto& tuning = world.system<game::player_system>().tuning();

    ImGui::Text("Movement tuning:");
    ImGui::SliderFloat("turn, deg/s", &tuning.run_turn_degrees_per_second, 90.0f, 1440.0f, "%.0f");
    ImGui::SliderFloat(
        "attack turn, deg/s", &tuning.attack_turn_degrees_per_second, 360.0f, 4000.0f, "%.0f"
    );
    ImGui::SliderFloat("acceleration, s", &tuning.acceleration_seconds, 0.0f, 1.0f, "%.2f");
    ImGui::SliderFloat("deceleration, s", &tuning.deceleration_seconds, 0.0f, 1.0f, "%.2f");
    ImGui::SliderFloat("lean forward, deg", &tuning.lean_forward_degrees, 0.0f, 20.0f, "%.1f");
    ImGui::SliderFloat("lean back, deg", &tuning.lean_back_degrees, 0.0f, 20.0f, "%.1f");
    ImGui::SliderFloat("lean side, deg", &tuning.lean_side_degrees, 0.0f, 25.0f, "%.1f");
    ImGui::SliderFloat("lean follow, s", &tuning.lean_follow_seconds, 0.0f, 0.5f, "%.2f");
    ImGui::SliderFloat("input buffer, s", &tuning.input_buffer_seconds, 0.0f, 0.5f, "%.2f");
    ImGui::SliderFloat("coyote time, s", &tuning.coyote_seconds, 0.0f, 0.5f, "%.2f");
    ImGui::SliderFloat("lunge time, s", &tuning.lunge_seconds, 0.0f, 0.5f, "%.2f");
    ImGui::SliderFloat("lunge distance", &tuning.lunge_distance, 0.0f, 40.0f, "%.1f");
    if (ImGui::Button("defaults")) {
        tuning = game::movement_tuning{};
    }
}

}  // namespace

auto render_debug_hud(
    const gfx::engine& engine,
    ecs::entity player,
    const gfx::third_person_camera_controller& camera_controller,
    bool show_colliders
) -> void {
    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    const auto window_pos         = ImVec2(viewport->WorkPos.x + 10, viewport->WorkPos.y + 10);
    ImGui::SetNextWindowPos(window_pos, ImGuiCond_Always, ImVec2(0.0f, 0.0f));

    constexpr ImGuiWindowFlags window_flags =
        ImGuiWindowFlags_NoCollapse |
        ImGuiWindowFlags_NoResize |
        ImGuiWindowFlags_NoScrollbar |
        ImGuiWindowFlags_AlwaysAutoResize |
        ImGuiWindowFlags_NoSavedSettings |
        ImGuiWindowFlags_NoNav |
        ImGuiWindowFlags_NoFocusOnAppearing;

    ImGui::Begin("Arena", nullptr, window_flags);
    ImGui::Text("Mouse - rotate camera");
    ImGui::Text("Scroll - zoom");
    ImGui::Text("F1 - toggle cursor");
    ImGui::Text("F2 - toggle colliders");
    ImGui::Text("F3 - test impulse");
    ImGui::Text("ESC - exit");
    ImGui::Separator();

    auto& world = engine.get_world();

    render_input_state(world, player);
    ImGui::Separator();

    render_movement_tuning(world);
    ImGui::Separator();

    if (!world.system<game::surface_placement_system>().is_waiting(player)) {
        const auto player_ent = player;
        const auto& tc        = world.get<ecs::transform_component>(player_ent);
        const auto pos        = tc.get_position();
        ImGui::Text("Position: (%.1f, %.1f, %.1f)", pos.x, pos.y, pos.z);

        const auto& rb = world.get<ecs::rigid_body_component>(player_ent);
        const auto vel = rb.get_velocity();
        ImGui::Text("Velocity: (%.1f, %.1f, %.1f)", vel.x, vel.y, vel.z);
        const auto imp = rb.get_impulse();
        ImGui::Text("Impulse: (%.1f, %.1f, %.1f)", imp.x, imp.y, imp.z);
        ImGui::Text("Grounded: %s", rb.is_grounded() ? "yes" : "no");
        ImGui::Text("Frozen: %s", rb.is_frozen() ? "yes" : "no");

        ImGui::Text("Arm length: %.1f", camera_controller.get_actual_arm_length());

        ImGui::Separator();
        ImGui::Text("Animation FSM:");
        const auto& fsm_comp = world.get<ecs::animation_fsm_component>(player_ent);
        for (std::size_t i = 0; i < fsm_comp.machine_count(); ++i) {
            const auto& state = fsm_comp.get_machine(i).get_current_state();
            ImGui::Text("  Layer %zu: %s", i, state.c_str());
        }

        ImGui::Separator();
        const auto& fighter = world.get<game::player_component>(player_ent);
        ImGui::Text(
            "Sword: %s%s%s", fighter.has_weapon() ? "equipped" : "none",
            fighter.is_swinging() ? ", swinging" : "", fighter.is_hitting() ? ", HIT" : ""
        );
        ImGui::Text("Colliders: %s", show_colliders ? "visible" : "hidden");
    }

    ImGui::End();
}

}  // namespace vw::arena
