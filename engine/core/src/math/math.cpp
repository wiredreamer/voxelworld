module vw.core;

import std;

namespace vw::math {

auto approx_equal(
    float a, float b, float tolerance
) -> bool {
    return std::abs(a - b) < tolerance;
}

auto approx_equal(
    const vec2f& a, const vec2f& b, float tolerance
) -> bool {
    return approx_equal(a.x, b.x, tolerance) && approx_equal(a.y, b.y, tolerance);
}

auto approx_equal(
    const vec3f& a, const vec3f& b, float tolerance
) -> bool {
    return approx_equal(a.x, b.x, tolerance) && approx_equal(a.y, b.y, tolerance) &&
        approx_equal(a.z, b.z, tolerance);
}

auto approx_equal(
    const vec4f& a, const vec4f& b, float tolerance
) -> bool {
    return approx_equal(a.x, b.x, tolerance) && approx_equal(a.y, b.y, tolerance) &&
        approx_equal(a.z, b.z, tolerance) && approx_equal(a.w, b.w, tolerance);
}

auto approx_equal(
    const mat4f& a, const mat4f& b, float tolerance
) -> bool {
    for (int i = 0; i < 16; ++i) {
        if (!approx_equal(a[i], b[i], tolerance)) {
            return false;
        }
    }
    return true;
}

auto approx_equal(
    const quat& a, const quat& b, float tolerance
) -> bool {
    return approx_equal(a.x, b.x, tolerance) && approx_equal(a.y, b.y, tolerance) &&
        approx_equal(a.z, b.z, tolerance) && approx_equal(a.w, b.w, tolerance);
}

auto length(
    const vec2f& v
) -> float {
    return std::sqrt((v.x * v.x) + (v.y * v.y));
}

auto length_squared(
    const vec2f& v
) -> float {
    return (v.x * v.x) + (v.y * v.y);
}

auto normalize(
    const vec2f& v
) -> vec2f {
    float len = length(v);
    if (len > 0.0f) {
        return {v.x / len, v.y / len};
    }
    return v;
}

auto dot(
    const vec2f& a, const vec2f& b
) -> float {
    return (a.x * b.x) + (a.y * b.y);
}

auto cross(
    const vec2f& a, const vec2f& b
) -> float {
    return (a.x * b.y) - (a.y * b.x);
}

auto lerp(
    const vec2f& a, const vec2f& b, float t
) -> vec2f {
    return {a.x + (t * (b.x - a.x)), a.y + (t * (b.y - a.y))};
}

auto min(
    const vec2f& a, const vec2f& b
) -> vec2f {
    return {std::min(a.x, b.x), std::min(a.y, b.y)};
}

auto max(
    const vec2f& a, const vec2f& b
) -> vec2f {
    return {std::max(a.x, b.x), std::max(a.y, b.y)};
}

auto abs(
    const vec2f& v
) -> vec2f {
    return {std::abs(v.x), std::abs(v.y)};
}

auto floor(
    const vec2f& v
) -> vec2f {
    return {std::floor(v.x), std::floor(v.y)};
}

auto ceil(
    const vec2f& v
) -> vec2f {
    return {std::ceil(v.x), std::ceil(v.y)};
}

auto round(
    const vec2f& v
) -> vec2f {
    return {std::round(v.x), std::round(v.y)};
}

auto fract(
    const vec2f& v
) -> vec2f {
    return {v.x - std::floor(v.x), v.y - std::floor(v.y)};
}

auto sign(
    const vec2f& v
) -> vec2f {
    return {
        (v.x > 0.0f) ? 1.0f : ((v.x < 0.0f) ? -1.0f : 0.0f),
        (v.y > 0.0f) ? 1.0f : ((v.y < 0.0f) ? -1.0f : 0.0f)
    };
}

auto clamp(
    const vec2f& v, const vec2f& min_v, const vec2f& max_v
) -> vec2f {
    return {clamp(v.x, min_v.x, max_v.x), clamp(v.y, min_v.y, max_v.y)};
}

auto length(
    const vec3f& v
) -> float {
    return std::sqrt((v.x * v.x) + (v.y * v.y) + (v.z * v.z));
}

auto length_squared(
    const vec3f& v
) -> float {
    return (v.x * v.x) + (v.y * v.y) + (v.z * v.z);
}

auto normalize(
    const vec3f& v
) -> vec3f {
    float len = length(v);
    if (len > 0.0f) {
        return {v.x / len, v.y / len, v.z / len};
    }
    return v;
}

auto cross(
    const vec3f& a, const vec3f& b
) -> vec3f {
    return {(a.y * b.z) - (a.z * b.y), (a.z * b.x) - (a.x * b.z), (a.x * b.y) - (a.y * b.x)};
}

auto dot(
    const vec3f& a, const vec3f& b
) -> float {
    return (a.x * b.x) + (a.y * b.y) + (a.z * b.z);
}

auto min(
    const vec3f& a, const vec3f& b
) -> vec3f {
    return {std::min(a.x, b.x), std::min(a.y, b.y), std::min(a.z, b.z)};
}

auto max(
    const vec3f& a, const vec3f& b
) -> vec3f {
    return {std::max(a.x, b.x), std::max(a.y, b.y), std::max(a.z, b.z)};
}

auto abs(
    const vec3f& v
) -> vec3f {
    return {std::abs(v.x), std::abs(v.y), std::abs(v.z)};
}

auto floor(
    const vec3f& v
) -> vec3f {
    return {std::floor(v.x), std::floor(v.y), std::floor(v.z)};
}

auto ceil(
    const vec3f& v
) -> vec3f {
    return {std::ceil(v.x), std::ceil(v.y), std::ceil(v.z)};
}

auto round(
    const vec3f& v
) -> vec3f {
    return {std::round(v.x), std::round(v.y), std::round(v.z)};
}

auto fract(
    const vec3f& v
) -> vec3f {
    return {v.x - std::floor(v.x), v.y - std::floor(v.y), v.z - std::floor(v.z)};
}

auto sign(
    const vec3f& v
) -> vec3f {
    return {
        (v.x > 0.0f) ? 1.0f : ((v.x < 0.0f) ? -1.0f : 0.0f),
        (v.y > 0.0f) ? 1.0f : ((v.y < 0.0f) ? -1.0f : 0.0f),
        (v.z > 0.0f) ? 1.0f : ((v.z < 0.0f) ? -1.0f : 0.0f)
    };
}

auto clamp(
    const vec3f& v, const vec3f& min_v, const vec3f& max_v
) -> vec3f {
    return {
        clamp(v.x, min_v.x, max_v.x), clamp(v.y, min_v.y, max_v.y), clamp(v.z, min_v.z, max_v.z)
    };
}

auto distance(
    const vec3f& a, const vec3f& b
) -> float {
    return length(b - a);
}

auto distance_squared(
    const vec3f& a, const vec3f& b
) -> float {
    return length_squared(b - a);
}

auto angle(
    const vec3f& a, const vec3f& b
) -> float {
    float len_a = length(a);
    float len_b = length(b);
    if (len_a > 0.0f && len_b > 0.0f) {
        float cos_angle = clamp(dot(a, b) / (len_a * len_b), -1.0f, 1.0f);
        return std::acos(cos_angle);
    }
    return 0.0f;
}

auto reflect(
    const vec3f& incident, const vec3f& normal
) -> vec3f {
    return incident - normal * (2.0f * dot(incident, normal));
}

auto refract(
    const vec3f& incident, const vec3f& normal, float eta
) -> vec3f {
    float cos_i  = -dot(incident, normal);
    float sin2_t = (eta * eta) * (1.0f - (cos_i * cos_i));
    if (sin2_t > 1.0f) {
        return reflect(incident, normal);
    }
    float cos_t = std::sqrt(1.0f - sin2_t);
    return (incident * eta) + (normal * ((eta * cos_i) - cos_t));
}

auto project(
    const vec3f& a, const vec3f& onto
) -> vec3f {
    float len_sq = length_squared(onto);
    if (len_sq > 0.0f) {
        return onto * (dot(a, onto) / len_sq);
    }
    return vec3f{0.0f, 0.0f, 0.0f};
}

auto reject(
    const vec3f& a, const vec3f& from
) -> vec3f {
    return a - project(a, from);
}

auto length(
    const vec4f& v
) -> float {
    return std::sqrt((v.x * v.x) + (v.y * v.y) + (v.z * v.z) + (v.w * v.w));
}

auto length_squared(
    const vec4f& v
) -> float {
    return (v.x * v.x) + (v.y * v.y) + (v.z * v.z) + (v.w * v.w);
}

auto normalize(
    const vec4f& v
) -> vec4f {
    float len = length(v);
    if (len > 0.0f) {
        return {v.x / len, v.y / len, v.z / len, v.w / len};
    }
    return v;
}

auto dot(
    const vec4f& a, const vec4f& b
) -> float {
    return (a.x * b.x) + (a.y * b.y) + (a.z * b.z) + (a.w * b.w);
}

auto lerp(
    const vec4f& a, const vec4f& b, float t
) -> vec4f {
    return {
        a.x + (t * (b.x - a.x)),
        a.y + (t * (b.y - a.y)),
        a.z + (t * (b.z - a.z)),
        a.w + (t * (b.w - a.w))
    };
}

auto min(
    const vec4f& a, const vec4f& b
) -> vec4f {
    return {std::min(a.x, b.x), std::min(a.y, b.y), std::min(a.z, b.z), std::min(a.w, b.w)};
}

auto max(
    const vec4f& a, const vec4f& b
) -> vec4f {
    return {std::max(a.x, b.x), std::max(a.y, b.y), std::max(a.z, b.z), std::max(a.w, b.w)};
}

auto abs(
    const vec4f& v
) -> vec4f {
    return {std::abs(v.x), std::abs(v.y), std::abs(v.z), std::abs(v.w)};
}

auto floor(
    const vec4f& v
) -> vec4f {
    return {std::floor(v.x), std::floor(v.y), std::floor(v.z), std::floor(v.w)};
}

auto ceil(
    const vec4f& v
) -> vec4f {
    return {std::ceil(v.x), std::ceil(v.y), std::ceil(v.z), std::ceil(v.w)};
}

auto round(
    const vec4f& v
) -> vec4f {
    return {std::round(v.x), std::round(v.y), std::round(v.z), std::round(v.w)};
}

auto fract(
    const vec4f& v
) -> vec4f {
    return {
        v.x - std::floor(v.x), v.y - std::floor(v.y), v.z - std::floor(v.z), v.w - std::floor(v.w)
    };
}

auto sign(
    const vec4f& v
) -> vec4f {
    return {
        (v.x > 0.0f) ? 1.0f : ((v.x < 0.0f) ? -1.0f : 0.0f),
        (v.y > 0.0f) ? 1.0f : ((v.y < 0.0f) ? -1.0f : 0.0f),
        (v.z > 0.0f) ? 1.0f : ((v.z < 0.0f) ? -1.0f : 0.0f),
        (v.w > 0.0f) ? 1.0f : ((v.w < 0.0f) ? -1.0f : 0.0f)
    };
}

auto clamp(
    const vec4f& v, const vec4f& min_v, const vec4f& max_v
) -> vec4f {
    return {
        clamp(v.x, min_v.x, max_v.x),
        clamp(v.y, min_v.y, max_v.y),
        clamp(v.z, min_v.z, max_v.z),
        clamp(v.w, min_v.w, max_v.w)
    };
}

auto perspective_matrix(
    float fov, float aspect, float near, float far
) -> mat4f {
    mat4f matrix  = identity_matrix();
    const float f = 1.0f / std::tan(radians(fov * 0.5f));

    matrix[0, 0] = f / aspect;
    matrix[1, 1] = -f;
    matrix[2, 2] = far / (near - far);
    matrix[2, 3] = (near * far) / (near - far);
    matrix[3, 2] = -1.0f;
    matrix[3, 3] = 0.0f;

    return matrix;
}

auto perspective_matrix_reversed(
    float fov, float aspect, float near, float far
) -> mat4f {
    mat4f matrix  = identity_matrix();
    const float f = 1.0f / std::tan(radians(fov * 0.5f));

    matrix[0, 0] = f / aspect;
    matrix[1, 1] = -f;
    matrix[2, 2] = near / (far - near);
    matrix[2, 3] = (near * far) / (far - near);
    matrix[3, 2] = -1.0f;
    matrix[3, 3] = 0.0f;

    return matrix;
}

auto orthographic_matrix(
    float left, float right, float bottom, float top, float near, float far
) -> mat4f {
    mat4f matrix = identity_matrix();

    matrix[0, 0] = 2.0f / (right - left);
    matrix[1, 1] = 2.0f / (top - bottom);
    matrix[2, 2] = -1.0f / (far - near);
    matrix[0, 3] = -(right + left) / (right - left);
    matrix[1, 3] = -(top + bottom) / (top - bottom);
    matrix[2, 3] = -near / (far - near);

    return matrix;
}

auto orthographic_matrix_reversed(
    float width, float height, float near, float far
) -> mat4f {
    mat4f matrix = identity_matrix();

    matrix[0, 0] = 2.0f / width;
    matrix[1, 1] = -2.0f / height;
    matrix[2, 2] = 1.0f / (far - near);
    matrix[2, 3] = far / (far - near);

    return matrix;
}

auto look_at_matrix(
    const vec3f& eye, const vec3f& target
) -> mat4f {
    mat4f matrix  = identity_matrix();
    const vec3f f = normalize(target - eye);
    auto up       = vec3f{0.0f, 1.0f, 0.0f};
    if (std::abs(dot(f, up)) > 0.999f) {
        up = vec3f{1.0f, 0.0f, 0.0f};
    }

    const vec3f s = normalize(cross(up, f));
    const vec3f u = cross(f, s);

    matrix[0, 0] = s.x;
    matrix[0, 1] = s.y;
    matrix[0, 2] = s.z;
    matrix[0, 3] = -dot(s, eye);

    matrix[1, 0] = u.x;
    matrix[1, 1] = u.y;
    matrix[1, 2] = u.z;
    matrix[1, 3] = -dot(u, eye);

    matrix[2, 0] = -f.x;
    matrix[2, 1] = -f.y;
    matrix[2, 2] = -f.z;
    matrix[2, 3] = dot(f, eye);

    return matrix;
}

auto look_at_matrix(
    const vec3f& eye, const vec3f& target, const vec3f& up
) -> mat4f {
    mat4f matrix  = identity_matrix();
    const vec3f f = normalize(target - eye);
    const vec3f s = normalize(cross(up, f));
    const vec3f u = cross(f, s);

    matrix[0, 0] = s.x;
    matrix[0, 1] = s.y;
    matrix[0, 2] = s.z;
    matrix[0, 3] = -dot(s, eye);

    matrix[1, 0] = u.x;
    matrix[1, 1] = u.y;
    matrix[1, 2] = u.z;
    matrix[1, 3] = -dot(u, eye);

    matrix[2, 0] = -f.x;
    matrix[2, 1] = -f.y;
    matrix[2, 2] = -f.z;
    matrix[2, 3] = dot(f, eye);

    return matrix;
}

auto identity_matrix() -> mat4f {
    mat4f matrix;
    matrix[0, 0] = 1.0f;
    matrix[1, 1] = 1.0f;
    matrix[2, 2] = 1.0f;
    matrix[3, 3] = 1.0f;
    return matrix;
}

auto translation_matrix(
    const vec3f& translation
) -> mat4f {
    mat4f matrix = identity_matrix();
    matrix[0, 3] = translation.x;
    matrix[1, 3] = translation.y;
    matrix[2, 3] = translation.z;
    return matrix;
}

auto rotation_matrix_x(
    float angle
) -> mat4f {
    mat4f matrix  = identity_matrix();
    const float c = std::cos(angle);
    const float s = std::sin(angle);

    matrix[1, 1] = c;
    matrix[1, 2] = s;
    matrix[2, 1] = -s;
    matrix[2, 2] = c;

    return matrix;
}

auto rotation_matrix_y(
    float angle
) -> mat4f {
    mat4f matrix  = identity_matrix();
    const float c = std::cos(angle);
    const float s = std::sin(angle);

    matrix[0, 0] = c;
    matrix[0, 2] = -s;
    matrix[2, 0] = s;
    matrix[2, 2] = c;

    return matrix;
}

auto rotation_matrix_z(
    float angle
) -> mat4f {
    mat4f matrix  = identity_matrix();
    const float c = std::cos(angle);
    const float s = std::sin(angle);

    matrix[0, 0] = c;
    matrix[0, 1] = s;
    matrix[1, 0] = -s;
    matrix[1, 1] = c;

    return matrix;
}

auto rotation_matrix(
    const vec3f& rotation
) -> mat4f {
    const mat4f rot_x = rotation_matrix_x(rotation.x);
    const mat4f rot_y = rotation_matrix_y(rotation.y);
    const mat4f rot_z = rotation_matrix_z(rotation.z);

    return rot_z * rot_y * rot_x;
}

auto rotation_matrix(
    const quat& q
) -> mat4f {
    mat4f matrix = identity_matrix();

    float xx = q.x * q.x;
    float yy = q.y * q.y;
    float zz = q.z * q.z;
    float xy = q.x * q.y;
    float xz = q.x * q.z;
    float yz = q.y * q.z;
    float wx = q.w * q.x;
    float wy = q.w * q.y;
    float wz = q.w * q.z;

    matrix[0, 0] = 1.0f - 2.0f * (yy + zz);
    matrix[0, 1] = 2.0f * (xy - wz);
    matrix[0, 2] = 2.0f * (xz + wy);

    matrix[1, 0] = 2.0f * (xy + wz);
    matrix[1, 1] = 1.0f - 2.0f * (xx + zz);
    matrix[1, 2] = 2.0f * (yz - wx);

    matrix[2, 0] = 2.0f * (xz - wy);
    matrix[2, 1] = 2.0f * (yz + wx);
    matrix[2, 2] = 1.0f - 2.0f * (xx + yy);

    return matrix;
}

auto scale_matrix(
    const vec3f& scale
) -> mat4f {
    mat4f matrix = identity_matrix();
    matrix[0, 0] = scale.x;
    matrix[1, 1] = scale.y;
    matrix[2, 2] = scale.z;
    return matrix;
}

auto transform_matrix(
    const vec3f& position, const vec3f& rotation, const vec3f& scale
) -> mat4f {
    return translation_matrix(position) * rotation_matrix(rotation) * scale_matrix(scale);
}

auto transform_matrix(
    const vec3f& position, const quat& rotation, const vec3f& scale
) -> mat4f {
    return translation_matrix(position) * rotation_matrix(rotation) * scale_matrix(scale);
}

auto transpose_matrix(
    const mat4f& matrix
) -> mat4f {
    mat4f result;
    for (int i = 0; i < 4; i++) {
        for (int j = 0; j < 4; j++) {
            result[i, j] = matrix[j, i];
        }
    }
    return result;
}

auto lerp(
    float a, float b, float t
) -> float {
    return a + (t * (b - a));
}

auto lerp(
    const vec3f& a, const vec3f& b, float t
) -> vec3f {
    return {a.x + (t * (b.x - a.x)), a.y + (t * (b.y - a.y)), a.z + (t * (b.z - a.z))};
}

auto lerp(
    const transform& a, const transform& b, float t
) -> transform {
    transform result;
    result.set_position(lerp(a.get_position(), b.get_position(), t));
    result.set_rotation(slerp(a.get_rotation(), b.get_rotation(), t));
    result.set_scale(lerp(a.get_scale(), b.get_scale(), t));
    return result;
}

auto smoothstep(
    float edge0, float edge1, float x
) -> float {
    float t = clamp((x - edge0) / (edge1 - edge0), 0.0f, 1.0f);
    return (t * t) * (3.0f - (2.0f * t));
}

auto smoothstep(
    const vec2f& edge0, const vec2f& edge1, const vec2f& x
) -> vec2f {
    return {smoothstep(edge0.x, edge1.x, x.x), smoothstep(edge0.y, edge1.y, x.y)};
}

auto smoothstep(
    const vec3f& edge0, const vec3f& edge1, const vec3f& x
) -> vec3f {
    return {
        smoothstep(edge0.x, edge1.x, x.x),
        smoothstep(edge0.y, edge1.y, x.y),
        smoothstep(edge0.z, edge1.z, x.z)
    };
}

auto smoothstep(
    const vec4f& edge0, const vec4f& edge1, const vec4f& x
) -> vec4f {
    return {
        smoothstep(edge0.x, edge1.x, x.x),
        smoothstep(edge0.y, edge1.y, x.y),
        smoothstep(edge0.z, edge1.z, x.z),
        smoothstep(edge0.w, edge1.w, x.w)
    };
}

auto ease_in(
    const vec3f& a, const vec3f& b, float t
) -> vec3f {
    float eased_t = t * t;
    return lerp(a, b, eased_t);
}

auto ease_out(
    const vec3f& a, const vec3f& b, float t
) -> vec3f {
    float eased_t = t * (2.0f - t);
    return lerp(a, b, eased_t);
}

auto ease_in_out(
    const vec3f& a, const vec3f& b, float t
) -> vec3f {
    float eased_t;
    if (t < 0.5f) {
        eased_t = 2.0f * t * t;
    } else {
        eased_t = -1.0f + ((4.0f - (2.0f * t)) * t);
    }
    return lerp(a, b, eased_t);
}

auto evaluate_cubic_bezier(
    float t, float p0, float p1, float p2, float p3
) -> float {
    float one_minus_t    = 1.0f - t;
    float one_minus_t_sq = one_minus_t * one_minus_t;
    float one_minus_t_cb = one_minus_t_sq * one_minus_t;
    float t_sq           = t * t;
    float t_cb           = t_sq * t;

    return (one_minus_t_cb * p0) + (3.0f * one_minus_t_sq * t * p1) +
        (3.0f * one_minus_t * t_sq * p2) + (t_cb * p3);
}

auto lerp(
    uint8 a, uint8 b, float t
) -> uint8 {
    auto fa = static_cast<float>(a);
    auto fb = static_cast<float>(b);
    return static_cast<uint8>(fa + (t * (fb - fa)));
}

auto cubic_bezier(
    const vec3f& a, const vec3f& b, float t, float control1, float control2
) -> vec3f {
    float bezier_t = evaluate_cubic_bezier(t, 0.0f, control1, control2, 1.0f);
    return lerp(a, b, bezier_t);
}

auto apply_easing(
    float t, interpolation_type type
) -> float {
    t = clamp(t, 0.0f, 1.0f);

    switch (type) {
        case interpolation_type::linear:
            return t;

        case interpolation_type::step:
            return t < 1.0f ? 0.0f : 1.0f;

        case interpolation_type::ease_in:
            return t * t;

        case interpolation_type::ease_out:
            return t * (2.0f - t);

        case interpolation_type::ease_in_out:
            if (t < 0.5f) {
                return 2.0f * t * t;
            }
            return -1.0f + ((4.0f - (2.0f * t)) * t);

        case interpolation_type::cubic_bezier:
            return t;

        default:
            return t;
    }
}

auto apply_easing_bezier(
    float t, interpolation_type type, float control1, float control2
) -> float {
    if (type == interpolation_type::cubic_bezier) {
        t = clamp(t, 0.0f, 1.0f);
        return evaluate_cubic_bezier(t, 0.0f, control1, control2, 1.0f);
    }
    return apply_easing(t, type);
}

auto interpolate(
    const vec3f& a, const vec3f& b, float t, interpolation_type type, float control1, float control2
) -> vec3f {
    switch (type) {
        case interpolation_type::linear:
            return lerp(a, b, t);

        case interpolation_type::step:
            return t < 1.0f ? a : b;

        case interpolation_type::ease_in:
            return ease_in(a, b, t);

        case interpolation_type::ease_out:
            return ease_out(a, b, t);

        case interpolation_type::ease_in_out:
            return ease_in_out(a, b, t);

        case interpolation_type::cubic_bezier:
            return cubic_bezier(a, b, t, control1, control2);

        default:
            return lerp(a, b, t);
    }
}

auto interpolate(
    color a, color b, float t, interpolation_type type, float control1, float control2
) -> color {
    if (type == interpolation_type::step) {
        const auto r = t < 1.0f ? a : b;
        return r;
    }

    float eased_t = apply_easing_bezier(t, type, control1, control2);

    uint8 r   = lerp(a.r(), b.r(), eased_t);
    uint8 g   = lerp(a.g(), b.g(), eased_t);
    uint8 b_c = lerp(a.b(), b.b(), eased_t);
    uint8 a_c = lerp(a.a(), b.a(), eased_t);

    return {r, g, b_c, a_c};
}

auto dot(
    const quat& a, const quat& b
) -> float {
    return (a.x * b.x) + (a.y * b.y) + (a.z * b.z) + (a.w * b.w);
}

auto normalize(
    const quat& q
) -> quat {
    const float len = std::sqrt(dot(q, q));
    if (len > 0.0f) {
        const float inv = 1.0f / len;
        return {q.x * inv, q.y * inv, q.z * inv, q.w * inv};
    }
    return q;
}

auto slerp(
    const quat& a, const quat& b, float t
) -> quat {
    float d = dot(a, b);

    quat b2 = b;
    if (d < 0.0f) {
        b2 = -b;
        d  = -d;
    }

    if (d > 0.9995f) {
        const auto r = normalize(
            quat{
                a.x + (t * (b2.x - a.x)),
                a.y + (t * (b2.y - a.y)),
                a.z + (t * (b2.z - a.z)),
                a.w + (t * (b2.w - a.w))
            }
        );
        return r;
    }

    const float theta_0     = std::acos(d);
    const float theta       = theta_0 * t;
    const float sin_theta   = std::sin(theta);
    const float sin_theta_0 = std::sin(theta_0);

    const float s0 = std::cos(theta) - (d * sin_theta / sin_theta_0);
    const float s1 = sin_theta / sin_theta_0;

    return {
        (a.x * s0) + (b2.x * s1),
        (a.y * s0) + (b2.y * s1),
        (a.z * s0) + (b2.z * s1),
        (a.w * s0) + (b2.w * s1)
    };
}

auto euler_to_quat(
    const vec3f& euler
) -> quat {
    float cx = std::cos(-euler.x * 0.5f);
    float sx = std::sin(-euler.x * 0.5f);
    float cy = std::cos(-euler.y * 0.5f);
    float sy = std::sin(-euler.y * 0.5f);
    float cz = std::cos(-euler.z * 0.5f);
    float sz = std::sin(-euler.z * 0.5f);

    return {
        (sx * cy * cz) - (cx * sy * sz),
        (cx * sy * cz) + (sx * cy * sz),
        (cx * cy * sz) - (sx * sy * cz),
        (cx * cy * cz) + (sx * sy * sz)
    };
}

auto quat_to_euler(
    const quat& q
) -> vec3f {
    const float sinr_cosp = 2.0f * ((q.w * q.x) + (q.y * q.z));
    const float cosr_cosp = 1.0f - (2.0f * ((q.x * q.x) + (q.y * q.y)));
    const float rx        = std::atan2(sinr_cosp, cosr_cosp);

    const float sinp = 2.0f * ((q.w * q.y) - (q.z * q.x));
    float ry;
    if (std::abs(sinp) >= 1.0f) {
        ry = std::copysign(pi * 0.5f, sinp);
    } else {
        ry = std::asin(sinp);
    }

    const float siny_cosp = 2.0f * ((q.w * q.z) + (q.x * q.y));
    const float cosy_cosp = 1.0f - (2.0f * ((q.y * q.y) + (q.z * q.z)));
    const float rz        = std::atan2(siny_cosp, cosy_cosp);

    return {-rx, -ry, -rz};
}

auto interpolate(
    const quat& a, const quat& b, float t, interpolation_type type, float control1, float control2
) -> quat {
    float eased_t = apply_easing_bezier(t, type, control1, control2);
    return slerp(a, b, eased_t);
}

auto quat_look_y(
    const vec3f& direction
) -> quat {
    float yaw = -std::atan2(direction.x, direction.z);
    return euler_to_quat({0.0f, yaw, 0.0f});
}

auto perpendicular(
    const vec3f& eye, const vec3f& target
) -> vec3f {
    auto dir = normalize(target - eye);
    auto up  = vec3f{0.0f, 1.0f, 0.0f};

    if (std::abs(dot(dir, up)) > 0.999f) {
        up = vec3f{1.0f, 0.0f, 0.0f};
    }

    return normalize(cross(up, dir));
}

auto is_safe_zero(
    float a, float tolerance
) -> bool {
    return std::abs(a) < tolerance;
}

auto inverse_matrix(
    const mat4f& matrix
) -> std::expected<mat4f, error_type> {
    mat4f inv;

    inv[0, 0] =  //
        (matrix[1, 1] * (matrix[2, 2] * matrix[3, 3] - matrix[2, 3] * matrix[3, 2])) -
        (matrix[1, 2] * (matrix[2, 1] * matrix[3, 3] - matrix[2, 3] * matrix[3, 1])) +
        (matrix[1, 3] * (matrix[2, 1] * matrix[3, 2] - matrix[2, 2] * matrix[3, 1]));

    inv[0, 1] =  //
        -((matrix[0, 1] * (matrix[2, 2] * matrix[3, 3] - matrix[2, 3] * matrix[3, 2])) -
          (matrix[0, 2] * (matrix[2, 1] * matrix[3, 3] - matrix[2, 3] * matrix[3, 1])) +
          (matrix[0, 3] * (matrix[2, 1] * matrix[3, 2] - matrix[2, 2] * matrix[3, 1])));

    inv[0, 2] =  //
        (matrix[0, 1] * (matrix[1, 2] * matrix[3, 3] - matrix[1, 3] * matrix[3, 2])) -
        (matrix[0, 2] * (matrix[1, 1] * matrix[3, 3] - matrix[1, 3] * matrix[3, 1])) +
        (matrix[0, 3] * (matrix[1, 1] * matrix[3, 2] - matrix[1, 2] * matrix[3, 1]));

    inv[0, 3] =  //
        -((matrix[0, 1] * (matrix[1, 2] * matrix[2, 3] - matrix[1, 3] * matrix[2, 2])) -
          (matrix[0, 2] * (matrix[1, 1] * matrix[2, 3] - matrix[1, 3] * matrix[2, 1])) +
          (matrix[0, 3] * (matrix[1, 1] * matrix[2, 2] - matrix[1, 2] * matrix[2, 1])));

    inv[1, 0] =  //
        -((matrix[1, 0] * (matrix[2, 2] * matrix[3, 3] - matrix[2, 3] * matrix[3, 2])) -
          (matrix[1, 2] * (matrix[2, 0] * matrix[3, 3] - matrix[2, 3] * matrix[3, 0])) +
          (matrix[1, 3] * (matrix[2, 0] * matrix[3, 2] - matrix[2, 2] * matrix[3, 0])));

    inv[1, 1] =  //
        (matrix[0, 0] * (matrix[2, 2] * matrix[3, 3] - matrix[2, 3] * matrix[3, 2])) -
        (matrix[0, 2] * (matrix[2, 0] * matrix[3, 3] - matrix[2, 3] * matrix[3, 0])) +
        (matrix[0, 3] * (matrix[2, 0] * matrix[3, 2] - matrix[2, 2] * matrix[3, 0]));

    inv[1, 2] =  //
        -((matrix[0, 0] * (matrix[1, 2] * matrix[3, 3] - matrix[1, 3] * matrix[3, 2])) -
          (matrix[0, 2] * (matrix[1, 0] * matrix[3, 3] - matrix[1, 3] * matrix[3, 0])) +
          (matrix[0, 3] * (matrix[1, 0] * matrix[3, 2] - matrix[1, 2] * matrix[3, 0])));

    inv[1, 3] =  //
        (matrix[0, 0] * (matrix[1, 2] * matrix[2, 3] - matrix[1, 3] * matrix[2, 2])) -
        (matrix[0, 2] * (matrix[1, 0] * matrix[2, 3] - matrix[1, 3] * matrix[2, 0])) +
        (matrix[0, 3] * (matrix[1, 0] * matrix[2, 2] - matrix[1, 2] * matrix[2, 0]));

    inv[2, 0] =  //
        (matrix[1, 0] * (matrix[2, 1] * matrix[3, 3] - matrix[2, 3] * matrix[3, 1])) -
        (matrix[1, 1] * (matrix[2, 0] * matrix[3, 3] - matrix[2, 3] * matrix[3, 0])) +
        (matrix[1, 3] * (matrix[2, 0] * matrix[3, 1] - matrix[2, 1] * matrix[3, 0]));

    inv[2, 1] =  //
        -((matrix[0, 0] * (matrix[2, 1] * matrix[3, 3] - matrix[2, 3] * matrix[3, 1])) -
          (matrix[0, 1] * (matrix[2, 0] * matrix[3, 3] - matrix[2, 3] * matrix[3, 0])) +
          (matrix[0, 3] * (matrix[2, 0] * matrix[3, 1] - matrix[2, 1] * matrix[3, 0])));

    inv[2, 2] =  //
        (matrix[0, 0] * (matrix[1, 1] * matrix[3, 3] - matrix[1, 3] * matrix[3, 1])) -
        (matrix[0, 1] * (matrix[1, 0] * matrix[3, 3] - matrix[1, 3] * matrix[3, 0])) +
        (matrix[0, 3] * (matrix[1, 0] * matrix[3, 1] - matrix[1, 1] * matrix[3, 0]));

    inv[2, 3] =  //
        -((matrix[0, 0] * (matrix[1, 1] * matrix[2, 3] - matrix[1, 3] * matrix[2, 1])) -
          (matrix[0, 1] * (matrix[1, 0] * matrix[2, 3] - matrix[1, 3] * matrix[2, 0])) +
          (matrix[0, 3] * (matrix[1, 0] * matrix[2, 1] - matrix[1, 1] * matrix[2, 0])));

    inv[3, 0] =  //
        -((matrix[1, 0] * (matrix[2, 1] * matrix[3, 2] - matrix[2, 2] * matrix[3, 1])) -
          (matrix[1, 1] * (matrix[2, 0] * matrix[3, 2] - matrix[2, 2] * matrix[3, 0])) +
          (matrix[1, 2] * (matrix[2, 0] * matrix[3, 1] - matrix[2, 1] * matrix[3, 0])));

    inv[3, 1] =  //
        (matrix[0, 0] * (matrix[2, 1] * matrix[3, 2] - matrix[2, 2] * matrix[3, 1])) -
        (matrix[0, 1] * (matrix[2, 0] * matrix[3, 2] - matrix[2, 2] * matrix[3, 0])) +
        (matrix[0, 2] * (matrix[2, 0] * matrix[3, 1] - matrix[2, 1] * matrix[3, 0]));

    inv[3, 2] =  //
        -((matrix[0, 0] * (matrix[1, 1] * matrix[3, 2] - matrix[1, 2] * matrix[3, 1])) -
          (matrix[0, 1] * (matrix[1, 0] * matrix[3, 2] - matrix[1, 2] * matrix[3, 0])) +
          (matrix[0, 2] * (matrix[1, 0] * matrix[3, 1] - matrix[1, 1] * matrix[3, 0])));

    inv[3, 3] =  //
        (matrix[0, 0] * (matrix[1, 1] * matrix[2, 2] - matrix[1, 2] * matrix[2, 1])) -
        (matrix[0, 1] * (matrix[1, 0] * matrix[2, 2] - matrix[1, 2] * matrix[2, 0])) +
        (matrix[0, 2] * (matrix[1, 0] * matrix[2, 1] - matrix[1, 1] * matrix[2, 0]));

    float det =                       //
        (matrix[0, 0] * inv[0, 0]) +  //
        (matrix[0, 1] * inv[1, 0]) +  //
        (matrix[0, 2] * inv[2, 0]) +  //
        (matrix[0, 3] * inv[3, 0]);

    if (is_safe_zero(det)) {
        return std::unexpected{error_type::zero_determinant};
    }

    float inv_det = 1.0f / det;
    for (int i = 0; i < 4; i++) {
        for (int j = 0; j < 4; j++) {
            inv[i, j] *= inv_det;
        }
    }

    return inv;
}

}  // namespace vw::math
