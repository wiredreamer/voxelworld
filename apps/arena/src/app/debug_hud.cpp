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

constexpr std::string_view game_menu = "Game";

using tuning_field = float32 game::movement_tuning::*;

auto defaults_button(game::movement_tuning& tuning, std::initializer_list<tuning_field> fields)
    -> void {
    if (!ImGui::Button("defaults")) {
        return;
    }
    const game::movement_tuning defaults{};
    for (const tuning_field field : fields) {
        tuning.*field = defaults.*field;
    }
}

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

auto render_input_panel(ecs::world& world, ecs::entity player) -> void {
    const auto* input = world.try_get<game::player_input_component>(player);
    if (input == nullptr) {
        return;
    }

    const auto& frame    = input->get_frame();
    const auto& bindings = world.system<game::input_system>().mapper().bindings();

    ImGui::Text("move %+.2f %+.2f", frame.move_forward, frame.move_right);
    ImGui::Text("look yaw %.1f pitch %.1f", frame.look_yaw_degrees, frame.look_pitch_degrees);

    for (std::size_t i = 0; i < game::input_action_count; ++i) {
        const auto action = static_cast<game::input_action>(i);
        const auto name   = game::input_action_name(action);
        const auto keys   = bound_inputs(bindings, action);

        if (frame.is_held(action)) {
            ImGui::Text(
                "%-14.*s %-14s held %.2f s", static_cast<int>(name.size()), name.data(),
                keys.c_str(), input->hold_seconds(action)
            );
        } else {
            ImGui::TextDisabled(
                "%-14.*s %-14s", static_cast<int>(name.size()), name.data(), keys.c_str()
            );
        }
    }
}

auto render_body_panel(
    ecs::world& world,
    ecs::entity player,
    const gfx::third_person_camera_controller& camera_controller
) -> void {
    if (world.system<game::surface_placement_system>().is_waiting(player)) {
        ImGui::TextDisabled("waiting for the ground");
        return;
    }

    const auto pos = world.get<ecs::transform_component>(player).get_position();
    ImGui::Text("Position: (%.1f, %.1f, %.1f)", pos.x, pos.y, pos.z);

    const auto& rb = world.get<ecs::rigid_body_component>(player);
    const auto vel = rb.get_velocity();
    ImGui::Text("Velocity: (%.1f, %.1f, %.1f)", vel.x, vel.y, vel.z);
    const auto imp = rb.get_impulse();
    ImGui::Text("Impulse: (%.1f, %.1f, %.1f)", imp.x, imp.y, imp.z);
    ImGui::Text("Grounded: %s", rb.is_grounded() ? "yes" : "no");
    ImGui::Text("Frozen: %s", rb.is_frozen() ? "yes" : "no");
    ImGui::Text("Arm length: %.1f", camera_controller.get_actual_arm_length());
}

auto render_run_panel(game::movement_tuning& tuning) -> void {
    ImGui::SliderFloat("turn, deg/s", &tuning.run_turn_degrees_per_second, 90.0f, 1440.0f, "%.0f");
    ImGui::SliderFloat("acceleration, s", &tuning.acceleration_seconds, 0.0f, 1.0f, "%.2f");
    ImGui::SliderFloat("deceleration, s", &tuning.deceleration_seconds, 0.0f, 1.0f, "%.2f");
    defaults_button(
        tuning,
        {&game::movement_tuning::run_turn_degrees_per_second,
         &game::movement_tuning::acceleration_seconds,
         &game::movement_tuning::deceleration_seconds}
    );
}

auto render_lean_panel(game::movement_tuning& tuning) -> void {
    ImGui::SliderFloat("lean forward, deg", &tuning.lean_forward_degrees, 0.0f, 20.0f, "%.1f");
    ImGui::SliderFloat("lean back, deg", &tuning.lean_back_degrees, 0.0f, 20.0f, "%.1f");
    ImGui::SliderFloat("lean side, deg", &tuning.lean_side_degrees, 0.0f, 25.0f, "%.1f");
    ImGui::SliderFloat("lean follow, s", &tuning.lean_follow_seconds, 0.0f, 0.5f, "%.2f");
    ImGui::SliderFloat("head turn, deg", &tuning.head_turn_degrees, 0.0f, 90.0f, "%.0f");
    ImGui::SliderFloat("head follow, s", &tuning.head_follow_seconds, 0.0f, 0.5f, "%.2f");
    defaults_button(
        tuning,
        {&game::movement_tuning::lean_forward_degrees, &game::movement_tuning::lean_back_degrees,
         &game::movement_tuning::lean_side_degrees, &game::movement_tuning::lean_follow_seconds,
         &game::movement_tuning::head_turn_degrees, &game::movement_tuning::head_follow_seconds}
    );
}

