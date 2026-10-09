module vw.world;

import std;
import vw.core;

namespace vw::ecs {

namespace {

auto approach(const vec3f& from, const vec3f& to, float32 max_step) -> vec3f {
    const vec3f gap          = to - from;
    const float32 gap_length = math::length(gap);
    if (gap_length <= max_step) {
        return to;
    }
    return from + gap * (max_step / gap_length);
}

auto planar_step(
    const character_controller_component& cc, const vec3f& from, const vec3f& to, float32 dt
) -> float32 {
    const bool slowing = math::length_squared(to) < math::length_squared(from);
    const float32 seconds =
        slowing ? cc.get_deceleration_seconds() : cc.get_acceleration_seconds();
    if (seconds <= 0.0f) {
        return std::numeric_limits<float32>::max();
    }
    return cc.get_move_speed() / seconds * dt;
}

}  // namespace

character_controller_system::character_controller_system(world& w)
    : world_(&w) {}

auto character_controller_system::update(float32 delta_time) -> void {
    auto& reg = world_->registry();
    for (auto [ent, cc, rb, mi] :
         reg.view<character_controller_component, rigid_body_component, movement_intent_component>()) {

        if (rb.is_frozen()) {
            cc.jump_requested_ = false;
            cc.move_input_ = {0.0f, 0.0f, 0.0f};
            continue;
        }

        const vec3f planar_now{mi.wish_velocity_.x, 0.0f, mi.wish_velocity_.z};
        const vec3f planar_goal{
            cc.move_input_.x * cc.move_speed_, 0.0f, cc.move_input_.z * cc.move_speed_
        };
        const vec3f planar_next = approach(
            planar_now, planar_goal, planar_step(cc, planar_now, planar_goal, delta_time)
        );

        mi.wish_velocity_.x = planar_next.x;
        mi.wish_velocity_.z = planar_next.z;
        mi.wish_axes_ = static_cast<axis_flags>(axis_flag::xz | (mi.wish_axes_ & axis_flag::y));

        const bool jump_waiting_for_physics = (mi.wish_axes_ & axis_flag::y) != 0;
        if (rb.is_grounded()) {
            cc.seconds_off_ground_ = 0.0f;
            if (!jump_waiting_for_physics) {
                cc.left_ground_by_jump_ = false;
            }
        } else if (cc.seconds_off_ground_ < std::numeric_limits<float32>::max()) {
            cc.seconds_off_ground_ += delta_time;
        }

        const bool within_coyote =
            !cc.left_ground_by_jump_ && cc.seconds_off_ground_ <= cc.coyote_seconds_;
        if (cc.jump_requested_ && !jump_waiting_for_physics && (rb.is_grounded() || within_coyote)) {
            mi.wish_velocity_.y = cc.jump_impulse_;
            mi.wish_axes_ = axis_flag::xz | axis_flag::y;
            cc.left_ground_by_jump_ = true;
            ++cc.jump_count_;
        }

        auto facing_len = math::length(cc.facing_direction_);
        if (facing_len > 0.001f && reg.has<transform_component>(ent)) {
            const auto& tc = reg.get<transform_component>(ent);
            const auto target  = math::quat_look_y(cc.facing_direction_);
            const auto current = tc.get_rotation();
            const float32 alignment =
                math::clamp(std::abs(math::dot(current, target)), 0.0f, 1.0f);
            const float32 remaining_radians = 2.0f * std::acos(alignment);
            const float32 step_radians =
                math::radians(cc.turn_degrees_per_second_) * delta_time;

            if (remaining_radians > step_radians) {
                world_->system<transform_system>().modify(ent).set_rotation(
                    math::slerp(current, target, step_radians / remaining_radians)
                );
            } else if (remaining_radians > 0.0f) {
                world_->system<transform_system>().modify(ent).set_rotation(target);
            }
        }

        cc.jump_requested_ = false;
        cc.move_input_ = {0.0f, 0.0f, 0.0f};
    }
}

character_controller_system::controller_modifier::controller_modifier(
    character_controller_system* system, entity ent
)
    : system_(system), entity_(ent) {}

auto character_controller_system::modify(
    entity ent
) -> controller_modifier {
    return controller_modifier(this, ent);
}

auto character_controller_system::controller_modifier::set_move_input(
    const vec3f& input
) -> controller_modifier& {
    auto& reg = system_->world_->registry();
    if (!reg.has<character_controller_component>(entity_)) {
        return *this;
    }
    auto& comp = reg.get<character_controller_component>(entity_);
    comp.move_input_ = input;
    return *this;
}

auto character_controller_system::controller_modifier::set_facing_direction(
    const vec3f& direction
) -> controller_modifier& {
    auto& reg = system_->world_->registry();
    if (!reg.has<character_controller_component>(entity_)) {
        return *this;
    }
    auto& comp = reg.get<character_controller_component>(entity_);
    comp.facing_direction_ = direction;
    return *this;
}

auto character_controller_system::controller_modifier::set_move_speed(
    float32 speed
) -> controller_modifier& {
    auto& reg = system_->world_->registry();
    if (!reg.has<character_controller_component>(entity_)) {
        return *this;
    }
    auto& comp = reg.get<character_controller_component>(entity_);
    comp.move_speed_ = speed;
    return *this;
}

auto character_controller_system::controller_modifier::set_acceleration_seconds(
    float32 seconds
) -> controller_modifier& {
    auto& reg = system_->world_->registry();
    if (!reg.has<character_controller_component>(entity_)) {
        return *this;
    }
    auto& comp = reg.get<character_controller_component>(entity_);
    comp.acceleration_seconds_ = seconds;
    return *this;
}

auto character_controller_system::controller_modifier::set_deceleration_seconds(
    float32 seconds
) -> controller_modifier& {
    auto& reg = system_->world_->registry();
    if (!reg.has<character_controller_component>(entity_)) {
        return *this;
    }
    auto& comp = reg.get<character_controller_component>(entity_);
    comp.deceleration_seconds_ = seconds;
    return *this;
}

auto character_controller_system::controller_modifier::set_jump_impulse(
    float32 impulse
) -> controller_modifier& {
    auto& reg = system_->world_->registry();
    if (!reg.has<character_controller_component>(entity_)) {
        return *this;
    }
    auto& comp = reg.get<character_controller_component>(entity_);
    comp.jump_impulse_ = impulse;
    return *this;
}

auto character_controller_system::controller_modifier::set_turn_degrees_per_second(
    float32 degrees_per_second
) -> controller_modifier& {
    auto& reg = system_->world_->registry();
    if (!reg.has<character_controller_component>(entity_)) {
        return *this;
    }
    auto& comp = reg.get<character_controller_component>(entity_);
    comp.turn_degrees_per_second_ = degrees_per_second;
    return *this;
}

auto character_controller_system::controller_modifier::set_coyote_seconds(
    float32 seconds
) -> controller_modifier& {
    auto& reg = system_->world_->registry();
    if (!reg.has<character_controller_component>(entity_)) {
        return *this;
    }
    auto& comp = reg.get<character_controller_component>(entity_);
    comp.coyote_seconds_ = seconds;
    return *this;
}

auto character_controller_system::controller_modifier::request_jump(
) -> controller_modifier& {
    auto& reg = system_->world_->registry();
    if (!reg.has<character_controller_component>(entity_)) {
        return *this;
    }
    auto& comp = reg.get<character_controller_component>(entity_);
    comp.jump_requested_ = true;
    return *this;
}

auto character_controller_system::controller_modifier::set_step_hop_voxels(
    float32 voxels
) -> controller_modifier& {
    auto& reg = system_->world_->registry();
    if (!reg.has<character_controller_component>(entity_)) {
        return *this;
    }
    auto& comp = reg.get<character_controller_component>(entity_);
    comp.step_hop_voxels_ = voxels;
    return *this;
}

auto character_controller_system::controller_modifier::set_ride_lead(
    float32 lead_voxels, float32 rise_speed, float32 sink_speed
) -> controller_modifier& {
    auto& reg = system_->world_->registry();
    if (!reg.has<character_controller_component>(entity_)) {
        return *this;
    }
    auto& comp = reg.get<character_controller_component>(entity_);
    comp.ride_lead_voxels_ = std::max(lead_voxels, 0.0f);
    comp.ride_rise_speed_  = std::max(rise_speed, 1.0f);
    comp.ride_sink_speed_  = std::max(sink_speed, 1.0f);
    return *this;
}

}  // namespace vw::ecs
