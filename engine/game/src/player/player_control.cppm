export module vw.game:player.control;

import std;

import vw.core;
import vw.asset;
import vw.ecs;
import vw.world;
import :input.mapper;

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

enum class loadout : uint8 {
    unarmed,
    melee,
    bow,
};

// см. docs/ENGINE.md#лук
enum class bow_phase : uint8 {
    rest,
    drawing,
    holding,
    releasing,
};

// см. docs/ENGINE.md#заряженный-удар-и-способности-меча
enum class strike_kind : uint8 {
    none,
    light,
    overhead,
    heavy,
    whirl,
    pommel,
};

enum class blow_kind : uint8 {
    none,
    stagger,
    death,
};

enum class death_fall : uint8 {
    backward,
    forward,
};

enum class whirl_phase : uint8 {
    none,
    gathering,
    spinning,
    braking,
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
    float32 step_hop_voxels                = 1.0f;
    float32 model_sink_seconds             = 0.12f;
    float32 model_air_sink_seconds         = 0.03f;
    float32 model_lift_limit               = 16.0f;
    float32 ride_lead_voxels               = 1.0f;
    float32 ride_rise_speed                = 250.0f;
    float32 ride_sink_speed                = 200.0f;
    float32 fall_after_seconds             = 0.1f;
    float32 jump_impulse                   = 150.0f;
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
    float32 bow_draw_playback_rate         = 1.0f;
    float32 bow_full_draw_seconds          = 0.8f;
    float32 bow_quick_draw_seconds         = 0.25f;
    float32 bow_quick_arrow_speed          = 220.0f;
    float32 bow_full_arrow_speed           = 900.0f;
    float32 bow_aim_reach                  = 4000.0f;
    float32 bow_release_fallback_seconds   = 0.08f;
    float32 charge_decide_seconds          = 0.15f;
    float32 charge_full_seconds            = 0.5f;
    float32 charge_move_scale              = 1.0f;
    float32 charged_lunge_distance         = 25.0f;
    float32 whirl_turns                    = 3.0f;
    float32 whirl_turn_seconds             = 0.3f;
    float32 whirl_gather_seconds           = 0.2f;
    float32 whirl_brake_seconds            = 0.25f;
    float32 whirl_move_scale               = 0.5f;
    float32 whirl_settle_seconds           = 0.06f;
    float32 pommel_lunge_distance          = 25.0f;
};

struct player_component final {
    [[nodiscard]] auto has_weapon() const -> bool {
        return weapon_.is_valid();
    }

    [[nodiscard]] auto has_shield() const -> bool {
        return shield_.is_valid();
    }

    [[nodiscard]] auto has_bow() const -> bool {
        return bow_.is_valid();
    }

    [[nodiscard]] auto has_nocked_arrow() const -> bool {
        return nocked_arrow_.is_valid();
    }

    [[nodiscard]] auto get_loadout() const -> loadout {
        return loadout_;
    }

    [[nodiscard]] auto get_bow_phase() const -> bow_phase {
        return bow_phase_;
    }

    [[nodiscard]] auto get_draw_share() const -> float32 {
        return draw_share_;
    }

    [[nodiscard]] auto get_shot_power() const -> float32 {
        return shot_power_;
    }

    [[nodiscard]] auto get_arrows_loosed() const -> uint32 {
        return arrows_loosed_;
    }

    [[nodiscard]] auto is_aiming() const -> bool {
        return aiming_;
    }

    [[nodiscard]] auto is_braced() const -> bool {
        return braced_;
    }

    [[nodiscard]] auto is_guarding() const -> bool {
        return guarding_;
    }

    [[nodiscard]] auto is_in_stance() const -> bool {
        return in_stance_;
    }

    [[nodiscard]] auto get_blocked_hits() const -> uint32 {
        return blocked_hits_;
    }

    [[nodiscard]] auto get_staggers() const -> uint32 {
        return staggers_;
    }

    [[nodiscard]] auto get_guard_breaks() const -> uint32 {
        return guard_breaks_;
    }

    [[nodiscard]] auto is_dead() const -> bool {
        return dead_;
    }

