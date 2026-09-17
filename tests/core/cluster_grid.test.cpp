#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

import std;
import vw.core;

using namespace vw;
using Catch::Approx;
using spatial::cluster_grid;
using spatial::cluster_lights;
using spatial::view_depth_point;
using spatial::view_sphere;

namespace {

auto bench_grid() -> cluster_grid {
    const float32 fov_scale = 1.0F / std::tan(math::radians(60.0F * 0.5F));

    return cluster_grid{
        .screen_width  = 1280,
        .screen_height = 720,
        .tile_size     = 32,
        .slices        = 24,
        .near_depth    = 0.1F,
        .far_depth     = 4096.0F,
        .proj_x        = fov_scale / (1280.0F / 720.0F),
        .proj_y        = -fov_scale,
    };
}

auto cluster_of(const cluster_grid& grid, const view_depth_point& point) -> std::optional<uint32> {
    if (point.depth < grid.near_depth || point.depth > grid.far_depth) {
        return std::nullopt;
    }

    const float32 pixel_x = (((grid.proj_x * point.x / point.depth) * 0.5F) + 0.5F) *
                            static_cast<float32>(grid.screen_width);
    const float32 pixel_y = (((grid.proj_y * point.y / point.depth) * 0.5F) + 0.5F) *
                            static_cast<float32>(grid.screen_height);

    if (pixel_x < 0.0F || pixel_x >= static_cast<float32>(grid.screen_width) ||
        pixel_y < 0.0F || pixel_y >= static_cast<float32>(grid.screen_height)) {
        return std::nullopt;
    }

    return grid.cluster_index(
        static_cast<uint32>(pixel_x) / grid.tile_size,
        static_cast<uint32>(pixel_y) / grid.tile_size,
        grid.slice_of(point.depth)
    );
}

auto reaches(const view_sphere& light, const view_depth_point& point) -> bool {
    const float32 across = point.x - light.center.x;
    const float32 down   = point.y - light.center.y;
    const float32 depth  = point.depth - light.center.depth;

    return ((across * across) + (down * down) + (depth * depth)) <
           (light.radius * light.radius);
}

auto random_light(const cluster_grid& grid, std::mt19937& rng) -> view_sphere {
    std::uniform_real_distribution<float32> across{-1.2F, 1.2F};
    std::uniform_real_distribution<float32> log_depth{std::log(0.5F), std::log(2000.0F)};
    std::uniform_real_distribution<float32> reach{0.05F, 0.8F};

    const float32 depth = std::exp(log_depth(rng));

    return view_sphere{
        .center = view_depth_point{
            .x     = across(rng) * depth / grid.proj_x,
            .y     = across(rng) * depth / std::abs(grid.proj_y),
            .depth = depth,
        },
        .radius = depth * reach(rng),
    };
}

}  // namespace

TEST_CASE("slices tile the depth range with no hole and no overlap", "[cluster]") {
    const cluster_grid grid = bench_grid();

    REQUIRE(grid.z_range_of(0).near_depth == Approx(grid.near_depth));
    REQUIRE(grid.z_range_of(grid.slices - 1).far_depth == Approx(grid.far_depth));

    for (uint32 slice = 0; slice + 1 < grid.slices; ++slice) {
        REQUIRE(grid.z_range_of(slice).far_depth == Approx(grid.z_range_of(slice + 1).near_depth));
    }
}

TEST_CASE("slice_of and z_range_of invert each other", "[cluster]") {
    const cluster_grid grid = bench_grid();

    for (uint32 slice = 0; slice < grid.slices; ++slice) {
        const auto range = grid.z_range_of(slice);

        REQUIRE(grid.slice_of(std::sqrt(range.near_depth * range.far_depth)) == slice);
    }
}

TEST_CASE("every depth lands in the slab its slice names", "[cluster]") {
    const cluster_grid grid = bench_grid();

    constexpr int32 samples  = 2000;
    constexpr float32 margin = 1.0001F;

    for (int32 i = 0; i < samples; ++i) {
        const float32 t = static_cast<float32>(i) / static_cast<float32>(samples - 1);
        const float32 depth =
            grid.near_depth * std::pow(grid.far_depth / grid.near_depth, t);

        const auto range = grid.z_range_of(grid.slice_of(depth));

        REQUIRE(depth >= range.near_depth / margin);
        REQUIRE(depth <= range.far_depth * margin);
    }
}