auto render_jump_panel(game::movement_tuning& tuning) -> void {
    ImGui::SliderFloat("input buffer, s", &tuning.input_buffer_seconds, 0.0f, 0.5f, "%.2f");
    ImGui::SliderFloat("coyote time, s", &tuning.coyote_seconds, 0.0f, 0.5f, "%.2f");
    ImGui::SliderFloat("fall after, s", &tuning.fall_after_seconds, 0.0f, 0.5f, "%.2f");
    ImGui::SliderFloat("hard landing, u/s", &tuning.hard_landing_speed, 50.0f, 400.0f, "%.0f");
    ImGui::SliderFloat("stride, voxels", &tuning.stride_voxels, 0.0f, 10.0f, "%.1f");
    ImGui::SliderFloat("stride lead toe, deg", &tuning.stride_lead_pitch_degrees, 0.0f, 45.0f, "%.0f");
    ImGui::SliderFloat("stride trail toe, deg", &tuning.stride_trail_pitch_degrees, 0.0f, 60.0f, "%.0f");
    ImGui::SliderFloat("stride arms, voxels", &tuning.stride_arm_swing_voxels, 0.0f, 8.0f, "%.1f");
    ImGui::SliderFloat("stride follow, s", &tuning.stride_follow_seconds, 0.0f, 0.5f, "%.2f");
    defaults_button(
        tuning,
        {&game::movement_tuning::input_buffer_seconds, &game::movement_tuning::coyote_seconds,
         &game::movement_tuning::fall_after_seconds, &game::movement_tuning::hard_landing_speed,
         &game::movement_tuning::stride_voxels, &game::movement_tuning::stride_lead_pitch_degrees,
         &game::movement_tuning::stride_trail_pitch_degrees,
         &game::movement_tuning::stride_arm_swing_voxels,
         &game::movement_tuning::stride_follow_seconds}
    );
}

auto render_strike_panel(game::movement_tuning& tuning) -> void {
    ImGui::SliderFloat("attack speed", &tuning.attack_playback_rate, 0.3f, 1.5f, "%.2f");
    ImGui::SliderFloat(
        "attack turn, deg/s", &tuning.attack_turn_degrees_per_second, 360.0f, 4000.0f, "%.0f"
    );
    ImGui::SliderFloat("lunge time, s", &tuning.lunge_seconds, 0.0f, 0.5f, "%.2f");
    ImGui::SliderFloat("lunge distance", &tuning.lunge_distance, 0.0f, 40.0f, "%.1f");
    ImGui::SliderFloat("thrust lunge distance", &tuning.finisher_lunge_distance, 0.0f, 60.0f, "%.1f");
    ImGui::SliderFloat("chain reset, s", &tuning.chain_reset_seconds, 0.0f, 1.0f, "%.2f");
    defaults_button(
        tuning,
        {&game::movement_tuning::attack_playback_rate,
         &game::movement_tuning::attack_turn_degrees_per_second,
         &game::movement_tuning::lunge_seconds, &game::movement_tuning::lunge_distance,
         &game::movement_tuning::finisher_lunge_distance,
         &game::movement_tuning::chain_reset_seconds}
    );
}

