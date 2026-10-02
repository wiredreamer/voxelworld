export module vw.core:math;

import std;

import :types;
import :vector;
import :matrix;
import :transform;
import :color;

export namespace vw::math {

enum class error_type {
    zero_determinant,
};

enum class interpolation_type : uint8 {
    linear,
    step,
    ease_in,
    ease_out,
    ease_in_out,
    cubic_bezier
};

constexpr float pi         = 3.14159265359f;
constexpr float deg_to_rad = pi / 180.0f;
constexpr float rad_to_deg = 180.0f / pi;

constexpr float epsilon = std::numeric_limits<float>::epsilon();

constexpr auto radians(float degrees) -> float;
constexpr auto degrees(float radians) -> float;

template <typename T>
constexpr auto clamp(T value, T min_val, T max_val) -> T;

template <typename T>
constexpr auto sign(T value) -> T;

auto length(const vec2f& v) -> float;
auto length_squared(const vec2f& v) -> float;
auto normalize(const vec2f& v) -> vec2f;
auto dot(const vec2f& a, const vec2f& b) -> float;
auto cross(const vec2f& a, const vec2f& b) -> float;
auto lerp(const vec2f& a, const vec2f& b, float t) -> vec2f;

auto min(const vec2f& a, const vec2f& b) -> vec2f;
auto max(const vec2f& a, const vec2f& b) -> vec2f;
auto abs(const vec2f& v) -> vec2f;
auto floor(const vec2f& v) -> vec2f;
auto ceil(const vec2f& v) -> vec2f;
auto round(const vec2f& v) -> vec2f;
auto fract(const vec2f& v) -> vec2f;
auto sign(const vec2f& v) -> vec2f;
auto clamp(const vec2f& v, const vec2f& min_v, const vec2f& max_v) -> vec2f;

auto length(const vec3f& v) -> float;
auto length_squared(const vec3f& v) -> float;
auto normalize(const vec3f& v) -> vec3f;
auto cross(const vec3f& a, const vec3f& b) -> vec3f;
auto dot(const vec3f& a, const vec3f& b) -> float;
auto lerp(const vec3f& a, const vec3f& b, float t) -> vec3f;

auto min(const vec3f& a, const vec3f& b) -> vec3f;
auto max(const vec3f& a, const vec3f& b) -> vec3f;
auto abs(const vec3f& v) -> vec3f;
auto floor(const vec3f& v) -> vec3f;
auto ceil(const vec3f& v) -> vec3f;
auto round(const vec3f& v) -> vec3f;
auto fract(const vec3f& v) -> vec3f;
auto sign(const vec3f& v) -> vec3f;
auto clamp(const vec3f& v, const vec3f& min_v, const vec3f& max_v) -> vec3f;

auto distance(const vec3f& a, const vec3f& b) -> float;
auto distance_squared(const vec3f& a, const vec3f& b) -> float;
auto angle(const vec3f& a, const vec3f& b) -> float;
auto reflect(const vec3f& incident, const vec3f& normal) -> vec3f;
auto refract(const vec3f& incident, const vec3f& normal, float eta) -> vec3f;
auto project(const vec3f& a, const vec3f& onto) -> vec3f;
auto reject(const vec3f& a, const vec3f& from) -> vec3f;

auto length(const vec4f& v) -> float;
auto length_squared(const vec4f& v) -> float;
auto normalize(const vec4f& v) -> vec4f;
auto dot(const vec4f& a, const vec4f& b) -> float;
auto lerp(const vec4f& a, const vec4f& b, float t) -> vec4f;

auto min(const vec4f& a, const vec4f& b) -> vec4f;
auto max(const vec4f& a, const vec4f& b) -> vec4f;
auto abs(const vec4f& v) -> vec4f;
auto floor(const vec4f& v) -> vec4f;
auto ceil(const vec4f& v) -> vec4f;
auto round(const vec4f& v) -> vec4f;
auto fract(const vec4f& v) -> vec4f;
auto sign(const vec4f& v) -> vec4f;
auto clamp(const vec4f& v, const vec4f& min_v, const vec4f& max_v) -> vec4f;

auto approx_equal(float a, float b, float tolerance = 1e-5f) -> bool;
auto approx_equal(const vec2f& a, const vec2f& b, float tolerance = 1e-5f) -> bool;
auto approx_equal(const vec3f& a, const vec3f& b, float tolerance = 1e-5f) -> bool;
auto approx_equal(const vec4f& a, const vec4f& b, float tolerance = 1e-5f) -> bool;
auto approx_equal(const mat4f& a, const mat4f& b, float tolerance = 1e-5f) -> bool;
auto approx_equal(const quat& a, const quat& b, float tolerance = 1e-5f) -> bool;

auto lerp(float a, float b, float t) -> float;
auto lerp(const transform& a, const transform& b, float t) -> transform;

auto ease_in(const vec3f& a, const vec3f& b, float t) -> vec3f;
auto ease_out(const vec3f& a, const vec3f& b, float t) -> vec3f;
auto ease_in_out(const vec3f& a, const vec3f& b, float t) -> vec3f;
auto evaluate_cubic_bezier(float t, float p0, float p1, float p2, float p3) -> float;
auto cubic_bezier(const vec3f& a, const vec3f& b, float t, float control1, float control2) -> vec3f;
auto apply_easing(float t, interpolation_type type) -> float;
auto apply_easing_bezier(float t, interpolation_type type, float control1, float control2) -> float;
auto lerp(uint8 a, uint8 b, float t) -> uint8;
auto interpolate(
    const vec3f& a,
    const vec3f& b,
    float t,
    interpolation_type type,
    float control1 = 0.0f,
    float control2 = 1.0f
) -> vec3f;
auto interpolate(
    color a, color b, float t, interpolation_type type, float control1 = 0.0f, float control2 = 1.0f
) -> color;

auto dot(const quat& a, const quat& b) -> float;
auto normalize(const quat& q) -> quat;
auto slerp(const quat& a, const quat& b, float t) -> quat;
auto euler_to_quat(const vec3f& euler) -> quat;
auto quat_to_euler(const quat& q) -> vec3f;
auto interpolate(
    const quat& a,
    const quat& b,
    float t,
    interpolation_type type,
    float control1 = 0.0f,
    float control2 = 1.0f
) -> quat;

auto quat_look_y(const vec3f& direction) -> quat;

auto perpendicular(const vec3f& eye, const vec3f& target) -> vec3f;
auto is_safe_zero(float a, float tolerance = 1e-5f) -> bool;

auto smoothstep(float edge0, float edge1, float x) -> float;
auto smoothstep(const vec2f& edge0, const vec2f& edge1, const vec2f& x) -> vec2f;
auto smoothstep(const vec3f& edge0, const vec3f& edge1, const vec3f& x) -> vec3f;
auto smoothstep(const vec4f& edge0, const vec4f& edge1, const vec4f& x) -> vec4f;

inline auto mix(float a, float b, float t) -> float { return lerp(a, b, t); }
inline auto mix(const vec2f& a, const vec2f& b, float t) -> vec2f { return lerp(a, b, t); }
inline auto mix(const vec3f& a, const vec3f& b, float t) -> vec3f { return lerp(a, b, t); }
inline auto mix(const vec4f& a, const vec4f& b, float t) -> vec4f { return lerp(a, b, t); }

auto perspective_matrix(float fov, float aspect, float near, float far) -> mat4f;

auto perspective_matrix_reversed(float fov, float aspect, float near, float far) -> mat4f;
auto orthographic_matrix(float left, float right, float bottom, float top, float near, float far) -> mat4f;
auto orthographic_matrix_reversed(float width, float height, float near, float far) -> mat4f;
auto look_at_matrix(const vec3f& eye, const vec3f& target) -> mat4f;
auto look_at_matrix(const vec3f& eye, const vec3f& target, const vec3f& up) -> mat4f;

auto translation_matrix(const vec3f& translation) -> mat4f;
auto rotation_matrix_x(float angle) -> mat4f;
auto rotation_matrix_y(float angle) -> mat4f;
auto rotation_matrix_z(float angle) -> mat4f;
auto rotation_matrix(const vec3f& rotation) -> mat4f;
auto rotation_matrix(const quat& rotation) -> mat4f;
auto scale_matrix(const vec3f& scale) -> mat4f;
auto transform_matrix(
    const vec3f& position, const vec3f& rotation, const vec3f& scale
) -> mat4f;
auto transform_matrix(
    const vec3f& position, const quat& rotation, const vec3f& scale
) -> mat4f;

auto identity_matrix() -> mat4f;
auto transpose_matrix(const mat4f& matrix) -> mat4f;
auto inverse_matrix(const mat4f& matrix) -> std::expected<mat4f, error_type>;

constexpr auto radians(
    float degrees
) -> float {
    return degrees * deg_to_rad;
}

constexpr auto degrees(
    float radians
) -> float {
    return radians * rad_to_deg;
}

template <typename T>
constexpr auto clamp(
    T value, T min_val, T max_val
) -> T {
    if (value < min_val)
        return min_val;
    if (value > max_val)
        return max_val;
    return value;
}

template <typename T>
constexpr auto sign(
    T value
) -> T {
    return value > T(0) ? T(1) : value < T(0) ? T(-1) : T(0);
}

}  // namespace vw::math
