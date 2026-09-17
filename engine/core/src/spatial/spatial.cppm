export module vw.core:spatial;

import std;

import :types;
import :vector;
import :matrix;
import :math;

export namespace vw::spatial {

struct aabb;
struct ray;

struct plane {
    vec3f normal;
    float32 distance;

    [[nodiscard]] auto distance_to_point(
        const vec3f& point
    ) const -> float32 {
        return math::dot(normal, point) + distance;
    }
};

struct ray {
    vec3f start;
    vec3f end;
    vec3f direction;

    ray(const vec3f& start, const vec3f& end);

    [[nodiscard]] auto length() const -> float32;
    [[nodiscard]] auto point_at(float32 t) const -> vec3f;
    [[nodiscard]] auto intersects_at(const aabb& bounds, float32& t_out) const -> bool;
};

struct aabb {
    vec3f min;
    vec3f max;

    [[nodiscard]] auto center() const -> vec3f {
        return {(min.x + max.x) * 0.5F, (min.y + max.y) * 0.5F, (min.z + max.z) * 0.5F};
    }

    [[nodiscard]] auto size() const -> vec3f {
        return {max.x - min.x, max.y - min.y, max.z - min.z};
    }

    [[nodiscard]] auto area() const -> float32 {
        const vec3f s = size();
        return (s.x * s.y) + (s.y * s.z) + (s.z * s.x);
    }

    [[nodiscard]] auto intersects(
        const aabb& other
    ) const -> bool {
        return  //
            min.x <= other.max.x && max.x >= other.min.x && min.y <= other.max.y &&
            max.y >= other.min.y && min.z <= other.max.z && max.z >= other.min.z;
    }

    [[nodiscard]] auto intersects(
        const vec3f& point
    ) const -> bool {
        return  //
            point.x >= min.x && point.x <= max.x && point.y >= min.y && point.y <= max.y &&
            point.z >= min.z && point.z <= max.z;
    }

    [[nodiscard]] auto intersects(const ray& r) const -> bool;

    [[nodiscard]] static auto merge(
        const aabb& a, const aabb& b
    ) -> aabb {
        return aabb{
            .min =
                vec3f{
                    std::min(a.min.x, b.min.x),
                    std::min(a.min.y, b.min.y),
                    std::min(a.min.z, b.min.z)
                },
            .max = vec3f{
                std::max(a.max.x, b.max.x), std::max(a.max.y, b.max.y), std::max(a.max.z, b.max.z)
            }
        };
    }

    [[nodiscard]] auto operator==(
        const aabb& other
    ) const -> bool {
        constexpr float32 epsilon = 1e-5F;
        return math::is_safe_zero(min.x - other.min.x, epsilon) &&
            math::is_safe_zero(min.y - other.min.y, epsilon) &&
            math::is_safe_zero(min.z - other.min.z, epsilon) &&
            math::is_safe_zero(max.x - other.max.x, epsilon) &&
            math::is_safe_zero(max.y - other.max.y, epsilon) &&
            math::is_safe_zero(max.z - other.max.z, epsilon);
    }

    [[nodiscard]] auto operator!=(
        const aabb& other
    ) const -> bool {
        return !(*this == other);
    }
};

struct frustum {
    static constexpr std::size_t plane_count = 6;

    std::array<plane, plane_count> planes{};

    [[nodiscard]] static auto from_view_projection_matrix(const mat4f& view_proj) -> frustum;

    [[nodiscard]] auto intersects(const aabb& bounds) const -> bool;
    [[nodiscard]] auto intersects(const vec3f& point) const -> bool;
    [[nodiscard]] auto intersects(const ray& r) const -> bool;

    [[nodiscard]] auto operator==(const frustum& other) const -> bool;
    [[nodiscard]] auto operator!=(const frustum& other) const -> bool;

    [[nodiscard]] auto approximately_equal(
        const frustum& other, float32 angle_threshold, float32 distance_threshold
    ) const -> bool;
};

struct depth_range {
    float32 near_depth;
    float32 far_depth;
};

struct cluster_grid {
    uint32 screen_width  = 1280;
    uint32 screen_height = 720;
    uint32 tile_size     = 32;
    uint32 slices        = 24;

    float32 near_depth = 0.1F;
    float32 far_depth  = 4096.0F;

    float32 proj_x = 1.0F;
    float32 proj_y = -1.0F;

