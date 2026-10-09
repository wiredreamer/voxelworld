module vw.gfx;

import std;
import vw.core;
import vw.ecs;
import vw.world;
import vw.platform;

namespace vw::gfx {

namespace {

auto arm_share_at(const third_person_camera_params& params, float32 pitch_degrees) -> float32 {
    const bool up       = pitch_degrees > 0.0f;
    const float32 full  = up ? params.look_up_full_degrees : params.look_down_full_degrees;
    const float32 share = up ? params.look_up_arm_share : params.look_down_arm_share;
    if (full <= 0.0f) {
        return 1.0f;
    }

    const float32 along  = math::clamp(std::abs(pitch_degrees) / full, 0.0f, 1.0f);
    const float32 smooth = along * along * (3.0f - (2.0f * along));
    return 1.0f + ((share - 1.0f) * smooth);
}

}  // namespace

third_person_camera_controller::third_person_camera_controller(
    camera& camera, world_type& world, third_person_camera_params params
)
    : camera_(&camera), world_(&world), params_(params), actual_arm_length_(params.arm_length) {}

auto third_person_camera_controller::update(
    entity target, float32 look_yaw_degrees, float32 look_pitch_degrees, float32 zoom_delta,
    float32 focus_lift, bool over_shoulder, float32 delta_time
) -> void {
    params_.arm_length -= zoom_delta * params_.zoom_speed;
    params_.arm_length =
        math::clamp(params_.arm_length, params_.arm_length_min, params_.arm_length_max);

    auto& registry = world_->registry();
    if (!registry.has<transform_component>(target)) {
        return;
    }

    target_ = target;

    const auto& tc        = registry.get<transform_component>(target);
    const auto player_pos = tc.get_shown_position();
    const vec3f lift{0.0f, focus_lift, 0.0f};
    const vec3f chest       = player_pos + params_.target_offset * 0.5f + lift;
    const vec3f wanted_head = player_pos + params_.target_offset + lift;
    const float32 neck      = math::length(wanted_head - chest);
    const vec3f head        = neck > math::epsilon
        ? chest + (wanted_head - chest) * (free_length_(chest, wanted_head) / neck)
        : wanted_head;

    const float32 yaw_rad   = math::radians(look_yaw_degrees);
    const float32 pitch_rad = math::radians(look_pitch_degrees);

    const float32 shoulder_goal = over_shoulder ? 1.0f : 0.0f;
    const float32 follow        = params_.shoulder_follow_seconds > 0.0f
        ? 1.0f - std::exp(-delta_time / params_.shoulder_follow_seconds)
        : 1.0f;
    shoulder_share_ += (shoulder_goal - shoulder_share_) * follow;

    const vec3f aside{std::cos(yaw_rad), 0.0f, -std::sin(yaw_rad)};
    const vec3f shoulder = aside * params_.shoulder_offset + vec3f{0.0f, params_.shoulder_rise, 0.0f};

    const vec3f wanted_pivot =
        head + vec3f{0.0f, params_.pivot_rise, 0.0f} + shoulder * shoulder_share_;
    const float32 pivot_reach = math::length(wanted_pivot - head);
    const vec3f pivot = pivot_reach > math::epsilon
        ? head + (wanted_pivot - head) * (free_length_(head, wanted_pivot) / pivot_reach)
        : head;

    const float32 shouldered_arm =
        params_.arm_length +
        (std::min(params_.shoulder_arm_length, params_.arm_length) - params_.arm_length) *
            shoulder_share_;
    const float32 wanted_arm = shouldered_arm * arm_share_at(params_, look_pitch_degrees);

    const vec3f arm_dir{
        -std::sin(yaw_rad) * std::cos(pitch_rad),
        -std::sin(pitch_rad),
        -std::cos(yaw_rad) * std::cos(pitch_rad)
    };
    const vec3f look_dir = arm_dir * -1.0f;
    const vec3f above    = math::cross(aside, look_dir);

    // см. docs/ENGINE.md#камера-от-третьего-лица
    float32 free_arm = free_length_(pivot, pivot + arm_dir * wanted_arm);
    const std::array<vec3f, 4> probes{
        aside * params_.probe_radius, aside * -params_.probe_radius,
        above * params_.probe_radius, above * -params_.probe_radius
    };
    for (const auto& probe : probes) {
        free_arm = std::min(free_arm, free_length_(pivot, pivot + probe + arm_dir * wanted_arm));
    }

    const float32 free_share = wanted_arm > math::epsilon ? free_arm / wanted_arm : 1.0f;
    if (free_share < held_arm_share_ || params_.arm_return_seconds <= 0.0f) {
        held_arm_share_ = free_share;
    } else {
        held_arm_share_ +=
            (free_share - held_arm_share_) * (1.0f - std::exp(-delta_time / params_.arm_return_seconds));
    }

    actual_arm_length_ = wanted_arm * held_arm_share_;
    camera_->set_position(pivot + arm_dir * actual_arm_length_);

    float32 horizontal_dist = std::sqrt(look_dir.x * look_dir.x + look_dir.z * look_dir.z);
    float32 look_pitch      = std::atan2(look_dir.y, horizontal_dist) * 180.0f / math::pi;
    float32 look_yaw        = std::atan2(look_dir.x, look_dir.z) * 180.0f / math::pi;
    camera_->set_rotation(look_pitch, look_yaw);
}

auto third_person_camera_controller::free_length_(const vec3f& from, const vec3f& to) -> float32 {
    const float32 whole = math::length(to - from);
    if (whole <= math::epsilon) {
        return 0.0f;
    }

    const vw::spatial::ray path{from, to};
    constexpr spatial_layer_mask camera_mask = spatial_layer::terrain | spatial_layer::prop;
    const auto& tree = world_->system<spatial_system>();

    // см. docs/ENGINE.md#камера-от-третьего-лица
    collision_candidates_.clear();
    tree.query_all(path, collision_candidates_, camera_mask);
    std::erase_if(collision_candidates_, [this](entity ent) { return belongs_to_target_(ent); });
    const auto hit = tree.closest_voxel_hit(path, collision_candidates_);
    if (!hit) {
        return whole;
    }

    auto& registry      = world_->registry();
    const mat4f placing = model_matrix(
        registry.get<transform_component>(hit->ent), registry.get<model_component>(hit->ent)
    );
    const mat4f back = math::inverse_matrix(placing).value_or(math::identity_matrix());

    const vec3f corner{
        static_cast<float32>(hit->voxel_pos.x), static_cast<float32>(hit->voxel_pos.y),
        static_cast<float32>(hit->voxel_pos.z)
    };
    const vw::spatial::aabb cell{.min = corner, .max = corner + vec3f{1.0f, 1.0f, 1.0f}};
    const vw::spatial::ray inside{back * from, back * to};

    float32 entered  = 0.0f;
    const vec3f met  = inside.intersects_at(cell, entered) ? inside.point_at(entered) : cell.center();
    const float32 to_wall = math::dot(placing * met - from, path.direction);

    return math::clamp(to_wall - params_.collision_skin, 0.0f, whole);
}

auto third_person_camera_controller::belongs_to_target_(entity ent) const -> bool {
    auto top = ent;
    while (top != target_) {
        const auto* links = world_->try_get<hierarchy_component>(top);
        if (links == nullptr || !links->has_parent()) {
            return false;
        }
        top = links->get_parent();
    }
    return true;
}

auto third_person_camera_controller::get_params() -> third_person_camera_params& {
    return params_;
}

auto third_person_camera_controller::get_actual_arm_length() const -> float32 {
    return actual_arm_length_;
}

auto third_person_camera_controller::get_shoulder_share() const -> float32 {
    return shoulder_share_;
}

}  // namespace vw::gfx
