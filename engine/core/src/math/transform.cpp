module vw.core;

namespace vw {

auto transform::get_position() const -> const vec3f& {
    return position_;
}

auto transform::get_rotation() const -> const quat& {
    return rotation_;
}

auto transform::get_rotation_euler() const -> vec3f {
    return math::quat_to_euler(rotation_);
}

auto transform::get_scale() const -> const vec3f& {
    return scale_;
}

auto transform::calc_matrix() const -> mat4f {
    return math::transform_matrix(position_, rotation_, scale_);
}

auto transform::set_position(
    const vec3f& position
) -> void {
    position_ = position;
}

auto transform::set_rotation(
    const quat& rotation
) -> void {
    rotation_ = rotation;
}

auto transform::set_rotation_euler(
    const vec3f& euler
) -> void {
    rotation_ = math::euler_to_quat(euler);
}

auto transform::set_scale(
    const vec3f& scale
) -> void {
    scale_ = scale;
}

auto transform::translate(
    const vec3f& offset
) -> void {
    position_ += offset;
}

auto transform::rotate(
    const vec3f& angles
) -> void {
    rotation_ = math::euler_to_quat(angles) * rotation_;
}

auto transform::scale(
    const vec3f& factor
) -> void {
    scale_.x *= factor.x;
    scale_.y *= factor.y;
    scale_.z *= factor.z;
}

}  // namespace vw