TEST_CASE("slice_of never goes backwards and stops at both ends", "[cluster]") {
    const cluster_grid grid = bench_grid();

    REQUIRE(grid.slice_of(-100.0F) == 0);
    REQUIRE(grid.slice_of(0.0F) == 0);
    REQUIRE(grid.slice_of(grid.near_depth * 0.5F) == 0);
    REQUIRE(grid.slice_of(grid.far_depth * 100.0F) == grid.slices - 1);

    uint32 previous = 0;

    for (int32 i = 0; i < 5000; ++i) {
        const float32 depth = 0.05F + (static_cast<float32>(i) * 1.5F);
        const uint32 slice  = grid.slice_of(depth);

        REQUIRE(slice >= previous);
        previous = slice;
    }
}

TEST_CASE("one slice is exactly flat tiles", "[cluster]") {
    cluster_grid grid = bench_grid();
    grid.slices       = 1;

    REQUIRE(grid.cluster_count() == grid.tiles_x() * grid.tiles_y());
    REQUIRE(grid.z_range_of(0).near_depth == Approx(grid.near_depth));
    REQUIRE(grid.z_range_of(0).far_depth == Approx(grid.far_depth));
    REQUIRE(grid.slice_of(grid.near_depth) == 0);
    REQUIRE(grid.slice_of(grid.far_depth) == 0);
    REQUIRE(grid.slice_of(37.0F) == 0);
}

TEST_CASE("a source is listed in every cluster it can light", "[cluster]") {
    const cluster_grid grid = bench_grid();

    cluster_lights clusters{grid, 8};
    std::mt19937 rng{20260822};
    std::uniform_real_distribution<float32> offset{-1.0F, 1.0F};

    uint32 checked = 0;
    uint32 missed  = 0;

    for (int32 light_n = 0; light_n < 160; ++light_n) {
        const view_sphere light = random_light(grid, rng);

        clusters.clear();
        clusters.add(0, light);

        for (int32 sample = 0; sample < 768; ++sample) {
            const view_depth_point point{
                .x     = light.center.x + (offset(rng) * light.radius),
                .y     = light.center.y + (offset(rng) * light.radius),
                .depth = light.center.depth + (offset(rng) * light.radius),
            };

            if (!reaches(light, point)) {
                continue;
            }

            const auto cluster = cluster_of(grid, point);
            if (!cluster) {
                continue;
            }

            ++checked;

            const auto listed = clusters.lights_of(*cluster);
            if (std::ranges::find(listed, 0U) == listed.end()) {
                ++missed;
            }
        }
    }

    REQUIRE(checked > 10000);
    REQUIRE(missed == 0);
}

TEST_CASE("a source behind the camera reaches nothing", "[cluster]") {
    const cluster_grid grid = bench_grid();

    cluster_lights clusters{grid, 8};
    clusters.add(0, view_sphere{.center = {0.0F, 0.0F, -50.0F}, .radius = 1.0F});

    REQUIRE(clusters.get_assignment_count() == 0);

    clusters.add(1, view_sphere{.center = {0.0F, 0.0F, 0.01F}, .radius = 0.02F});

    REQUIRE(clusters.get_assignment_count() == 0);
}

TEST_CASE("a source across the near plane keeps to the frame", "[cluster]") {
    const cluster_grid grid = bench_grid();

    cluster_lights clusters{grid, 8};
    clusters.add(0, view_sphere{.center = {0.0F, 0.0F, 0.05F}, .radius = 10.0F});

    REQUIRE(clusters.get_assignment_count() > 0);
    REQUIRE(clusters.get_assignment_count() <= grid.cluster_count());

    const auto cluster = cluster_of(grid, view_depth_point{0.0F, 0.0F, 1.0F});
    REQUIRE(cluster.has_value());

    const auto listed = clusters.lights_of(*cluster);
    REQUIRE(std::ranges::find(listed, 0U) != listed.end());
}