auto render_dodge_panel(game::movement_tuning& tuning) -> void {
    if (ImGui::RadioButton("roll (light)", tuning.dodge == game::dodge_kind::roll)) {
        tuning.dodge = game::dodge_kind::roll;
    }
    ImGui::SameLine();
    if (ImGui::RadioButton("dash (heavy)", tuning.dodge == game::dodge_kind::dash)) {
        tuning.dodge = game::dodge_kind::dash;
    }

    ImGui::SliderFloat("roll distance", &tuning.roll_distance, 10.0f, 120.0f, "%.0f");
    ImGui::SliderFloat("roll time, s", &tuning.roll_seconds, 0.1f, 1.5f, "%.2f");
    ImGui::SliderFloat("roll recovery, s", &tuning.roll_recovery_seconds, 0.0f, 0.6f, "%.2f");
    ImGui::SliderFloat("roll pivot height", &tuning.roll_pivot_height, 0.0f, 25.0f, "%.1f");
    ImGui::SliderFloat("roll dive, s", &tuning.roll_dive_seconds, 0.0f, 0.4f, "%.2f");
    ImGui::SliderFloat("roll dive, deg", &tuning.roll_dive_degrees, 0.0f, 90.0f, "%.0f");
    ImGui::SliderFloat("roll dive lift", &tuning.roll_dive_lift, 0.0f, 8.0f, "%.1f");
    ImGui::SliderFloat("roll height up, s", &tuning.roll_height_rise_seconds, 0.0f, 0.2f, "%.3f");
    ImGui::SliderFloat("roll height down, s", &tuning.roll_height_fall_seconds, 0.0f, 0.3f, "%.3f");
    ImGui::SliderFloat("dash distance", &tuning.dash_distance, 5.0f, 80.0f, "%.0f");
    ImGui::SliderFloat("dash time, s", &tuning.dash_seconds, 0.05f, 0.8f, "%.2f");
    ImGui::SliderFloat("dash recovery, s", &tuning.dash_recovery_seconds, 0.0f, 0.6f, "%.2f");

    constexpr uint32 fewest_dodge_charges = 1;
    constexpr uint32 most_dodge_charges   = 4;
    ImGui::SliderScalar(
        "dodge charges", ImGuiDataType_U32, &tuning.dodge_charges, &fewest_dodge_charges,
        &most_dodge_charges
    );
    ImGui::SliderFloat("dodge recharge, s", &tuning.dodge_recharge_seconds, 0.0f, 5.0f, "%.2f");

    if (ImGui::Button("defaults")) {
        const game::movement_tuning defaults{};
        tuning.dodge = defaults.dodge;
        tuning.dodge_charges = defaults.dodge_charges;
        for (const tuning_field field :
             {&game::movement_tuning::roll_distance, &game::movement_tuning::roll_seconds,
              &game::movement_tuning::roll_recovery_seconds,
              &game::movement_tuning::roll_pivot_height, &game::movement_tuning::roll_dive_seconds,
              &game::movement_tuning::roll_dive_degrees, &game::movement_tuning::roll_dive_lift,
              &game::movement_tuning::roll_height_rise_seconds,
              &game::movement_tuning::roll_height_fall_seconds,
              &game::movement_tuning::dash_distance, &game::movement_tuning::dash_seconds,
              &game::movement_tuning::dash_recovery_seconds,
              &game::movement_tuning::dodge_recharge_seconds}) {
            tuning.*field = defaults.*field;
        }
    }
}

