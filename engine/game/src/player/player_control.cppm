export module vw.game:player.control;

import std;

import vw.core;
import vw.asset;
import vw.ecs;
import vw.world;

export namespace vw::game {

class player_system;

enum class air_state : uint8 {
    ground,
    rising,
    falling,
};

enum class dodge_kind : uint8 {
    roll,
    dash,
};

struct movement_tuning final {
    float32 run_turn_degrees_per_second    = 360.0f;
    float32 attack_turn_degrees_per_second = 800.0f;
    float32 acceleration_seconds           = 0.15f;
    float32 deceleration_seconds           = 0.2f;
    float32 lean_forward_degrees           = 5.0f;
    float32 lean_back_degrees              = 4.0f;
    float32 lean_side_degrees              = 8.0f;
    float32 lean_follow_seconds            = 0.08f;
    float32 head_turn_degrees              = 35.0f;
    float32 head_follow_seconds            = 0.12f;
    float32 input_buffer_seconds           = 0.15f;
    float32 coyote_seconds                 = 0.1f;
    float32 fall_after_seconds             = 0.1f;
    float32 hard_landing_speed             = 155.0f;
    float32 stride_voxels                  = 5.0f;
    float32 stride_lead_pitch_degrees      = 15.0f;
    float32 stride_trail_pitch_degrees     = 25.0f;
    float32 stride_arm_swing_voxels        = 3.0f;
    float32 stride_follow_seconds          = 0.1f;
    float32 roll_distance                  = 100.0f;
    float32 roll_seconds                   = 0.75f;
    float32 roll_recovery_seconds          = 0.05f;
    float32 roll_pivot_height              = 13.0f;
    float32 roll_dive_seconds              = 0.2f;
    float32 roll_dive_degrees              = 40.0f;
    float32 roll_dive_lift                 = 1.5f;
    float32 roll_height_rise_seconds       = 0.02f;
    float32 roll_height_fall_seconds       = 0.06f;
    dodge_kind dodge                       = dodge_kind::roll;
    float32 dash_distance                  = 75.0f;
    float32 dash_seconds                   = 0.25f;
    float32 dash_recovery_seconds          = 0.05f;
    uint32 dodge_charges                   = 1;
    float32 dodge_recharge_seconds         = 1.2f;
    float32 lunge_seconds                  = 0.15f;
    float32 lunge_distance                 = 20.0f;
    float32 finisher_lunge_distance        = 40.0f;
    float32 chain_reset_seconds            = 0.3f;
    float32 attack_playback_rate           = 0.75f;
    float32 guard_speed_scale              = 0.5f;
    float32 guard_back_speed_scale         = 0.35f;
    float32 guard_side_speed_scale         = 0.35f;
    float32 guard_turn_degrees_per_second  = 720.0f;
    float32 stance_step_speed              = 40.0f;
    float32 stance_turn_step_degrees       = 35.0f;
    float32 stance_turn_follow_degrees     = 10.0f;
    float32 stance_turn_step_seconds       = 0.15f;
    float32 stance_turn_step_lift          = 1.5f;
};

struct player_component final {
    [[nodiscard]] auto has_weapon() const -> bool {
        return weapon_.is_valid();
    }

    [[nodiscard]] auto has_shield() const -> bool {
        return shield_.is_valid();
    }

    [[nodiscard]] auto is_guarding() const -> bool {
        return guarding_;
    }

    [[nodiscard]] auto get_blocked_hits() const -> uint32 {
        return blocked_hits_;
    }

    [[nodiscard]] auto get_foot_twist_degrees(std::size_t foot) const -> float32 {
        return math::degrees(feet_[foot].twist);
    }

    [[nodiscard]] auto is_foot_stepping(std::size_t foot) const -> bool {
        return feet_[foot].step_elapsed >= 0.0f;
    }

    [[nodiscard]] auto get_turn_steps() const -> uint32 {
        return turn_steps_;
    }

    [[nodiscard]] auto is_attacking() const -> bool {
        return attacking_;
    }

    [[nodiscard]] auto is_swinging() const -> bool {
        return swinging_;
    }

    [[nodiscard]] auto is_hitting() const -> bool {
        return hit_window_;
    }

    [[nodiscard]] auto can_cancel() const -> bool {
        return cancel_open_;
    }

    [[nodiscard]] auto get_swing_count() const -> uint32 {
        return swing_count_;
    }

    [[nodiscard]] auto get_chain_step() const -> uint32 {
        return chain_step_;
    }

    [[nodiscard]] auto get_attack_direction() const -> const vec3f& {
        return attack_facing_;
    }

    [[nodiscard]] auto get_pose() const -> ecs::entity {
        return pose_;
    }

    [[nodiscard]] auto get_lean_forward_degrees() const -> float32 {
        return lean_forward_degrees_;
    }

    [[nodiscard]] auto get_lean_right_degrees() const -> float32 {
        return lean_right_degrees_;
    }

    [[nodiscard]] auto get_head_yaw_degrees() const -> float32 {
        return head_yaw_degrees_;
    }

    [[nodiscard]] auto get_head() const -> ecs::entity {
        return head_;
    }

    [[nodiscard]] auto get_air_state() const -> air_state {
        return air_state_;
    }

    [[nodiscard]] auto get_soft_landings() const -> uint32 {
        return soft_landings_;
    }

    [[nodiscard]] auto get_hard_landings() const -> uint32 {
        return hard_landings_;
    }

    [[nodiscard]] auto is_body_locked() const -> bool {
        return body_locked_;
    }

    [[nodiscard]] auto get_stride() const -> float32 {
        return stride_;
    }