TEST_CASE("a source with no reach is listed nowhere", "[cluster]") {
    const cluster_grid grid = bench_grid();

    cluster_lights clusters{grid, 8};
    clusters.add(0, view_sphere{.center = {0.0F, 0.0F, 12.0F}, .radius = 0.0F});

    REQUIRE(clusters.get_assignment_count() == 0);
}

TEST_CASE("a source larger than the scene is listed everywhere", "[cluster]") {
    const cluster_grid grid = bench_grid();

    cluster_lights clusters{grid, 8};
    clusters.add(0, view_sphere{.center = {0.0F, 0.0F, 10.0F}, .radius = 1.0e6F});

    REQUIRE(clusters.get_assignment_count() == grid.cluster_count());

    for (uint32 cluster = 0; cluster < grid.cluster_count(); ++cluster) {
        REQUIRE(clusters.count_of(cluster) == 1);
    }
}

TEST_CASE("a cluster past its cap counts the rest and stays out of its neighbour", "[cluster]") {
    const cluster_grid grid{
        .screen_width  = 64,
        .screen_height = 32,
        .tile_size     = 32,
        .slices        = 1,
        .near_depth    = 1.0F,
        .far_depth     = 100.0F,
        .proj_x        = 1.0F,
        .proj_y        = 1.0F,
    };

    REQUIRE(grid.cluster_count() == 2);

    const view_sphere left{.center = {-5.0F, 0.0F, 10.0F}, .radius = 0.5F};
    const view_sphere right{.center = {5.0F, 0.0F, 10.0F}, .radius = 0.5F};

    cluster_lights probe{grid, 4};
    probe.add(0, left);
    probe.add(1, right);

    REQUIRE(probe.count_of(0) == 1);
    REQUIRE(probe.count_of(1) == 1);
    REQUIRE(probe.lights_of(0)[0] == 0);
    REQUIRE(probe.lights_of(1)[0] == 1);

    cluster_lights clusters{grid, 2};
    clusters.add(0, left);
    clusters.add(1, left);
    clusters.add(2, left);
    clusters.add(3, right);

    REQUIRE(clusters.count_of(0) == 3);
    REQUIRE(clusters.lights_of(0).size() == 2);
    REQUIRE(clusters.lights_of(0)[0] == 0);
    REQUIRE(clusters.lights_of(0)[1] == 1);
    REQUIRE(clusters.get_overflow_count() == 1);
    REQUIRE(clusters.get_assignment_count() == 4);

    REQUIRE(clusters.count_of(1) == 1);
    REQUIRE(clusters.lights_of(1).size() == 1);
    REQUIRE(clusters.lights_of(1)[0] == 3);
}

TEST_CASE("clear puts the grid back where it started", "[cluster]") {
    const cluster_grid grid = bench_grid();

    cluster_lights clusters{grid, 2};
    clusters.add(0, view_sphere{.center = {0.0F, 0.0F, 10.0F}, .radius = 1.0e6F});
    clusters.add(1, view_sphere{.center = {0.0F, 0.0F, 10.0F}, .radius = 1.0e6F});
    clusters.add(2, view_sphere{.center = {0.0F, 0.0F, 10.0F}, .radius = 1.0e6F});

    REQUIRE(clusters.get_overflow_count() == grid.cluster_count());

    clusters.clear();

    REQUIRE(clusters.get_assignment_count() == 0);
    REQUIRE(clusters.get_overflow_count() == 0);

    for (uint32 cluster = 0; cluster < grid.cluster_count(); ++cluster) {
        REQUIRE(clusters.count_of(cluster) == 0);
        REQUIRE(clusters.lights_of(cluster).empty());
    }
}