auto render_guard_tuning(ecs::world& world, ecs::entity player) -> void {
    auto& players = world.system<game::player_system>();
    auto& tuning  = players.tuning();

    ImGui::Text("Guard (hold RMB with the shield):");
    ImGui::SliderFloat("guard speed forward", &tuning.guard_speed_scale, 0.2f, 1.0f, "%.2f");
    ImGui::SliderFloat("guard speed back", &tuning.guard_back_speed_scale, 0.2f, 1.0f, "%.2f");
    ImGui::SliderFloat("guard speed aside", &tuning.guard_side_speed_scale, 0.2f, 1.0f, "%.2f");
    ImGui::SliderFloat("guard turn, deg/s", &tuning.guard_turn_degrees_per_second, 90.0f, 1440.0f, "%.0f");
    ImGui::SliderFloat("stance step pace, u/s", &tuning.stance_step_speed, 10.0f, 100.0f, "%.0f");
    ImGui::SliderFloat("turn step after, deg", &tuning.stance_turn_step_degrees, 10.0f, 90.0f, "%.0f");
    ImGui::SliderFloat("other foot follows after, deg", &tuning.stance_turn_follow_degrees, 0.0f, 45.0f, "%.0f");
    ImGui::SliderFloat("turn step time, s", &tuning.stance_turn_step_seconds, 0.05f, 0.5f, "%.2f");
    ImGui::SliderFloat("turn step lift", &tuning.stance_turn_step_lift, 0.0f, 4.0f, "%.1f");

    ImGui::BeginDisabled(!world.get<game::player_component>(player).is_guarding());
    if (ImGui::Button("hit the shield")) {
        static_cast<void>(players.take_hit_on_shield(player));
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    if (ImGui::Button("defaults")) {
        const game::movement_tuning defaults{};
        tuning.guard_speed_scale             = defaults.guard_speed_scale;
        tuning.guard_back_speed_scale        = defaults.guard_back_speed_scale;
        tuning.guard_side_speed_scale        = defaults.guard_side_speed_scale;
        tuning.guard_turn_degrees_per_second = defaults.guard_turn_degrees_per_second;
        tuning.stance_step_speed             = defaults.stance_step_speed;
        tuning.stance_turn_step_degrees      = defaults.stance_turn_step_degrees;
        tuning.stance_turn_follow_degrees    = defaults.stance_turn_follow_degrees;
        tuning.stance_turn_step_seconds      = defaults.stance_turn_step_seconds;
        tuning.stance_turn_step_lift         = defaults.stance_turn_step_lift;
    }
}

auto render_fighter_state(ecs::world& world, ecs::entity player) -> void {
    const auto& fighter = world.get<game::player_component>(player);
    ImGui::Text(
        "Sword: %s%s%s%s  chain %u/3, swings %u", fighter.has_weapon() ? "equipped" : "none",
        fighter.is_swinging() ? ", swinging" : "", fighter.is_hitting() ? ", HIT" : "",
        fighter.can_cancel() ? ", cancel" : "", fighter.get_chain_step(), fighter.get_swing_count()
    );
    ImGui::Text(
        "Shield: %s%s%s, blocked %u", fighter.has_shield() ? "equipped" : "none",
        fighter.is_in_stance() ? ", STANCE" : "", fighter.is_guarding() ? ", GUARD" : "",
        fighter.get_blocked_hits()
    );
    ImGui::Text(
        "Feet: twist %+.0f / %+.0f deg%s%s, turn steps %u", fighter.get_foot_twist_degrees(0),
        fighter.get_foot_twist_degrees(1), fighter.is_foot_stepping(0) ? ", LEFT STEP" : "",
        fighter.is_foot_stepping(1) ? ", RIGHT STEP" : "", fighter.get_turn_steps()
    );
    ImGui::Text(
        "Landings: soft %u, hard %u%s", fighter.get_soft_landings(), fighter.get_hard_landings(),
        fighter.is_body_locked() ? ", LOCKED" : ""
    );
    ImGui::Text(
        "Dodge: charges %u/%u, recharge %.2f s, dodges %u%s%s", fighter.get_dodge_charges(),
        world.system<game::player_system>().tuning().dodge_charges,
        fighter.get_dodge_recharge_left(), fighter.get_dodge_count(),
        fighter.is_rolling() ? ", ROLL" : fighter.is_dashing() ? ", DASH" : "",
        fighter.is_invulnerable() ? ", IFRAME" : ""
    );

    const auto& machines = world.get<ecs::animation_fsm_component>(player);
    for (std::size_t i = 0; i < machines.machine_count(); ++i) {
        ImGui::Text("Layer %zu: %s", i, machines.get_machine(i).get_current_state().c_str());
    }
}

}  // namespace

auto register_debug_panels(
    gfx::engine& engine,
    ecs::entity player,
    const gfx::third_person_camera_controller& camera_controller
) -> void {
    auto& tool   = engine.get_debug_tool();
    auto& world  = engine.get_world();
    auto& tuning = world.system<game::player_system>().tuning();
    const std::string menu{game_menu};

    tool.add_panel(menu, "Run", [&tuning] { render_run_panel(tuning); });
    tool.add_panel(menu, "Lean and head", [&tuning] { render_lean_panel(tuning); });
    tool.add_panel(menu, "Jump", [&tuning] { render_jump_panel(tuning); });
    tool.add_panel(menu, "Strike", [&tuning] { render_strike_panel(tuning); });
    tool.add_panel(menu, "Dodge", [&tuning] { render_dodge_panel(tuning); });
    tool.add_panel(menu, "Input", [&world, player] { render_input_panel(world, player); });
    tool.add_panel(menu, "Body", [&world, player, &camera_controller] {
        render_body_panel(world, player, camera_controller);
    });
}

auto render_debug_hud(const gfx::engine& engine, ecs::entity player, bool show_colliders) -> void {
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
    ImGui::TextDisabled(
        "F1 cursor  F2 colliders (%s)  F3 impulse  ESC exit", show_colliders ? "on" : "off"
    );
    ImGui::TextDisabled("Other settings: Debug Tool > %.*s", static_cast<int>(game_menu.size()), game_menu.data());
    ImGui::Separator();

    auto& world = engine.get_world();
    if (!world.system<game::surface_placement_system>().is_waiting(player)) {
        render_guard_tuning(world, player);
        ImGui::Separator();
        render_fighter_state(world, player);
    }

    ImGui::End();
}

}  // namespace vw::arena