    [[nodiscard]] auto is_dodging() const -> bool {
        return dodging_;
    }

    [[nodiscard]] auto is_rolling() const -> bool {
        return dodging_ && dodge_kind_ == dodge_kind::roll;
    }

    [[nodiscard]] auto is_dashing() const -> bool {
        return dodging_ && dodge_kind_ == dodge_kind::dash;
    }

    [[nodiscard]] auto get_dodge_kind() const -> dodge_kind {
        return dodge_kind_;
    }

    [[nodiscard]] auto get_dodge_direction() const -> const vec3f& {
        return dodge_facing_;
    }

    [[nodiscard]] auto is_invulnerable() const -> bool {
        return invulnerable_;
    }

    [[nodiscard]] auto get_dodge_charges() const -> uint32 {
        return dodge_charges_;
    }

    [[nodiscard]] auto get_dodge_recharge_left() const -> float32 {
        return dodge_recharge_left_;
    }

    [[nodiscard]] auto get_dodge_count() const -> uint32 {
        return dodge_count_;
    }

private:
    friend class player_system;

    struct planted_foot {
        float32 plant_yaw    = 0.0f;
        float32 twist        = 0.0f;
        float32 step_from    = 0.0f;
        float32 step_elapsed = -1.0f;
        float32 lift         = 0.0f;
        vec3f turn_offset{0.0f, 0.0f, 0.0f};
        vec3f anchor{0.0f, 0.0f, 0.0f};
    };

    ecs::entity pose_;
    ecs::entity body_;
    ecs::entity head_;
    ecs::entity hand_right_;
    ecs::entity hand_left_;
    ecs::entity foot_right_;
    ecs::entity foot_left_;
    ecs::entity weapon_;
    ecs::entity shield_;

    vec3f attack_facing_{0.0f, 0.0f, 0.0f};
    vec3f previous_planar_velocity_{0.0f, 0.0f, 0.0f};
    float32 lean_forward_degrees_ = 0.0f;
    float32 lean_right_degrees_   = 0.0f;
    float32 head_yaw_degrees_     = 0.0f;
    float32 swing_seconds_        = 0.0f;
    float32 lunge_seconds_        = 0.0f;
    float32 attack_buffered_      = -1.0f;
    float32 jump_buffered_        = -1.0f;
    uint32 seen_jump_count_       = 0;
    uint32 swing_count_           = 0;
    uint32 chain_step_            = 0;
    float32 since_swing_seconds_  = 0.0f;
    air_state air_state_          = air_state::ground;
    float32 fall_speed_           = 0.0f;
    float32 body_locked_seconds_  = 0.0f;
    uint32 soft_landings_         = 0;
    uint32 hard_landings_         = 0;
    bool body_locked_             = false;
    float32 stride_               = 0.0f;
    bool pushed_with_left_        = true;
    vec3f dodge_facing_{0.0f, 0.0f, 1.0f};
    float32 dodge_elapsed_        = 0.0f;
    dodge_kind dodge_kind_        = dodge_kind::roll;
    float32 roll_height_          = 0.0f;
    float32 dodge_buffered_       = -1.0f;
    float32 dodge_recharge_left_  = 0.0f;
    uint32 dodge_charges_         = 1;
    uint32 dodge_count_           = 0;
    bool dodging_                 = false;
    bool invulnerable_            = false;
    bool guarding_                = false;
    std::array<planted_foot, 2> feet_{};
    uint32 turn_steps_            = 0;
    uint32 blocked_hits_          = 0;
    bool attacking_     = false;
    bool swinging_      = false;
    bool swing_started_ = false;
    bool lunging_       = false;
    bool lunged_        = false;
    bool hit_window_    = false;
    bool cancel_open_   = false;
};

class player_system final {
public:
    static constexpr std::string_view system_name = "player";

    player_system(ecs::world& w, asset::asset_storage& assets);

    auto update(float32 delta_time) -> void;

    [[nodiscard]] auto spawn() -> ecs::entity;

    auto toggle_weapon(ecs::entity player) -> void;

    auto take_hit_on_shield(ecs::entity player) -> bool;

    [[nodiscard]] auto tuning() -> movement_tuning& {
        return tuning_;
    }

private:
    [[nodiscard]] auto create_body_part_(ecs::entity parent, std::string_view part_name) const
        -> ecs::entity;
    auto attach_machines_(ecs::entity root) const -> void;
    auto lean_(ecs::entity ent, player_component& state, float32 delta_time) const -> void;
    auto swing_legs_(ecs::entity ent, player_component& state, float32 delta_time) const -> void;
    auto plant_feet_(ecs::entity ent, player_component& state, float32 delta_time) const -> void;
    [[nodiscard]] auto lowest_point_(const player_component& state, const quat& turn) const
        -> float32;
    auto turn_head_(
        ecs::entity ent, player_component& state, const vec3f& look, float32 delta_time
    ) const -> void;
    static auto read_action_events_(player_component& state, const ecs::animation_player_component& layers)
        -> void;
    auto recharge_dodge_(player_component& state, float32 delta_time) const -> void;
    auto pace_stance_steps_(ecs::entity ent) const -> void;
    [[nodiscard]] auto hold_in_socket_(
        ecs::entity hand, std::string_view socket, std::string_view prefab
    ) const -> ecs::entity;
    static auto end_swing_(player_component& state) -> void;

    ecs::world* world_;
    asset::asset_storage* assets_;
    std::vector<ecs::entity> toggling_;
    movement_tuning tuning_;
};

}  // namespace vw::game