namespace {

struct gpu_buffers {
    std::vector<uint32> cluster_counts;
    uint32 overflow_count = 0;
    std::vector<uint32> indices;
};

auto as_gpu_wrote(const cluster_lights& reference) -> gpu_buffers {
    const uint32 clusters = reference.get_grid().cluster_count();
    const uint32 cap      = reference.get_cap();

    gpu_buffers out{
        .cluster_counts = std::vector<uint32>(static_cast<std::size_t>(clusters), 0),
        .overflow_count = static_cast<uint32>(reference.get_overflow_count()),
        .indices        = std::vector<uint32>(static_cast<std::size_t>(clusters) * cap, 0),
    };

    for (uint32 cluster = 0; cluster < clusters; ++cluster) {
        out.cluster_counts[cluster] = reference.count_of(cluster);

        const auto list = reference.lights_of(cluster);
        std::ranges::copy(
            list, out.indices.begin() + (static_cast<std::ptrdiff_t>(cluster) * cap)
        );
    }

    return out;
}

auto lit_reference(const cluster_grid& grid, uint32 cap) -> cluster_lights {
    cluster_lights clusters{grid, cap};

    clusters.add(0, view_sphere{.center = {0.0F, 0.0F, 12.0F}, .radius = 6.0F});
    clusters.add(1, view_sphere{.center = {3.0F, 1.0F, 14.0F}, .radius = 8.0F});
    clusters.add(2, view_sphere{.center = {-4.0F, -2.0F, 9.0F}, .radius = 5.0F});
    clusters.add(3, view_sphere{.center = {0.5F, 0.0F, 40.0F}, .radius = 25.0F});

    return clusters;
}

auto check_gpu(const cluster_lights& reference, const gpu_buffers& gpu) -> spatial::cluster_check {
    return spatial::check_clusters(reference, gpu.cluster_counts, gpu.overflow_count, gpu.indices);
}

auto first_cluster_with_two(const gpu_buffers& gpu, uint32 clusters) -> uint32 {
    for (uint32 cluster = 0; cluster < clusters; ++cluster) {
        if (gpu.cluster_counts[cluster] >= 2) {
            return cluster;
        }
    }
    return 0;
}

}  // namespace

TEST_CASE("the comparator agrees with a cull that did the same thing", "[cluster]") {
    const cluster_grid grid = bench_grid();
    const cluster_lights reference = lit_reference(grid, 8);
    const gpu_buffers gpu          = as_gpu_wrote(reference);

    const auto check = check_gpu(reference, gpu);

    REQUIRE(check.ok());
    REQUIRE(check.clusters_compared == grid.cluster_count());
    REQUIRE(check.count_mismatches == 0);
    REQUIRE(check.set_mismatches == 0);
    REQUIRE(check.overflow_matches);
}

TEST_CASE("the order inside a cluster is not something to agree on", "[cluster]") {
    const cluster_grid grid = bench_grid();
    const cluster_lights reference = lit_reference(grid, 8);
    gpu_buffers gpu                = as_gpu_wrote(reference);

    const uint32 cap     = reference.get_cap();
    const uint32 cluster = first_cluster_with_two(gpu, grid.cluster_count());
    REQUIRE(gpu.cluster_counts[cluster] >= 2);

    const auto at = static_cast<std::size_t>(cluster) * cap;
    std::swap(gpu.indices[at], gpu.indices[at + 1]);

    REQUIRE(check_gpu(reference, gpu).ok());
}

TEST_CASE("a count off by one is caught and located", "[cluster]") {
    const cluster_grid grid = bench_grid();
    const cluster_lights reference = lit_reference(grid, 8);
    gpu_buffers gpu                = as_gpu_wrote(reference);

    const uint32 cluster        = first_cluster_with_two(gpu, grid.cluster_count());
    const uint32 was            = gpu.cluster_counts[cluster];
    gpu.cluster_counts[cluster] = was - 1;

    const auto check = check_gpu(reference, gpu);

    REQUIRE_FALSE(check.ok());
    REQUIRE(check.count_mismatches == 1);
    REQUIRE(check.first_bad == cluster);
    REQUIRE(check.reference_count == was);
    REQUIRE(check.actual_count == was - 1);
}

