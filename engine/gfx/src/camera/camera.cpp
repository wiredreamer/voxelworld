module vw.gfx;

import std;
import vw.core;
import vw.ecs;
import vw.world;
import vw.platform;

namespace vw::gfx {
camera::camera(
    float fov, float aspect, float near, float far
)
    : position_(0.0f, 0.0f, 0.0f)
    , pitch_(0.0f)
    , yaw_(0.0f)
    , fov_(fov)
    , aspect_(aspect)
    , near_(near)
    , far_(far)
    , forward_(0.0f, 0.0f, 1.0f)
    , right_(1.0f, 0.0f, 0.0f)
    , up_(0.0f, 1.0f, 0.0f)
    , vectors_dirty_(false)
    , view_matrix_dirty_(true)
    , projection_matrix_dirty_(true)
    , frustum_dirty_(true) {
    update_vectors();
}

auto camera::set_position(
    const vec3f& position
) -> void {
    position_          = position;
    view_matrix_dirty_ = true;
    frustum_dirty_     = true;
}

auto camera::set_rotation(
    float pitch, float yaw
) -> void {
    pitch_             = pitch;
    yaw_               = yaw;
    vectors_dirty_     = true;
    view_matrix_dirty_ = true;
    frustum_dirty_     = true;
}

auto camera::set_aspect_ratio(
    float aspect
) -> void {
    aspect_                  = aspect;
    projection_matrix_dirty_ = true;
    frustum_dirty_           = true;
}

auto camera::set_far(
    float far
) -> void {
    far_                     = far;
    projection_matrix_dirty_ = true;
    frustum_dirty_           = true;
}

auto camera::get_near() const -> float {
    return near_;
}

auto camera::get_far() const -> float {
    return far_;
}

auto camera::get_fov() const -> float {
    return fov_;
}

auto camera::get_aspect_ratio() const -> float {
    return aspect_;
}

auto camera::get_position() const -> vec3f {
    return position_;
}

auto camera::get_pitch() const -> float {
    return pitch_;
}

auto camera::get_yaw() const -> float {
    return yaw_;
}

auto camera::get_view_matrix() const -> mat4f {
    if (view_matrix_dirty_) {
        update_view_matrix();
    }
    return view_matrix_;
}

auto camera::get_projection_matrix() const -> mat4f {
    if (projection_matrix_dirty_) {
        update_projection_matrix();
    }
    return projection_matrix_;
}

auto camera::get_view_projection_matrix() const -> mat4f {
    return get_projection_matrix() * get_view_matrix();
}

auto camera::move_forward(
    float distance
) -> void {
    if (vectors_dirty_) {
        update_vectors();
    }
    position_          = position_ + forward_ * distance;
    view_matrix_dirty_ = true;
    frustum_dirty_     = true;
}

auto camera::move_right(
    float distance
) -> void {
    if (vectors_dirty_) {
        update_vectors();
    }
    position_          = position_ + right_ * distance;
    view_matrix_dirty_ = true;
    frustum_dirty_     = true;
}

auto camera::move_up(
    float distance
) -> void {
    if (vectors_dirty_) {
        update_vectors();
    }
    position_          = position_ + up_ * distance;
    view_matrix_dirty_ = true;
    frustum_dirty_     = true;
}

auto camera::rotate(
    float delta_pitch, float delta_yaw
) -> void {
    pitch_ += delta_pitch;
    yaw_ += delta_yaw;

    pitch_ = math::clamp(pitch_, -89.0f, 89.0f);

    vectors_dirty_     = true;
    view_matrix_dirty_ = true;
    frustum_dirty_     = true;
}

auto camera::get_forward() const -> vec3f {
    if (vectors_dirty_) {
        update_vectors();
    }
    return forward_;
}

auto camera::get_right() const -> vec3f {
    if (vectors_dirty_) {
        update_vectors();
    }
    return right_;
}

auto camera::get_up() const -> vec3f {
    if (vectors_dirty_) {
        update_vectors();
    }
    return up_;
}

auto camera::update_vectors() const -> void {
    const float pitch_rad = math::radians(pitch_);
    const float yaw_rad   = math::radians(yaw_);

    forward_.x = std::sin(yaw_rad) * std::cos(pitch_rad);
    forward_.y = std::sin(pitch_rad);
    forward_.z = std::cos(yaw_rad) * std::cos(pitch_rad);

    forward_ = math::normalize(forward_);
    right_   = math::normalize(math::cross(vec3f(0.0f, 1.0f, 0.0f), forward_));
    up_      = math::normalize(math::cross(forward_, right_));

    vectors_dirty_ = false;
}

auto camera::update_view_matrix() const -> void {
    if (vectors_dirty_) {
        update_vectors();
    }

    const vec3f center = position_ + forward_;
    view_matrix_       = math::look_at_matrix(position_, center, up_);
    view_matrix_dirty_ = false;
}

auto camera::update_projection_matrix() const -> void {
    projection_matrix_       = math::perspective_matrix_reversed(fov_, aspect_, near_, far_);
    projection_matrix_dirty_ = false;
}

auto camera::get_frustum() const -> const vw::spatial::frustum& {
    if (vectors_dirty_) {
        update_vectors();
    }
    if (view_matrix_dirty_) {
        update_view_matrix();
    }
    if (projection_matrix_dirty_) {
        update_projection_matrix();
    }
    if (frustum_dirty_) {
        update_frustum();
    }
    return frustum_;
}

auto camera::update_frustum() const -> void {
    frustum_       = vw::spatial::frustum::from_view_projection_matrix(get_view_projection_matrix());
    frustum_dirty_ = false;
}

auto camera::screen_to_world_ray(
    const vec2d& mouse_pos, const vec2i& window_size
) const -> vw::spatial::ray {
    if (window_size.x <= 0 || window_size.y <= 0) {
        return vw::spatial::ray{vec3f{0.0f, 0.0f, 0.0f}, vec3f{0.0f, 0.0f, 1.0f}};
    }

    const float32 ndc_x =
        static_cast<float32>(mouse_pos.x) / static_cast<float32>(window_size.x) * 2.0f - 1.0f;
    const float32 ndc_y =
        static_cast<float32>(mouse_pos.y) / static_cast<float32>(window_size.y) * 2.0f - 1.0f;

    const mat4f view_proj     = get_view_projection_matrix();
    const auto inv_result     = math::inverse_matrix(view_proj);
    const mat4f inv_view_proj = inv_result.value_or(math::identity_matrix());

    const auto unproject = [&inv_view_proj](float32 x, float32 y, float32 depth) -> vec3f {
        const vec4f point = inv_view_proj * vec4f{x, y, depth, 1.0f};
        if (point.w == 0.0f) {
            return vec3f{point.x, point.y, point.z};
        }
        return vec3f{point.x / point.w, point.y / point.w, point.z / point.w};
    };

    return vw::spatial::ray{unproject(ndc_x, ndc_y, 1.0f), unproject(ndc_x, ndc_y, 0.0f)};
}

}  // namespace vw::gfx