    [[nodiscard]] auto operator==(const cluster_grid&) const -> bool = default;

    [[nodiscard]] auto tiles_x() const -> uint32 {
        return (screen_width + tile_size - 1) / tile_size;
    }

    [[nodiscard]] auto tiles_y() const -> uint32 {
        return (screen_height + tile_size - 1) / tile_size;
    }

    [[nodiscard]] auto cluster_count() const -> uint32 {
        return tiles_x() * tiles_y() * slices;
    }

    [[nodiscard]] auto z_scale() const -> float32 {
        return static_cast<float32>(slices) / std::log(far_depth / near_depth);
    }

    [[nodiscard]] auto z_bias() const -> float32 {
        return -z_scale() * std::log(near_depth);
    }

    [[nodiscard]] auto slice_of(
        float32 depth
    ) const -> uint32 {
        const float32 held = std::clamp(depth, near_depth, far_depth);
        const auto raw     = static_cast<int32>(std::floor(std::log(held) * z_scale() + z_bias()));

        return static_cast<uint32>(std::clamp(raw, 0, static_cast<int32>(slices) - 1));
    }

    [[nodiscard]] auto z_range_of(
        uint32 slice
    ) const -> depth_range {
        const float32 ratio = far_depth / near_depth;
        const auto count    = static_cast<float32>(slices);

        return {
            .near_depth = near_depth * std::pow(ratio, static_cast<float32>(slice) / count),
            .far_depth  = near_depth * std::pow(ratio, static_cast<float32>(slice + 1) / count),
        };
    }

    [[nodiscard]] auto cluster_index(
        uint32 tile_x, uint32 tile_y, uint32 slice
    ) const -> uint32 {
        return ((slice * tiles_y()) + tile_y) * tiles_x() + tile_x;
    }
};

struct view_sphere {
    // x и y в пространстве вида; z — глубина вдоль оси взгляда, положительная
    // перед камерой. Шейдер приходит сюда с -(view * world).z: знак
    // переворачивается один раз, здесь, а не в каждом сравнении ниже.
    vec3f center;
    float32 radius;
};

struct view_capsule {
    vec3f end_a;
    vec3f end_b;
    float32 radius;
};

[[nodiscard]] constexpr auto as_capsule(
    const view_sphere& ball
) -> view_capsule {
    return {.end_a = ball.center, .end_b = ball.center, .radius = ball.radius};
}

struct tile_rect {
    uint32 min_x = 1;
    uint32 min_y = 1;
    uint32 max_x = 0;
    uint32 max_y = 0;

    [[nodiscard]] auto is_empty() const -> bool {
        return min_x > max_x || min_y > max_y;
    }
};

[[nodiscard]] auto scatter_slice(const cluster_grid& grid, const view_capsule& shape, uint32 slice)
    -> tile_rect;

[[nodiscard]] inline auto scatter_slice(
    const cluster_grid& grid, const view_sphere& light, uint32 slice
) -> tile_rect {
    return scatter_slice(grid, as_capsule(light), slice);
}

class cluster_lights {
public:
    cluster_lights(const cluster_grid& grid, uint32 cap);

    auto clear() -> void;
    auto add(uint32 index, const view_capsule& shape) -> void;

    auto add(
        uint32 index, const view_sphere& light
    ) -> void {
        add(index, as_capsule(light));
    }

    [[nodiscard]] auto get_grid() const -> const cluster_grid& {
        return grid_;
    }

    [[nodiscard]] auto get_cap() const -> uint32 {
        return cap_;
    }

    [[nodiscard]] auto count_of(
        uint32 cluster
    ) const -> uint32 {
        return counts_[cluster];
    }

    [[nodiscard]] auto lights_of(uint32 cluster) const -> std::span<const uint32>;

    [[nodiscard]] auto get_assignment_count() const -> uint64 {
        return assignments_;
    }

    [[nodiscard]] auto get_overflow_count() const -> uint64 {
        return overflow_;
    }

private:
    auto place_(uint32 index, uint32 slice, const tile_rect& rect) -> void;

    cluster_grid grid_;
    uint32 cap_;
    std::vector<uint32> counts_;
    std::vector<uint32> indices_;
    uint64 assignments_ = 0;
    uint64 overflow_    = 0;
};

struct cluster_check {
    uint64 clusters_compared = 0;
    uint64 count_mismatches  = 0;
    uint64 set_mismatches    = 0;
    bool overflow_matches    = true;