TEST_CASE("a different source with the same count is caught", "[cluster]") {
    const cluster_grid grid = bench_grid();
    const cluster_lights reference = lit_reference(grid, 8);
    gpu_buffers gpu                = as_gpu_wrote(reference);

    const uint32 cap     = reference.get_cap();
    const uint32 cluster = first_cluster_with_two(gpu, grid.cluster_count());

    gpu.indices[static_cast<std::size_t>(cluster) * cap] = 99;

    const auto check = check_gpu(reference, gpu);

    REQUIRE_FALSE(check.ok());
    REQUIRE(check.count_mismatches == 0);
    REQUIRE(check.set_mismatches == 1);
    REQUIRE(check.first_bad == cluster);
}

TEST_CASE("past the cap only the count is compared", "[cluster]") {
    const cluster_grid grid = bench_grid();

    cluster_lights reference{grid, 1};
    for (uint32 light = 0; light < 3; ++light) {
        reference.add(light, view_sphere{.center = {0.0F, 0.0F, 10.0F}, .radius = 1.0e6F});
    }

    gpu_buffers gpu = as_gpu_wrote(reference);

    std::ranges::fill(gpu.indices, 2U);

    const auto check = check_gpu(reference, gpu);

    REQUIRE(reference.count_of(0) == 3);
    REQUIRE(check.ok());
}

TEST_CASE("a wrong overflow tally is caught on its own", "[cluster]") {
    const cluster_grid grid = bench_grid();

    cluster_lights reference{grid, 1};
    reference.add(0, view_sphere{.center = {0.0F, 0.0F, 10.0F}, .radius = 1.0e6F});
    reference.add(1, view_sphere{.center = {0.0F, 0.0F, 10.0F}, .radius = 1.0e6F});

    gpu_buffers gpu = as_gpu_wrote(reference);

    REQUIRE(check_gpu(reference, gpu).ok());

    gpu.overflow_count = 0;

    const auto check = check_gpu(reference, gpu);

    REQUIRE_FALSE(check.ok());
    REQUIRE(check.count_mismatches == 0);
    REQUIRE(check.set_mismatches == 0);
    REQUIRE_FALSE(check.overflow_matches);
}

TEST_CASE("buffers too short to hold the grid are not silently agreed with", "[cluster]") {
    const cluster_grid grid = bench_grid();
    const cluster_lights reference = lit_reference(grid, 8);
    const gpu_buffers gpu          = as_gpu_wrote(reference);

    const auto short_counts =
        std::span<const uint32>{gpu.cluster_counts}.first(gpu.cluster_counts.size() - 1);

    REQUIRE_FALSE(
        spatial::check_clusters(reference, short_counts, gpu.overflow_count, gpu.indices).ok()
    );
}

namespace {

using spatial::view_capsule;

auto distance_to_segment(const view_capsule& shape, const view_depth_point& point) -> float32 {
    const vec3f along{
        shape.end_b.x - shape.end_a.x,
        shape.end_b.y - shape.end_a.y,
        shape.end_b.depth - shape.end_a.depth,
    };
    const vec3f from{
        point.x - shape.end_a.x,
        point.y - shape.end_a.y,
        point.depth - shape.end_a.depth,
    };

    const float32 length_sq = math::dot(along, along);
    const float32 t =
        length_sq > 0.0F ? std::clamp(math::dot(from, along) / length_sq, 0.0F, 1.0F) : 0.0F;

    return math::length(vec3f{
        from.x - (t * along.x),
        from.y - (t * along.y),
        from.z - (t * along.z),
    });
}

auto listed_clusters(const cluster_lights& clusters) -> uint32 {
    uint32 total = 0;
    for (uint32 cluster = 0; cluster < clusters.get_grid().cluster_count(); ++cluster) {
        total += (clusters.count_of(cluster) > 0) ? 1U : 0U;
    }
    return total;
}

}  // namespace

TEST_CASE("a capsule with both ends together is the ball it came from", "[cluster]") {
    const cluster_grid grid = bench_grid();

    const view_sphere ball{.center = {2.0F, -1.0F, 18.0F}, .radius = 7.0F};

    for (uint32 slice = 0; slice < grid.slices; ++slice) {
        const auto from_ball    = spatial::scatter_slice(grid, ball, slice);
        const auto from_capsule = spatial::scatter_slice(grid, spatial::as_capsule(ball), slice);

        REQUIRE(from_ball.min_x == from_capsule.min_x);
        REQUIRE(from_ball.max_x == from_capsule.max_x);
        REQUIRE(from_ball.min_y == from_capsule.min_y);
        REQUIRE(from_ball.max_y == from_capsule.max_y);
    }
}

