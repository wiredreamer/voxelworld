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

constexpr float32 step_probe_voxels     = 0.25f;
constexpr float32 step_probe_seconds    = 0.12f;
constexpr float32 step_clearance_voxels = 0.2f;
constexpr float32 step_floor_skin       = 1.0f;
constexpr float32 step_input_threshold  = 0.1f;

auto box_hits_solid(const world_grid& grid, const vec3f& lo, const vec3f& hi) -> bool {
    const int32 vs_i = grid.world_units_per_voxel();
    const auto vs    = static_cast<float32>(vs_i);

    const auto first = [vs](float32 v) -> int32 { return static_cast<int32>(std::floor(v / vs)); };
    const auto last  = [vs](float32 v) -> int32 {
        return static_cast<int32>(std::ceil(v / vs)) - 1;
    };

    for (int32 vx = first(lo.x); vx <= last(hi.x); ++vx) {
        for (int32 vy = first(lo.y); vy <= last(hi.y); ++vy) {
            for (int32 vz = first(lo.z); vz <= last(hi.z); ++vz) {
                if (!grid.get_voxel(vec3i{vx * vs_i, vy * vs_i, vz * vs_i}).is_empty()) {
                    return true;
                }
            }
        }
    }
    return false;
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
        } else if (cc.step_hop_voxels_ > 0.0f && !jump_waiting_for_physics && rb.is_grounded()) {
            const vec3f wish{cc.move_input_.x, 0.0f, cc.move_input_.z};
            const float32 wish_length = math::length(wish);
            const auto* grid = world_->system<world_grid_system>().grid();
            if (wish_length > step_input_threshold && grid != nullptr) {
                const auto vs        = static_cast<float32>(grid->world_units_per_voxel());
                const float32 rise   = cc.step_hop_voxels_ * vs;
                const float32 reach  = std::max(
                    step_probe_voxels * vs, math::length(planar_next) * step_probe_seconds
                );

                if (step_ahead_(ent, wish * (reach / wish_length), rise)) {
                    const float32 gravity = std::abs(
                        world_->system<physics_system>().get_gravity() * rb.get_gravity_scale()
                    );
                    mi.wish_velocity_.y =
                        std::sqrt(2.0f * gravity * (rise + (step_clearance_voxels * vs)));
                    mi.wish_axes_ = axis_flag::xz | axis_flag::y;
                    cc.left_ground_by_jump_ = true;
                    ++cc.step_hop_count_;
                }
            }
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

auto character_controller_system::step_ahead_(
    entity ent, const vec3f& ahead, float32 rise
) const -> bool {
    auto& reg = world_->registry();
    const auto* grid = world_->system<world_grid_system>().grid();
    if (grid == nullptr || !reg.has<box_collider_component>(ent) ||
        !reg.has<transform_component>(ent)) {
        return false;
    }

    const auto& collider = reg.get<box_collider_component>(ent);
    const vec3f center =
        reg.get<transform_component>(ent).get_position() + collider.get_offset();
    const vec3f half = collider.get_extents() * 0.5f;

    const vec3f lifted{0.0f, rise + 0.01f, 0.0f};
    const vec3f lo = center - half;
    const vec3f hi = center + half;

    const vec3f feet_lo{lo.x, lo.y + step_floor_skin, lo.z};
    const vec3f feet_hi{hi.x, std::min(hi.y, lo.y + rise), hi.z};

    if (!box_hits_solid(*grid, feet_lo + ahead, feet_hi + ahead)) {
        return false;
    }

    return !box_hits_solid(*grid, lo + lifted, hi + lifted) &&
           !box_hits_solid(*grid, lo + lifted + ahead, hi + lifted + ahead);
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

}  // namespace vw::ecs
