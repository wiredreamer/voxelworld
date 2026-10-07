export module vw.world:components.physics;

import std;

import vw.core;
import vw.ecs;
import :spatial;

export namespace vw::ecs {

class animation_fsm_system;
class animation_system;
class character_controller_system;
class hierarchy_system;
class light_system;
class model_system;
class physics_system;
class socket_system;
class spatial_system;
class transform_system;
class world_grid_system;

struct box_collider_component final {
    [[nodiscard]] auto get_extents() const -> const vec3f& {
        return extents_;
    }

    [[nodiscard]] auto get_offset() const -> const vec3f& {
        return offset_;
    }

private:
    friend class physics_system;

    vec3f extents_{1.0F, 1.0F, 1.0F};
    vec3f offset_{0.0F, 0.0F, 0.0F};
};

struct rigid_body_component final {
    [[nodiscard]] auto get_step_sink() const -> float32 {
        return shown_sink_;
    }

    [[nodiscard]] auto get_steps_taken() const -> uint32 {
        return steps_taken_;
    }

    [[nodiscard]] auto get_velocity() const -> const vec3f& {
        return velocity_;
    }

    [[nodiscard]] auto get_impulse() const -> const vec3f& {
        return impulse_;
    }

    [[nodiscard]] auto get_gravity_scale() const -> float32 {
        return gravity_scale_;
    }

    [[nodiscard]] auto get_drag() const -> float32 {
        return drag_;
    }

    [[nodiscard]] auto is_grounded() const -> bool {
        return grounded_;
    }

    [[nodiscard]] auto is_frozen() const -> bool {
        return frozen_;
    }

private:
    friend class physics_system;

    vec3f velocity_{0.0F, 0.0F, 0.0F};
    vec3f impulse_{0.0F, 0.0F, 0.0F};
    float32 gravity_scale_ = 1.0F;
    float32 drag_          = 5.0F;
    bool grounded_         = false;
    bool frozen_           = false;
    vec3f stepped_from_{0.0F, 0.0F, 0.0F};
    vec3f stepped_to_{0.0F, 0.0F, 0.0F};
    float32 sink_before_step_    = 0.0F;
    float32 shown_sink_          = 0.0F;
    float32 step_sink_           = 0.0F;
    float32 step_lead_           = 0.0F;
    float32 step_catch_up_       = 0.0F;
    float32 step_smooth_seconds_ = 0.0F;
    entity step_follower_{};
    uint32 steps_taken_          = 0;
};

using axis_flags = uint8;

namespace axis_flag {
inline constexpr axis_flags none = 0;
inline constexpr axis_flags x    = 1;
inline constexpr axis_flags y    = 2;
inline constexpr axis_flags z    = 4;
inline constexpr axis_flags xz   = x | z;
inline constexpr axis_flags xyz  = x | y | z;
}  // namespace axis_flag

struct movement_intent_component final {
    [[nodiscard]] auto get_wish_velocity() const -> const vec3f& {
        return wish_velocity_;
    }

    [[nodiscard]] auto get_wish_axes() const -> axis_flags {
        return wish_axes_;
    }

private:
    friend class physics_system;
    friend class character_controller_system;

    vec3f wish_velocity_{0.0F, 0.0F, 0.0F};
    axis_flags wish_axes_ = axis_flag::xz;
};

struct character_controller_component final {
    [[nodiscard]] auto get_move_speed() const -> float32 {
        return move_speed_;
    }

    [[nodiscard]] auto get_acceleration_seconds() const -> float32 {
        return acceleration_seconds_;
    }

    [[nodiscard]] auto get_deceleration_seconds() const -> float32 {
        return deceleration_seconds_;
    }

    [[nodiscard]] auto get_jump_impulse() const -> float32 {
        return jump_impulse_;
    }

    [[nodiscard]] auto get_turn_degrees_per_second() const -> float32 {
        return turn_degrees_per_second_;
    }

    [[nodiscard]] auto get_facing_direction() const -> const vec3f& {
        return facing_direction_;
    }

    [[nodiscard]] auto get_coyote_seconds() const -> float32 {
        return coyote_seconds_;
    }

    [[nodiscard]] auto get_jump_count() const -> uint32 {
        return jump_count_;
    }

    [[nodiscard]] auto left_ground_by_jump() const -> bool {
        return left_ground_by_jump_;
    }

    [[nodiscard]] auto get_seconds_off_ground() const -> float32 {
        return seconds_off_ground_;
    }

    [[nodiscard]] auto get_step_hop_voxels() const -> float32 {
        return step_hop_voxels_;
    }


private:
    friend class character_controller_system;

    vec3f move_input_{0.0F, 0.0F, 0.0F};
    vec3f facing_direction_{0.0F, 0.0F, 1.0F};
    float32 move_speed_              = 100.0F;
    float32 acceleration_seconds_    = 0.1F;
    float32 deceleration_seconds_    = 0.15F;
    float32 jump_impulse_            = 150.0F;
    float32 turn_degrees_per_second_ = 900.0F;
    float32 coyote_seconds_          = 0.1F;
    float32 seconds_off_ground_      = std::numeric_limits<float32>::max();
    float32 step_hop_voxels_         = 0.0F;
    uint32 jump_count_               = 0;
    bool left_ground_by_jump_        = false;
    bool jump_requested_             = false;
};

}  // namespace vw::ecs