TEST_CASE("a column is listed in every cluster it can reach", "[cluster]") {
    const cluster_grid grid = bench_grid();

    cluster_lights clusters{grid, 64};

    const std::array<view_capsule, 4> columns{
        view_capsule{
            .end_a = {0.0F, 20.0F, 30.0F}, .end_b = {0.0F, -140.0F, 30.0F},
            .radius = 8.0F
        },
        view_capsule{
            .end_a = {25.0F, 10.0F, 60.0F}, .end_b = {25.0F, -150.0F, 62.0F},
            .radius = 12.0F
        },
        view_capsule{
            .end_a = {-18.0F, 40.0F, 12.0F}, .end_b = {-18.0F, -60.0F, 14.0F},
            .radius = 6.0F
        },
        view_capsule{
            .end_a = {5.0F, 30.0F, 20.0F}, .end_b = {-40.0F, -90.0F, 220.0F},
            .radius = 10.0F
        },
    };

    for (uint32 i = 0; i < columns.size(); ++i) {
        clusters.add(i, columns[i]);
    }

    std::mt19937 rng{20260822};
    std::uniform_real_distribution<float32> along{-0.1F, 1.1F};
    std::uniform_real_distribution<float32> offset{-1.4F, 1.4F};

    uint32 checked = 0;

    for (uint32 i = 0; i < columns.size(); ++i) {
        const view_capsule& shape = columns[i];

        for (uint32 sample = 0; sample < 12000; ++sample) {
            const float32 t = along(rng);

            const view_depth_point point{
                .x     = shape.end_a.x + (t * (shape.end_b.x - shape.end_a.x)) +
                         (offset(rng) * shape.radius),
                .y     = shape.end_a.y + (t * (shape.end_b.y - shape.end_a.y)) +
                         (offset(rng) * shape.radius),
                .depth = shape.end_a.depth + (t * (shape.end_b.depth - shape.end_a.depth)) +
                         (offset(rng) * shape.radius),
            };

            if (distance_to_segment(shape, point) > shape.radius) {
                continue;
            }

            const auto cluster = cluster_of(grid, point);
            if (!cluster) {
                continue;
            }

            ++checked;

            const auto listed = clusters.lights_of(*cluster);
            REQUIRE(std::ranges::find(listed, i) != listed.end());
        }
    }

    REQUIRE(checked > 4000);
}

TEST_CASE("a column costs far fewer clusters than the ball around it", "[cluster]") {
    const cluster_grid grid = bench_grid();

    const view_capsule column{
        .end_a  = {0.0F, 24.0F, 260.0F},
        .end_b  = {0.0F, -144.0F, 260.0F},
        .radius = 8.0F,
    };

    const float32 half = (column.end_a.y - column.end_b.y) * 0.5F;

    const view_sphere around{
        .center = {0.0F, (column.end_a.y + column.end_b.y) * 0.5F, 260.0F},
        .radius = std::sqrt((column.radius * column.radius) + (half * half)),
    };

    cluster_lights as_column{grid, 8};
    as_column.add(0, column);

    cluster_lights as_ball{grid, 8};
    as_ball.add(0, around);

    const uint32 column_clusters = listed_clusters(as_column);
    const uint32 ball_clusters   = listed_clusters(as_ball);

    REQUIRE(column_clusters > 0);
    REQUIRE(ball_clusters > (column_clusters * 3));
}

TEST_CASE("a column behind the camera reaches nothing", "[cluster]") {
    const cluster_grid grid = bench_grid();

    cluster_lights clusters{grid, 4};
    clusters.add(
        0,
        view_capsule{
            .end_a = {0.0F, 20.0F, -40.0F}, .end_b = {0.0F, -20.0F, -10.0F},
            .radius = 5.0F
        }
    );

    REQUIRE(clusters.get_assignment_count() == 0);
}
