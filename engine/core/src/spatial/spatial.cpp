module vw.core;

import std;

namespace vw::spatial {

namespace {

auto normalized_plane(float32 x, float32 y, float32 z, float32 w) -> plane {
    const vec3f normal{x, y, z};
    const float32 len = math::length(normal);
    if (len <= 0.0F) {
        return {};
    }
    const float32 inv_len = 1.0F / len;
    return {.normal = normal * inv_len, .distance = w * inv_len};
}

}  // namespace

auto frustum::from_view_projection_matrix(const mat4f& view_proj) -> frustum {
    frustum f{};

    for (int32 row = 0; row < 3; ++row) {
        const auto left  = static_cast<std::size_t>(row) * 2;
        const auto right = left + 1;

        f.planes[left] = normalized_plane(
            view_proj[3, 0] + view_proj[row, 0], view_proj[3, 1] + view_proj[row, 1],
            view_proj[3, 2] + view_proj[row, 2], view_proj[3, 3] + view_proj[row, 3]);

        f.planes[right] = normalized_plane(
            view_proj[3, 0] - view_proj[row, 0], view_proj[3, 1] - view_proj[row, 1],
            view_proj[3, 2] - view_proj[row, 2], view_proj[3, 3] - view_proj[row, 3]);
    }

    return f;
}

auto frustum::operator==(const frustum& other) const -> bool {
    for (std::size_t i = 0; i < frustum::plane_count; ++i) {
        if (planes[i].normal != other.planes[i].normal ||
            std::abs(planes[i].distance - other.planes[i].distance) > 1e-5F) {
            return false;
        }
    }
    return true;
}

auto frustum::operator!=(const frustum& other) const -> bool {
    return !(*this == other);
}

auto frustum::approximately_equal(
    const frustum& other, float32 angle_threshold, float32 distance_threshold) const -> bool {
    for (std::size_t i = 0; i < frustum::plane_count; ++i) {
        const float32 angle_diff = std::acos(
            math::clamp(math::dot(planes[i].normal, other.planes[i].normal), -1.0F, 1.0F));
        const float32 distance_diff = std::abs(planes[i].distance - other.planes[i].distance);

        if (angle_diff > angle_threshold || distance_diff > distance_threshold) {
            return false;
        }
    }
    return true;
}

namespace {

auto projected_span(
    float32 x_min, float32 x_max, float32 depth_min, float32 depth_max
) -> std::pair<float32, float32> {
    const float32 near_min = x_min / depth_min;
    const float32 far_min  = x_min / depth_max;
    const float32 near_max = x_max / depth_min;
    const float32 far_max  = x_max / depth_max;

    return {
        std::min(std::min(near_min, far_min), std::min(near_max, far_max)),
        std::max(std::max(near_min, far_min), std::max(near_max, far_max)),
    };
}

auto tile_of(float32 pixel, float32 tile_size, int32 last) -> uint32 {
    const auto index = static_cast<int32>(std::floor(pixel / tile_size));
    return static_cast<uint32>(std::clamp(index, 0, last));
}

// см. docs/rendering.md#эталон-и-шейдер
auto rect_of_span(
    const cluster_grid& grid, float32 x_min, float32 x_max, float32 y_min, float32 y_max
) -> tile_rect {
    float32 ndc_x0 = x_min * grid.proj_x;
    float32 ndc_x1 = x_max * grid.proj_x;
    if (ndc_x0 > ndc_x1) {
        std::swap(ndc_x0, ndc_x1);
    }

    float32 ndc_y0 = y_min * grid.proj_y;
    float32 ndc_y1 = y_max * grid.proj_y;
    if (ndc_y0 > ndc_y1) {
        std::swap(ndc_y0, ndc_y1);
    }

    const auto width  = static_cast<float32>(grid.screen_width);
    const auto height = static_cast<float32>(grid.screen_height);

    const float32 pixel_x0 = ((ndc_x0 * 0.5F) + 0.5F) * width;
    const float32 pixel_x1 = ((ndc_x1 * 0.5F) + 0.5F) * width;
    const float32 pixel_y0 = ((ndc_y0 * 0.5F) + 0.5F) * height;
    const float32 pixel_y1 = ((ndc_y1 * 0.5F) + 0.5F) * height;

    if (pixel_x1 < 0.0F || pixel_x0 > width || pixel_y1 < 0.0F || pixel_y0 > height) {
        return {};
    }

    const auto tile_size = static_cast<float32>(grid.tile_size);
    const auto last_x    = static_cast<int32>(grid.tiles_x()) - 1;
    const auto last_y    = static_cast<int32>(grid.tiles_y()) - 1;

    return {
        .min_x = tile_of(pixel_x0, tile_size, last_x),
        .min_y = tile_of(pixel_y0, tile_size, last_y),
        .max_x = tile_of(pixel_x1, tile_size, last_x),
        .max_y = tile_of(pixel_y1, tile_size, last_y),
    };
}

}  // namespace

auto scatter_slice(
    const cluster_grid& grid, const view_capsule& shape, uint32 slice
) -> tile_rect {
    const depth_range slab = grid.z_range_of(slice);

    const float32 spine_near = std::min(shape.end_a.depth, shape.end_b.depth);
    const float32 spine_far  = std::max(shape.end_a.depth, shape.end_b.depth);

    const float32 depth_min = std::max(slab.near_depth, spine_near - shape.radius);
    const float32 depth_max = std::min(slab.far_depth, spine_far + shape.radius);

    if (depth_min > depth_max) {
        return {};
    }

    const float32 outside = std::max(
        {0.0F, slab.near_depth - spine_far, spine_near - slab.far_depth}
    );
    const float32 radius_squared = (shape.radius * shape.radius) - (outside * outside);

    if (radius_squared <= 0.0F) {
        return {};
    }

    const float32 radius = std::sqrt(radius_squared);

    const float32 along_x     = shape.end_b.x - shape.end_a.x;
    const float32 along_y     = shape.end_b.y - shape.end_a.y;
    const float32 along_depth = shape.end_b.depth - shape.end_a.depth;

    float32 t_near = 0.0F;
    float32 t_far  = 1.0F;

    if (std::abs(along_depth) > 1.0e-6F) {
        const float32 first =
            (slab.near_depth - shape.radius - shape.end_a.depth) / along_depth;
        const float32 second =
            (slab.far_depth + shape.radius - shape.end_a.depth) / along_depth;

        t_near = std::clamp(std::min(first, second), 0.0F, 1.0F);
        t_far  = std::clamp(std::max(first, second), 0.0F, 1.0F);
    }

    const float32 x_near = shape.end_a.x + (t_near * along_x);
    const float32 x_far  = shape.end_a.x + (t_far * along_x);
    const float32 y_near = shape.end_a.y + (t_near * along_y);
    const float32 y_far  = shape.end_a.y + (t_far * along_y);

    const auto [x_min, x_max] = projected_span(
        std::min(x_near, x_far) - radius, std::max(x_near, x_far) + radius, depth_min,
        depth_max
    );
    const auto [y_min, y_max] = projected_span(
        std::min(y_near, y_far) - radius, std::max(y_near, y_far) + radius, depth_min,
        depth_max
    );

    return rect_of_span(grid, x_min, x_max, y_min, y_max);
}

cluster_lights::cluster_lights(
    const cluster_grid& grid, uint32 cap
)
    : grid_(grid)
    , cap_(cap)
    , counts_(grid.cluster_count(), 0)
    , indices_(static_cast<std::size_t>(grid.cluster_count()) * cap, 0) {}

auto cluster_lights::clear() -> void {
    std::ranges::fill(counts_, 0U);
    assignments_ = 0;
    overflow_    = 0;
}

auto cluster_lights::add(
    uint32 index, const view_capsule& shape
) -> void {
    const float32 nearest  = std::min(shape.end_a.depth, shape.end_b.depth) - shape.radius;
    const float32 farthest = std::max(shape.end_a.depth, shape.end_b.depth) + shape.radius;

    if (farthest < grid_.near_depth || nearest > grid_.far_depth) {
        return;
    }

    const uint32 first = grid_.slice_of(nearest);
    const uint32 last  = grid_.slice_of(farthest);

    for (uint32 slice = first; slice <= last; ++slice) {
        place_(index, slice, scatter_slice(grid_, shape, slice));
    }
}

auto cluster_lights::place_(
    uint32 index, uint32 slice, const tile_rect& rect
) -> void {
    if (rect.is_empty()) {
        return;
    }

    for (uint32 tile_y = rect.min_y; tile_y <= rect.max_y; ++tile_y) {
        for (uint32 tile_x = rect.min_x; tile_x <= rect.max_x; ++tile_x) {
            const uint32 cluster = grid_.cluster_index(tile_x, tile_y, slice);
            const uint32 slot    = counts_[cluster]++;

            ++assignments_;

            if (slot < cap_) {
                indices_[(static_cast<std::size_t>(cluster) * cap_) + slot] = index;
            } else {
                ++overflow_;
            }
        }
    }
}

auto cluster_lights::lights_of(
    uint32 cluster
) const -> std::span<const uint32> {
    const uint32 stored = std::min(counts_[cluster], cap_);

    return std::span<const uint32>{
        indices_.data() + (static_cast<std::size_t>(cluster) * cap_), stored
    };
}

auto check_clusters(
    const cluster_lights& reference,
    std::span<const uint32> cluster_counts,
    uint32 overflow_count,
    std::span<const uint32> indices
) -> cluster_check {
    const cluster_grid& grid  = reference.get_grid();
    const uint32 cap          = reference.get_cap();
    const uint32 cluster_count = grid.cluster_count();

    cluster_check result{};

    if (cluster_counts.size() < cluster_count ||
        indices.size() < static_cast<std::size_t>(cluster_count) * cap) {
        result.count_mismatches = cluster_count;
        return result;
    }

    std::vector<uint32> mine;
    std::vector<uint32> theirs;
    mine.reserve(cap);
    theirs.reserve(cap);

    for (uint32 cluster = 0; cluster < cluster_count; ++cluster) {
        ++result.clusters_compared;

        const uint32 expected = reference.count_of(cluster);
        const uint32 actual   = cluster_counts[cluster];

        if (expected != actual) {
            if (result.count_mismatches == 0 && result.set_mismatches == 0) {
                result.first_bad       = cluster;
                result.reference_count = expected;
                result.actual_count    = actual;
            }
            ++result.count_mismatches;
            continue;
        }

        if (expected > cap) {
            continue;
        }

        const auto expected_list = reference.lights_of(cluster);
        const auto actual_list   = indices.subspan(
            static_cast<std::size_t>(cluster) * cap, expected
        );

        mine.assign(expected_list.begin(), expected_list.end());
        theirs.assign(actual_list.begin(), actual_list.end());
        std::ranges::sort(mine);
        std::ranges::sort(theirs);

        if (mine != theirs) {
            if (result.count_mismatches == 0 && result.set_mismatches == 0) {
                result.first_bad       = cluster;
                result.reference_count = expected;
                result.actual_count    = actual;
            }
            ++result.set_mismatches;
        }
    }

    result.overflow_matches = overflow_count == reference.get_overflow_count();

    return result;
}

}  // namespace vw::spatial
