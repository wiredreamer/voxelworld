module vw.core;

import std;

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

auto transform::from_matrix(
    const mat4f& m
) -> transform {
    transform result;
    result.position_ = vec3f{m[0, 3], m[1, 3], m[2, 3]};

    std::array<vec3f, 3> axes{
        vec3f{m[0, 0], m[1, 0], m[2, 0]},
        vec3f{m[0, 1], m[1, 1], m[2, 1]},
        vec3f{m[0, 2], m[1, 2], m[2, 2]},
    };

    vec3f scale{math::length(axes[0]), math::length(axes[1]), math::length(axes[2])};

    // Отражение поворотом не выразить: знак определителя уходит в масштаб по
    // первой оси, а поворот остаётся собственным.
    if (math::dot(math::cross(axes[0], axes[1]), axes[2]) < 0.0F) {
        scale.x = -scale.x;
    }

    for (std::size_t i = 0; i < axes.size(); ++i) {
        if (scale[i] != 0.0F) {
            axes[i] = axes[i] / scale[i];
        }
    }

    // Столбцы — оси поворота; кватернион восстанавливается по наибольшему из
    // четырёх кандидатов, чтобы не делить на число около нуля.
    const float32 r00 = axes[0].x;
    const float32 r11 = axes[1].y;
    const float32 r22 = axes[2].z;
    const float32 r21 = axes[1].z;
    const float32 r12 = axes[2].y;
    const float32 r02 = axes[2].x;
    const float32 r20 = axes[0].z;
    const float32 r10 = axes[0].y;
    const float32 r01 = axes[1].x;

    const float32 trace = r00 + r11 + r22;

    quat q;
    if (trace > 0.0F) {
        const float32 s = 0.5F / std::sqrt(trace + 1.0F);
        q = quat{(r21 - r12) * s, (r02 - r20) * s, (r10 - r01) * s, 0.25F / s};
    } else if (r00 >= r11 && r00 >= r22) {
        const float32 s = 0.5F / std::sqrt(1.0F + r00 - r11 - r22);
        q = quat{0.25F / s, (r10 + r01) * s, (r02 + r20) * s, (r21 - r12) * s};
    } else if (r11 >= r22) {
        const float32 s = 0.5F / std::sqrt(1.0F + r11 - r00 - r22);
        q = quat{(r10 + r01) * s, 0.25F / s, (r21 + r12) * s, (r02 - r20) * s};
    } else {
        const float32 s = 0.5F / std::sqrt(1.0F + r22 - r00 - r11);
        q = quat{(r02 + r20) * s, (r21 + r12) * s, 0.25F / s, (r10 - r01) * s};
    }

    result.rotation_ = math::normalize(q);
    result.scale_    = scale;
    return result;
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