    uint32 first_bad       = 0;
    uint32 reference_count = 0;
    uint32 actual_count    = 0;

    [[nodiscard]] auto ok() const -> bool {
        return count_mismatches == 0 && set_mismatches == 0 && overflow_matches;
    }
};

[[nodiscard]] auto check_clusters(
    const cluster_lights& reference, std::span<const uint32> counts, std::span<const uint32> indices
) -> cluster_check;

inline ray::ray(
    const vec3f& start, const vec3f& end
)
    : start(start), end(end) {
    const vec3f dir   = end - start;
    const float32 len = math::length(dir);
    direction         = len > 0.0F ? math::normalize(dir) : vec3f{1.0F, 0.0F, 0.0F};
}

inline auto ray::length() const -> float32 {
    return math::length(end - start);
}

inline auto ray::point_at(
    float32 t
) const -> vec3f {
    return start + direction * t;
}

inline auto ray::intersects_at(
    const aabb& bounds, float32& t_out
) const -> bool {
    float32 t_min = 0.0F;
    float32 t_max = length();

    const float32 inv_dir_x = 1.0F / direction.x;
    float32 t0_x            = (bounds.min.x - start.x) * inv_dir_x;
    float32 t1_x            = (bounds.max.x - start.x) * inv_dir_x;
    if (inv_dir_x < 0.0F) {
        std::swap(t0_x, t1_x);
    }
    t_min = t0_x > t_min ? t0_x : t_min;
    t_max = t1_x < t_max ? t1_x : t_max;
    if (t_max < t_min) {
        return false;
    }

    const float32 inv_dir_y = 1.0F / direction.y;
    float32 t0_y            = (bounds.min.y - start.y) * inv_dir_y;
    float32 t1_y            = (bounds.max.y - start.y) * inv_dir_y;
    if (inv_dir_y < 0.0F) {
        std::swap(t0_y, t1_y);
    }
    t_min = t0_y > t_min ? t0_y : t_min;
    t_max = t1_y < t_max ? t1_y : t_max;
    if (t_max < t_min) {
        return false;
    }

    const float32 inv_dir_z = 1.0F / direction.z;
    float32 t0_z            = (bounds.min.z - start.z) * inv_dir_z;
    float32 t1_z            = (bounds.max.z - start.z) * inv_dir_z;
    if (inv_dir_z < 0.0F) {
        std::swap(t0_z, t1_z);
    }
    t_min = t0_z > t_min ? t0_z : t_min;
    t_max = t1_z < t_max ? t1_z : t_max;
    if (t_max < t_min) {
        return false;
    }

    t_out = t_min;
    return true;
}

inline auto aabb::intersects(
    const ray& r
) const -> bool {
    float32 unused = 0.0F;
    return r.intersects_at(*this, unused);
}

inline auto frustum::intersects(
    const vec3f& point
) const -> bool {
    for (const auto& p : planes) {
        if (math::dot(p.normal, point) + p.distance < 0.0F) {
            return false;
        }
    }
    return true;
}

inline auto frustum::intersects(
    const aabb& bounds
) const -> bool {
    for (const auto& p : planes) {
        const vec3f p_vertex{
            p.normal.x > 0.0F ? bounds.max.x : bounds.min.x,
            p.normal.y > 0.0F ? bounds.max.y : bounds.min.y,
            p.normal.z > 0.0F ? bounds.max.z : bounds.min.z
        };

        if (math::dot(p.normal, p_vertex) + p.distance < 0.0F) {
            return false;
        }
    }
    return true;
}

inline auto frustum::intersects(
    const ray& r
) const -> bool {
    float32 t_min = 0.0F;
    float32 t_max = r.length();

    for (const auto& p : planes) {
        const float32 denom = math::dot(p.normal, r.direction);

        if (std::abs(denom) < 1e-6F) {
            if (math::dot(p.normal, r.start) + p.distance < 0.0F) {
                return false;
            }
            continue;
        }

        const float32 t = -(math::dot(p.normal, r.start) + p.distance) / denom;

        if (denom > 0.0F) {
            t_max = std::min(t_max, t);
        } else {
            t_min = std::max(t_min, t);
        }

        if (t_min > t_max) {
            return false;
        }
    }

    return t_min <= r.length() && t_max >= 0.0F;
}

}  // namespace vw::spatial
