module vw.gfx;

import std;
import vw.core;
import vw.ecs;
import vw.world;
import vw.platform;

namespace vw::gfx {

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

    const auto& tc        = registry.get<transform_component>(target);
    const auto player_pos = tc.get_shown_position();
    const auto focus      = player_pos + params_.target_offset + vec3f{0.0f, focus_lift, 0.0f};

    const float32 yaw_rad   = math::radians(look_yaw_degrees);
    const float32 pitch_rad = math::radians(look_pitch_degrees);

    const float32 shoulder_goal = over_shoulder ? 1.0f : 0.0f;
    const float32 follow        = params_.shoulder_follow_seconds > 0.0f
        ? 1.0f - std::exp(-delta_time / params_.shoulder_follow_seconds)
        : 1.0f;
    shoulder_share_ += (shoulder_goal - shoulder_share_) * follow;

    const vec3f shoulder =
        vec3f{std::cos(yaw_rad), 0.0f, -std::sin(yaw_rad)} * params_.shoulder_offset +
        vec3f{0.0f, params_.shoulder_rise, 0.0f};
    const float32 wanted_arm =
        params_.arm_length +
        (std::min(params_.shoulder_arm_length, params_.arm_length) - params_.arm_length) *
            shoulder_share_;

    const vec3f arm_dir{
        -std::sin(yaw_rad) * std::cos(pitch_rad),
        -std::sin(pitch_rad),
        -std::cos(yaw_rad) * std::cos(pitch_rad)
    };

    const vec3f desired_pos = focus + shoulder * shoulder_share_ + arm_dir * wanted_arm;
    const float32 reach     = math::length(desired_pos - focus);

    actual_arm_length_ = reach;

    auto& spatial_sys = world_->system<spatial_system>();
    vw::spatial::ray collision_ray{focus, desired_pos};
    constexpr spatial_layer_mask camera_mask = spatial_layer::terrain | spatial_layer::prop;
    auto hit = spatial_sys.voxel_ray_cast(collision_ray, collision_candidates_, camera_mask);

    if (hit) {
        const auto& hit_tc  = registry.get<transform_component>(hit->ent);
        const auto& hit_mc  = registry.get<model_component>(hit->ent);
        vec3f hit_world_pos = model_matrix(hit_tc, hit_mc) *
            vec3f{
                static_cast<float32>(hit->voxel_pos.x) + 0.5f,
                static_cast<float32>(hit->voxel_pos.y) + 0.5f,
                static_cast<float32>(hit->voxel_pos.z) + 0.5f
            };

        float32 hit_distance = math::length(hit_world_pos - focus);
        float32 clamped      = hit_distance - params_.collision_skin;
        if (clamped < actual_arm_length_ && clamped > 0.0f) {
            actual_arm_length_ = clamped;
        }
    }

    const vec3f cam_pos = reach > math::epsilon
        ? focus + (desired_pos - focus) * (actual_arm_length_ / reach)
        : focus;
    camera_->set_position(cam_pos);

    const vec3f look_dir    = arm_dir * -1.0f;
    float32 horizontal_dist = std::sqrt(look_dir.x * look_dir.x + look_dir.z * look_dir.z);
    float32 look_pitch      = std::atan2(look_dir.y, horizontal_dist) * 180.0f / math::pi;
    float32 look_yaw        = std::atan2(look_dir.x, look_dir.z) * 180.0f / math::pi;
    camera_->set_rotation(look_pitch, look_yaw);
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
