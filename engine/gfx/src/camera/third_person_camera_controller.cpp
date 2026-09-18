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
    const player_input_state& input, entity target
) -> void {
    yaw_ += input.look_delta.x;
    pitch_ += input.look_delta.y;
    pitch_ = math::clamp(pitch_, params_.pitch_min, params_.pitch_max);

    params_.arm_length -= input.zoom_delta * params_.zoom_speed;
    params_.arm_length =
        math::clamp(params_.arm_length, params_.arm_length_min, params_.arm_length_max);

    auto& registry = world_->registry();
    if (!registry.has<transform_component>(target)) {
        return;
    }

    const auto& tc        = registry.get<transform_component>(target);
    const auto player_pos = tc.get_position();
    const auto focus      = player_pos + params_.target_offset;

    const float32 yaw_rad   = math::radians(yaw_);
    const float32 pitch_rad = math::radians(pitch_);

    const vec3f arm_dir{
        std::sin(yaw_rad) * std::cos(pitch_rad),
        std::sin(pitch_rad),
        std::cos(yaw_rad) * std::cos(pitch_rad)
    };

    vec3f desired_pos = focus + arm_dir * params_.arm_length;

    actual_arm_length_ = params_.arm_length;

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

    vec3f cam_pos = focus + arm_dir * actual_arm_length_;
    camera_->set_position(cam_pos);

    auto look_dir           = focus - cam_pos;
    float32 horizontal_dist = std::sqrt(look_dir.x * look_dir.x + look_dir.z * look_dir.z);
    float32 look_pitch      = std::atan2(look_dir.y, horizontal_dist) * 180.0f / math::pi;
    float32 look_yaw        = std::atan2(look_dir.x, look_dir.z) * 180.0f / math::pi;
    camera_->set_rotation(look_pitch, look_yaw);
}

auto third_person_camera_controller::get_params() -> third_person_camera_params& {
    return params_;
}

auto third_person_camera_controller::get_pitch() const -> float32 {
    return pitch_;
}

auto third_person_camera_controller::get_yaw() const -> float32 {
    return yaw_;
}

auto third_person_camera_controller::get_actual_arm_length() const -> float32 {
    return actual_arm_length_;
}

}  // namespace vw::gfx