    [[nodiscard]] auto get_death_fall() const -> death_fall {
        return death_fall_;
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

    [[nodiscard]] auto get_strike() const -> strike_kind {
        return strike_;
    }

    [[nodiscard]] auto is_heavy_strike() const -> bool {
        return strike_ == strike_kind::heavy;
    }

    [[nodiscard]] auto is_charging() const -> bool {
        return charging_;
    }

    [[nodiscard]] auto is_charge_ready() const -> bool {
        return charging_ && charge_ready_;
    }

    [[nodiscard]] auto get_charge_seconds() const -> float32 {
        return charge_seconds_;
    }

    [[nodiscard]] auto get_charge_count() const -> uint32 {
        return charge_count_;
    }

    [[nodiscard]] auto get_heavy_strike_count() const -> uint32 {
        return heavy_strike_count_;
    }

    [[nodiscard]] auto get_whirl_count() const -> uint32 {
        return whirl_count_;
    }

    [[nodiscard]] auto get_pommel_count() const -> uint32 {
        return pommel_count_;
    }

    [[nodiscard]] auto get_whirl_phase() const -> whirl_phase {
        return whirl_phase_;
    }

    [[nodiscard]] auto get_whirl_turn_degrees() const -> float32 {
        return math::degrees(whirl_turn_);
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
    ecs::entity bow_;
    ecs::entity nocked_arrow_;
    loadout loadout_         = loadout::unarmed;
    bow_phase bow_phase_     = bow_phase::rest;
    float32 draw_seconds_    = 0.0f;
    float32 draw_share_      = 0.0f;
    float32 shot_power_      = 0.0f;
    float32 release_seconds_ = 0.0f;
    uint32 arrows_loosed_    = 0;
    bool draw_ready_         = false;
    bool release_fired_      = false;
    bool arrow_loosed_       = false;
    bool bow_cancel_open_    = false;
    bool aiming_             = false;
    bool leapt_              = false;
    bool draw_owed_          = false;
    bool braced_             = false;

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
    bool in_stance_               = false;
    std::array<planted_foot, 2> feet_{};
    uint32 turn_steps_            = 0;
    uint32 blocked_hits_          = 0;
    uint32 staggers_              = 0;
    uint32 guard_breaks_          = 0;
    bool dead_                    = false;
    death_fall death_fall_        = death_fall::backward;
    blow_kind blow_               = blow_kind::none;
    strike_kind strike_        = strike_kind::none;
    float32 charge_seconds_    = 0.0f;
    float32 whirl_buffered_    = -1.0f;
    float32 pommel_buffered_   = -1.0f;
    float32 whirl_spin_elapsed_ = -1.0f;
    float32 whirl_turn_        = 0.0f;
    whirl_phase whirl_phase_   = whirl_phase::none;
    uint32 charge_count_       = 0;
    uint32 heavy_strike_count_ = 0;
    uint32 whirl_count_        = 0;
    uint32 pommel_count_       = 0;
    bool charge_watch_         = false;
    bool charging_             = false;
    bool charge_ready_         = false;
    bool charge_heard_         = false;
    bool charge_frozen_        = false;
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

    auto equip(ecs::entity player, loadout wanted) -> void;
    auto wear(ecs::entity player, std::string_view body_prefab) -> bool;

    auto take_hit_on_shield(ecs::entity player) -> bool;
    auto take_hit(ecs::entity player) -> bool;
    auto break_guard(ecs::entity player) -> bool;
    auto die(ecs::entity player, death_fall fall = death_fall::backward) -> bool;
    auto revive(ecs::entity player) -> bool;

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
    static auto drop_charge_(player_component& state) -> void;
    auto spin_whirl_(player_component& state, float32 delta_time) const -> void;
    auto put_away_(player_component& state) const -> void;
    auto draw_bow_(
        ecs::entity ent, player_component& state, const input_frame& frame, bool action_playing,
        float32 delta_time
    ) -> void;
    static auto rest_bow_(player_component& state) -> void;
    auto drop_draw_(ecs::entity ent, player_component& state) -> void;
    auto nock_(ecs::entity player) const -> void;
    auto unnock_(ecs::entity player) const -> void;
    auto loose_(ecs::entity player) const -> void;

    enum class bow_chore : uint8 {
        nock,
        unnock,
        loose,
    };

    struct pending_chore {
        ecs::entity player;
        bow_chore chore;
    };

    struct pending_loadout {
        ecs::entity player;
        loadout wanted;
    };

    ecs::world* world_;
    asset::asset_storage* assets_;
    std::vector<pending_loadout> equipping_;
    std::vector<pending_chore> chores_;
    movement_tuning tuning_;
};

}  // namespace vw::game
