export module vw.game:player.control;

import std;

import vw.core;
import vw.asset;
import vw.ecs;
import vw.world;

export namespace vw::game {

class player_system;

struct movement_tuning final {
    float32 run_turn_degrees_per_second    = 360.0f;
    float32 attack_turn_degrees_per_second = 2400.0f;
    float32 acceleration_seconds           = 0.15f;
    float32 deceleration_seconds           = 0.2f;
    float32 lean_forward_degrees           = 5.0f;
    float32 lean_back_degrees              = 4.0f;
    float32 lean_side_degrees              = 8.0f;
    float32 lean_follow_seconds            = 0.08f;
    float32 input_buffer_seconds           = 0.15f;
    float32 coyote_seconds                 = 0.1f;
    float32 lunge_seconds                  = 0.15f;
    float32 lunge_distance                 = 10.0f;
};

struct player_component final {
    [[nodiscard]] auto has_weapon() const -> bool {
        return weapon_.is_valid();
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

private:
    friend class player_system;

    ecs::entity pose_;
    ecs::entity body_;
    ecs::entity head_;
    ecs::entity hand_right_;
    ecs::entity hand_left_;
    ecs::entity foot_right_;
    ecs::entity foot_left_;
    ecs::entity weapon_;

    vec3f attack_facing_{0.0f, 0.0f, 0.0f};
    vec3f previous_planar_velocity_{0.0f, 0.0f, 0.0f};
    float32 lean_forward_degrees_ = 0.0f;
    float32 lean_right_degrees_   = 0.0f;
    float32 swing_seconds_        = 0.0f;
    float32 lunge_seconds_        = 0.0f;
    float32 attack_buffered_      = -1.0f;
    float32 jump_buffered_        = -1.0f;
    uint32 seen_jump_count_       = 0;
    uint32 swing_count_           = 0;
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

    [[nodiscard]] auto tuning() -> movement_tuning& {
        return tuning_;
    }

private:
    [[nodiscard]] auto create_body_part_(ecs::entity parent, std::string_view part_name) const
        -> ecs::entity;
    auto attach_machines_(ecs::entity root) const -> void;
    auto lean_(ecs::entity ent, player_component& state, float32 delta_time) const -> void;
    static auto read_action_events_(player_component& state, const ecs::animation_player_component& layers)
        -> void;
    static auto end_swing_(player_component& state) -> void;

    ecs::world* world_;
    asset::asset_storage* assets_;
    std::vector<ecs::entity> toggling_;
    movement_tuning tuning_;
};

}  // namespace vw::game
