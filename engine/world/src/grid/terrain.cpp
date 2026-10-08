module vw.world;


import std;
import vw.core;
import vw.asset;


namespace vw::ecs {

chunk_loader::chunk_loader(
    std::unique_ptr<terrain_generator> generator, job_system& jobs, int32 voxels_per_cell
)
    : generator_(std::move(generator))
    , jobs_(&jobs)
    , voxels_per_cell_(std::max(voxels_per_cell, 1)) {}

chunk_loader::~chunk_loader() {
    jobs_->drain(job_lane::terrain);
}

auto chunk_loader::request(
    vec2i coord
) -> bool {
    if (pending_columns_.contains(coord)) {
        return false;
    }
    pending_columns_.insert(coord);

    jobs_->submit(job_lane::terrain, [this, coord](uint32) { generate_(coord); });
    return true;
}

auto chunk_loader::try_pop_completed() -> std::unique_ptr<gen_column> {
    std::unique_ptr<gen_column> col;
    {
        std::scoped_lock lock(completed_mutex_);
        if (completed_queue_.empty()) {
            return nullptr;
        }
        col = std::move(completed_queue_.front());
        completed_queue_.pop();
    }
    pending_columns_.erase(col->get_coord());
    return col;
}

auto chunk_loader::is_pending(
    vec2i coord
) const -> bool {
    return pending_columns_.contains(coord);
}

auto chunk_loader::pending_count() const -> uint32 {
    return static_cast<uint32>(pending_columns_.size());
}

auto chunk_loader::record_column_(uint64 elapsed_ns, uint64 chunks) -> void {
    std::scoped_lock lock(stats_mutex_);
    gen_latency_.record(elapsed_ns);
    gen_chunks_ += chunks;
}

auto chunk_loader::get_gen_stats() const -> column_gen_stats {
    column_gen_stats out{};

    {
        std::scoped_lock lock(stats_mutex_);
        const auto summary = gen_latency_.summarize();

        out.columns  = summary.count;
        out.chunks   = gen_chunks_;
        out.total_ms = summary.total_ms;
        out.mean_us  = summary.mean_us;
        out.p50_us   = summary.p50_us;
        out.p99_us   = summary.p99_us;
        out.max_us   = summary.max_us;
    }

    const auto lane = jobs_->get_lane_stats(job_lane::terrain);
    out.queue_depth = lane.queued;
    out.queue_peak  = lane.peak;

    return out;
}

auto chunk_loader::generate_(vec2i coord) -> void {
    const auto started = std::chrono::steady_clock::now();

    auto col = std::make_unique<gen_column>(coord.x, coord.y);

    terrain_context ctx{
        .cx              = coord.x,
        .cz              = coord.y,
        .voxels_per_cell = voxels_per_cell_,
        .create_chunk    = [&col](int32 y) -> chunk_data& {
            return col->create_chunk(y, chunk_data{});
        }
    };

    generator_->generate(ctx);
    col->set_phase(column_phase::terrain);

    for (auto& [y, cd] : col->get_all_chunk_data()) {
        static_cast<void>(cd.volume->voxels().scan_fill());
    }

    const auto elapsed = std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::steady_clock::now() - started
    );
    record_column_(static_cast<uint64>(elapsed.count()), col->get_all_chunk_data().size());

    {
        std::scoped_lock lock(completed_mutex_);
        completed_queue_.push(std::move(col));
    }
}

}  // namespace vw::ecs




namespace vw::ecs {

// см. docs/world.md#слои
auto perlin_terrain_generator::slope_between(
    float32 west, float32 here, float32 east, float32 north, float32 south
) -> float32 {
    const float32 across = std::min(std::abs(here - west), std::abs(east - here));
    const float32 along  = std::min(std::abs(here - north), std::abs(south - here));
    return std::max(across, along);
}

perlin_terrain_generator::perlin_terrain_generator(
    vw::asset::model_identity_pool& identity_pool, vw::asset::page_pool& pool
)
    : perlin_terrain_generator(identity_pool, pool, params{}) {}

perlin_terrain_generator::perlin_terrain_generator(
    vw::asset::model_identity_pool& identity_pool, vw::asset::page_pool& pool, params p
)
    : identity_pool_(&identity_pool)
    , page_pool_(&pool)
    , params_(p)
    , biomes_(p.biomes.empty() ? default_biomes() : p.biomes)
    , noise_(p.seed)
    , regions_(region_map::params{
          .seed           = p.seed,
          .spacing_voxels = p.region_spacing_voxels,
          .jitter         = p.region_jitter,
          .warp_voxels    = p.region_warp_voxels,
          .warp_frequency = p.region_warp_frequency,
      }) {}

auto perlin_terrain_generator::cave_field_at(
    int32 wx, int32 wy, int32 wz, int32 depth
) const -> float32 {
    const auto f = params_.cave_field_frequency;
    const auto n = static_cast<float32>(noise_.noise3d(
        (static_cast<float64>(wx) * f) + 517.3,
        static_cast<float64>(wy) * f * params_.cave_field_squash,
        (static_cast<float64>(wz) * f) + 241.9
    ));

    const float32 near_surface = depth < params_.cave_field_surface_reach
        ? 1.0F - (static_cast<float32>(std::max(0, depth)) /
                  static_cast<float32>(std::max(1, params_.cave_field_surface_reach)))
        : 0.0F;

    const float32 t = ((n + (near_surface * params_.cave_field_surface_bias)) -
                       params_.cave_field_threshold) /
        params_.cave_field_falloff;
    const float32 c = std::clamp(t, 0.0F, 1.0F);
    return c * c * (3.0F - (2.0F * c));
}

auto perlin_terrain_generator::cave_entrance_leak_at(
    int32 wx, int32 wz
) const -> float32 {
    const auto fe = params_.cave_entrance_frequency;
    const auto n  = static_cast<float32>(noise_.noise2d(
        (static_cast<float64>(wx) * fe) - 88.1, (static_cast<float64>(wz) * fe) + 44.6
    ));

    return std::clamp(
        (n - params_.cave_entrance_threshold) / std::max(0.05F, params_.cave_entrance_falloff),
        0.0F, 1.0F
    );
}

auto perlin_terrain_generator::cave_openness_at(
    int32 wx, int32 wy, int32 wz, float32 field, int32 surface, float32 leak
) const -> float32 {
    const auto x = static_cast<float64>(wx);
    const auto y = static_cast<float64>(wy);
    const auto z = static_cast<float64>(wz);

    const auto spacing = static_cast<float32>(std::max(1, params_.cave_level_spacing));
    const auto phase   = static_cast<float32>(wy) * 6.2831853F / spacing;
    const float32 band =
        1.0F - (params_.cave_level_contrast * (0.5F - (0.5F * std::cos(phase))));

    const auto fc = params_.cave_cheese_frequency;
    const auto cheese = static_cast<float32>(
        noise_.noise3d(x * fc, y * fc * params_.cave_cheese_squash, z * fc)
    );
    const float32 cheese_open =
        (params_.cave_cheese_width * field * band) - std::abs(cheese);

    const auto ft = params_.cave_tunnel_frequency;
    const auto ta = static_cast<float32>(noise_.noise3d((x * ft) + 71.5, (y * ft) + 13.7, (z * ft) + 39.1));
    const auto tb = static_cast<float32>(noise_.noise3d((x * ft) - 128.3, (y * ft) + 96.2, (z * ft) - 57.4));
    const float32 tunnel_open =
        (params_.cave_tunnel_width * field) - std::sqrt((ta * ta) + (tb * tb));

    float32 open = std::max(cheese_open, tunnel_open);

    const int32 depth = surface - wy;
    const int32 reach = params_.cave_surface_margin + params_.cave_surface_fade;

    if (depth < reach) {
        const auto over = static_cast<float32>(reach - depth);
        const float32 fade = over / static_cast<float32>(std::max(1, params_.cave_surface_fade));

        open -= fade * 2.0F * (1.0F - leak);
        open += leak * params_.cave_entrance_lift;
    }

    const int32 above_bottom = wy - params_.world_bottom_y;
    if (above_bottom < (params_.bedrock_thickness + 4)) {
        open -= 1.0F;
    }

    return open;
}

auto perlin_terrain_generator::carve_caves_(
    vw::asset::model_writer& writer, terrain_context& ctx, int32 chunk_y,
    const column_profile& profile
) const -> void {
    constexpr int32 s = 64;

    if (!params_.caves) {
        return;
    }

    const int32 voxels_per_cell = profile.voxels_per_cell;

    const int32 x0 = ctx.cx * s;
    const int32 y0 = chunk_y * s;
    const int32 z0 = ctx.cz * s;

    if (y0 > profile.cell_of(profile.max_surface)) {
        return;
    }
    if ((((y0 + s - 1) * voxels_per_cell) + voxels_per_cell - 1) <
        (params_.world_bottom_y + params_.bedrock_thickness + 4)) {
        return;
    }

    const int32 stride = std::max(1, params_.cave_sample_stride);
    const int32 cells  = (s + stride - 1) / stride;
    const int32 points = cells + 1;
    const auto plane   = static_cast<std::size_t>(points) * points;

    thread_local std::vector<float32> open;
    open.assign(plane * points, -1.0F);

    bool any_open = false;

    for (int32 gy = 0; gy < points; ++gy) {
        const int32 ly = std::min(gy * stride, s - 1);
        const int32 wy = (y0 + ly) * voxels_per_cell;

        for (int32 gz = 0; gz < points; ++gz) {
            const int32 lz = std::min(gz * stride, s - 1);
            const int32 wz = (z0 + lz) * voxels_per_cell;

            for (int32 gx = 0; gx < points; ++gx) {
                const int32 lx = std::min(gx * stride, s - 1);
                const int32 wx = (x0 + lx) * voxels_per_cell;

                const int32 surface = profile.surface[column_profile::ring_index(lx, lz)];
                const int32 depth   = surface - wy;

                const float32 leak =
                    depth < (params_.cave_surface_margin + params_.cave_surface_fade)
                    ? cave_entrance_leak_at(wx, wz)
                    : 0.0F;

                float32 shaft = 0.0F;
                if (depth >= 0 && depth < params_.cave_entrance_depth) {
                    const float32 taper = 1.0F -
                        (static_cast<float32>(depth) /
                         static_cast<float32>(std::max(1, params_.cave_entrance_depth)));
                    shaft = cave_entrance_leak_at(wx, wz) * taper;
                }

                const float32 field = std::max(
                    cave_field_at(wx, wy, wz, depth),
                    std::max(leak, shaft) * params_.cave_entrance_field
                );
                if (field <= 0.0F) {
                    continue;
                }

                const float32 value = cave_openness_at(wx, wy, wz, field, surface, leak);

                open[(static_cast<std::size_t>(gy) * plane) + (static_cast<std::size_t>(gz) * points) + gx] =
                    value;
                any_open = any_open || value > 0.0F;
            }
        }
    }

    if (!any_open) {
        return;
    }

    const auto sample = [&](int32 gx, int32 gy, int32 gz) -> float32 {
        return open[(static_cast<std::size_t>(gy) * plane) +
                    (static_cast<std::size_t>(gz) * points) + gx];
    };

    const auto inv = 1.0F / static_cast<float32>(stride);

    for (int32 cy = 0; cy < cells; ++cy) {
        for (int32 cz = 0; cz < cells; ++cz) {
            for (int32 cx = 0; cx < cells; ++cx) {
                const float32 c000 = sample(cx, cy, cz);
                const float32 c100 = sample(cx + 1, cy, cz);
                const float32 c010 = sample(cx, cy + 1, cz);
                const float32 c110 = sample(cx + 1, cy + 1, cz);
                const float32 c001 = sample(cx, cy, cz + 1);
                const float32 c101 = sample(cx + 1, cy, cz + 1);
                const float32 c011 = sample(cx, cy + 1, cz + 1);
                const float32 c111 = sample(cx + 1, cy + 1, cz + 1);

                const float32 hi = std::max(
                    std::max(std::max(c000, c100), std::max(c010, c110)),
                    std::max(std::max(c001, c101), std::max(c011, c111))
                );
                if (hi <= 0.0F) {
                    continue;
                }

                const int32 x_end = std::min((cx + 1) * stride, s);
                const int32 y_end = std::min((cy + 1) * stride, s);
                const int32 z_end = std::min((cz + 1) * stride, s);

                for (int32 y = cy * stride; y < y_end; ++y) {
                    if (((y0 + y) * voxels_per_cell) < params_.world_bottom_y) {
                        continue;
                    }
                    const float32 ty = static_cast<float32>(y - (cy * stride)) * inv;

                    for (int32 z = cz * stride; z < z_end; ++z) {
                        const float32 tz = static_cast<float32>(z - (cz * stride)) * inv;

                        const float32 y00 = std::lerp(c000, c010, ty);
                        const float32 y10 = std::lerp(c100, c110, ty);
                        const float32 y01 = std::lerp(c001, c011, ty);
                        const float32 y11 = std::lerp(c101, c111, ty);

                        const float32 z0v = std::lerp(y00, y01, tz);
                        const float32 z1v = std::lerp(y10, y11, tz);

                        for (int32 x = cx * stride; x < x_end; ++x) {
                            const float32 tx = static_cast<float32>(x - (cx * stride)) * inv;
                            if (std::lerp(z0v, z1v, tx) > 0.0F) {
                                writer.set(x, y, z, voxels::air);
                            }
                        }
                    }
                }
            }
        }
    }
}

// см. docs/world.md#климат
auto perlin_terrain_generator::climate_at(
    float64 x, float64 z
) const -> biome_point {
    const auto& tuning = params_;

    const float64 rf = tuning.relief_warp_frequency;
    const float64 px =
        x + (noise_.fractal((x * rf) + 5.2, (z * rf) + 1.3, 2) * tuning.relief_warp_voxels);
    const float64 pz =
        z + (noise_.fractal((x * rf) + 9.7, (z * rf) + 6.1, 2) * tuning.relief_warp_voxels);

    const float64 lf     = tuning.landscape_frequency;
    const float64 relief = std::clamp((noise_.fractal(px * lf, pz * lf, 3) * 0.9) + 0.5, 0.0, 1.0);

    const float64 mf = tuning.moisture_frequency;

    return {
        .noise    = &noise_,
        .x        = px,
        .z        = pz,
        .relief   = relief,
        .moisture = unit_noise(noise_.fractal((px * mf) + 201.0, (pz * mf) + 301.0, 2)),
    };
}

// см. docs/world.md#биомы
auto perlin_terrain_generator::relief_at(
    float64 x, float64 z
) const -> relief_sample {
    constexpr float64 weight_floor = 0.02;

    const biome_point at = climate_at(x, z);
    const float64 blend  = std::max(0.01, static_cast<float64>(params_.biome_blend));
    const float64 toning = std::max(0.01, static_cast<float64>(params_.tone_blend));

    std::array<float64, 256> distances{};
    float64 nearest     = std::numeric_limits<float64>::max();
    float64 runner_up   = std::numeric_limits<float64>::max();
    std::size_t best    = 0;
    std::size_t second  = 0;

    for (std::size_t index = 0; index < biomes_.size(); ++index) {
        const biome_climate& climate = climate_of(biomes_[index]);

        const float64 dr = at.relief - climate.relief;
        const float64 dm = at.moisture - climate.moisture;
        distances[index] = (dr * dr) + (dm * dm);
        if (distances[index] < nearest) {
            runner_up = nearest;
            second    = best;
            nearest   = distances[index];
            best      = index;
        } else if (distances[index] < runner_up) {
            runner_up = distances[index];
            second    = index;
        }
    }

    const float64 strongest = std::exp(-nearest / (blend * blend));
    float64 height          = 0.0;
    float64 total           = 0.0;
    for (std::size_t index = 0; index < biomes_.size(); ++index) {
        const float64 weight =
            std::exp(-distances[index] / (blend * blend)) - (weight_floor * strongest);
        if (weight <= 0.0) {
            continue;
        }
        height += weight * height_of(biomes_[index], at);
        total += weight;
    }

    float32 tone_share = 0.0F;
    if (second != best) {
        const float64 own   = std::exp(-nearest / (toning * toning));
        const float64 other = std::exp(-runner_up / (toning * toning));
        tone_share          = static_cast<float32>(other / (own + other));
    }

    return {
        .height     = total > 0.0 ? height / total : height_of(biomes_[best], at),
        .biome      = static_cast<uint8>(best),
        .neighbour  = static_cast<uint8>(second),
        .tone_share = tone_share,
    };
}

auto perlin_terrain_generator::biome_at(
    int32 wx, int32 wz
) const -> const terrain_biome& {
    return biomes_[shape_at(wx, wz).biome];
}

auto perlin_terrain_generator::shape_at(
    int32 wx, int32 wz
) const -> column_shape {
    const auto x = static_cast<float64>(wx);
    const auto z = static_cast<float64>(wz);

    if (!params_.island) {
        const relief_sample relief = relief_at(x, z);
        return {
            .surface    = static_cast<int32>(std::floor(relief.height)),
            .bottom     = params_.world_bottom_y,
            .height     = static_cast<float32>(relief.height),
            .biome      = relief.biome,
            .neighbour  = relief.neighbour,
            .tone_share = relief.tone_share,
        };
    }

    const region_sample region = regions_.sample(x, z);

    const float64 jf  = params_.island_jag_frequency;
    const float64 jag = noise_.fractal((x * jf) + 133.0, (z * jf) + 77.0, 2) *
                        params_.island_jag_voxels;
    const float64 inside =
        region_map::is_home(region.cell) ? region.edge_distance + jag : -1.0;

    const relief_sample relief = relief_at(x, z);
    const float64 height       = relief.height;
    const auto surface         = static_cast<int32>(std::floor(height));
    if (inside < 0.0) {
        return {
            .surface    = surface,
            .bottom     = surface + 1,
            .height     = static_cast<float32>(height),
            .biome      = relief.biome,
            .neighbour  = relief.neighbour,
            .tone_share = relief.tone_share,
        };
    }

    const float64 taper =
        std::clamp(inside / std::max(1.0, static_cast<float64>(params_.island_taper_voxels)), 0.0, 1.0);
    const float64 depth = static_cast<float64>(params_.island_min_thickness) +
                          (taper * taper * static_cast<float64>(surface - params_.world_bottom_y));

    return {
        .surface    = surface,
        .bottom     = std::max(params_.world_bottom_y, surface - static_cast<int32>(depth)),
        .height     = static_cast<float32>(height),
        .biome      = relief.biome,
        .neighbour  = relief.neighbour,
        .tone_share = relief.tone_share,
    };
}

// см. docs/world.md#слои
auto perlin_terrain_generator::paint_at_(
    int32 wx, int32 wz, const column_shape& shape, float32 slope
) const -> column_paint {
    column_facts column{
        .noise   = &noise_,
        .x       = static_cast<float64>(wx),
        .z       = static_cast<float64>(wz),
        .surface = shape.surface,
        .slope   = slope,
    };
    if (shape.tone_share > 0.0F && shape.neighbour != shape.biome) {
        lend_tones(biomes_[shape.neighbour], column);
        column.neighbour_share = shape.tone_share;
    }
    return paint_of(biomes_[shape.biome], column);
}

auto perlin_terrain_generator::rock_voxel_at(
    int32 wy
) const -> voxel {
    if (wy < (params_.world_bottom_y + params_.bedrock_thickness)) {
        return terrain::bedrock;
    }
    if (wy < params_.rock_bottom_y) {
        return terrain::stone_deep[0];
    }
    if (wy < params_.rock_deep_y) {
        return terrain::stone_deep[2];
    }
    return terrain::stone_deep[4];
}

auto perlin_terrain_generator::voxel_at(
    int32 wy, int32 surface, const column_paint& paint
) const -> matter {
    return paint.at(surface - wy).value_or(matter{rock_voxel_at(wy)});
}

auto perlin_terrain_generator::surface_height_at(
    int32 wx, int32 wz
) const -> std::optional<int32> {
    const column_shape shape = shape_at(wx, wz);
    if (shape.is_void()) {
        return std::nullopt;
    }
    return shape.surface;
}

auto perlin_terrain_generator::surface_voxel_at(
    int32 wx, int32 wz
) const -> std::optional<voxel> {
    const column_shape shape = shape_at(wx, wz);
    if (shape.is_void()) {
        return std::nullopt;
    }
    const float32 slope = slope_between(
        shape_at(wx - 1, wz).height, shape.height, shape_at(wx + 1, wz).height,
        shape_at(wx, wz - 1).height, shape_at(wx, wz + 1).height
    );
    return voxel_at(shape.surface, shape.surface, paint_at_(wx, wz, shape, slope)).color;
}

auto perlin_terrain_generator::generate(
    terrain_context& ctx
) -> void {
    constexpr int32 s = 64;

    const auto profile = sample_column_(ctx.cx, ctx.cz, std::max(ctx.voxels_per_cell, 1));

    auto floor_div = [](int32 a, int32 b) -> int32 { return a >= 0 ? a / b : (a - b + 1) / b; };

    if (profile.min_bottom > profile.max_surface) {
        return;
    }

    int32 min_cy = floor_div(profile.cell_of(profile.min_bottom), s);
    int32 max_cy = floor_div(profile.cell_of(profile.max_surface), s);

    std::vector<placed_plant> plants;
    if (params_.plants && profile.voxels_per_cell == 1) {
        plants = plants_near_(ctx.cx, ctx.cz);
    }
    for (const placed_plant& plant : plants) {
        max_cy = std::max(max_cy, floor_div(plant.root.y + plant.shape->max.y - 1, s));
    }
    const plant_footing footing = footing_of_(ctx.cx, ctx.cz, profile, plants);

    for (int32 cy = max_cy; cy >= min_cy; --cy) {
        generate_chunk(ctx, cy, profile, plants, footing);
    }
}

auto perlin_terrain_generator::sample_column_(
    int32 cx, int32 cz, int32 voxels_per_cell
) const -> column_profile {
    constexpr int32 s = column_profile::size;
    constexpr int32 a = column_profile::apron;
    constexpr int32 p = column_profile::page;

    column_profile profile{};
    profile.voxels_per_cell = voxels_per_cell;
    profile.max_surface     = std::numeric_limits<int32>::lowest();
    profile.min_bottom      = std::numeric_limits<int32>::max();

    std::array<uint8, column_profile::stride * column_profile::stride> biomes{};
    std::array<uint8, column_profile::stride * column_profile::stride> neighbours{};
    std::array<float32, column_profile::stride * column_profile::stride> shares{};
    std::array<int32, column_profile::stride * column_profile::stride> bottoms{};

    for (int32 i = 0; i < column_profile::stride; ++i) {
        for (int32 j = 0; j < column_profile::stride; ++j) {
            const column_shape shape = shape_at(
                ((cx * s) + i - a) * voxels_per_cell, ((cz * s) + j - a) * voxels_per_cell
            );
            const int32 cell      = (i * column_profile::stride) + j;
            profile.surface[cell] = shape.surface;
            profile.height[cell]  = shape.height;
            biomes[cell]          = shape.biome;
            neighbours[cell]      = shape.neighbour;
            shares[cell]          = shape.tone_share;
            bottoms[cell]         = shape.bottom;
        }
    }

    profile.page_min_rock.fill(std::numeric_limits<int32>::max());
    profile.page_max_surface.fill(std::numeric_limits<int32>::lowest());
    profile.page_min_bottom.fill(std::numeric_limits<int32>::max());
    profile.page_max_bottom.fill(std::numeric_limits<int32>::lowest());

    for (int32 x = 0; x < s; ++x) {
        for (int32 z = 0; z < s; ++z) {
            const int32 cell = column_profile::ring_index(x, z);
            const column_shape shape{
                .surface    = profile.surface[cell],
                .bottom     = bottoms[cell],
                .height     = profile.height[cell],
                .biome      = biomes[cell],
                .neighbour  = neighbours[cell],
                .tone_share = shares[cell],
            };
            profile.bottom[(x * s) + z] = shape.bottom;
            if (shape.is_void()) {
                continue;
            }

            const float32 slope = slope_between(
                profile.height[column_profile::ring_index(x - 1, z)], shape.height,
                profile.height[column_profile::ring_index(x + 1, z)],
                profile.height[column_profile::ring_index(x, z - 1)],
                profile.height[column_profile::ring_index(x, z + 1)]
            ) / static_cast<float32>(voxels_per_cell);

            const column_paint paint = paint_at_(
                ((cx * s) + x) * voxels_per_cell, ((cz * s) + z) * voxels_per_cell, shape, slope
            );
            profile.paint[(x * s) + z] = paint;

            const int32 page = ((x / p) * column_profile::pages) + (z / p);
            profile.page_min_rock[page] =
                std::min(profile.page_min_rock[page], shape.surface - paint.depth());
            profile.page_max_surface[page] = std::max(profile.page_max_surface[page], shape.surface);
            profile.page_min_bottom[page]  = std::min(profile.page_min_bottom[page], shape.bottom);
            profile.page_max_bottom[page]  = std::max(profile.page_max_bottom[page], shape.bottom);

            profile.max_surface = std::max(profile.max_surface, shape.surface);
            profile.min_bottom  = std::min(profile.min_bottom, shape.bottom);
        }
    }

    return profile;
}

// см. docs/world.md#покров
auto perlin_terrain_generator::cover_of_(
    const terrain_context& ctx, int32 chunk_y, const column_profile& profile, const asset::model& voxels,
    const plant_footing& footing
) const -> asset::cover_layer {
    constexpr int32 s = 64;

    if (profile.voxels_per_cell != 1) {
        return asset::cover_layer{};
    }

    const int32 base_y = chunk_y * s;
    std::vector<asset::cover_layer::entry> entries;

    for (int32 x = 0; x < s; ++x) {
        for (int32 z = 0; z < s; ++z) {
            const uint8 form = profile.paint[(x * s) + z].cover;
            if (form == 0 || footing.test(static_cast<std::size_t>((x * s) + z))) {
                continue;
            }
            const int32 surface = profile.surface[column_profile::ring_index(x, z)];
            const int32 local_y = surface - base_y;
            if (local_y < 0 || local_y >= s || profile.bottom[(x * s) + z] > surface) {
                continue;
            }
            if (voxels.get_voxel(x, local_y, z).is_empty()) {
                continue;
            }
            if (local_y + 1 < s && !voxels.get_voxel(x, local_y + 1, z).is_empty()) {
                continue;
            }
            entries.push_back({.support = vec3i{x, local_y, z}, .form = form});
        }
    }

    return asset::cover_layer{entries};
}

auto perlin_terrain_generator::generate_chunk(
    terrain_context& ctx, int32 chunk_y, const column_profile& profile, std::span<const placed_plant> plants,
    const plant_footing& footing
) -> void {
    constexpr int32 s = 64;

    const int32 voxels_per_cell = profile.voxels_per_cell;

    auto mdl = std::make_shared<vw::asset::model>(
        *identity_pool_, *page_pool_, s, s, s,
        params_.world_units_per_voxel * voxels_per_cell
    );

    constexpr int32 p  = column_profile::page;
    constexpr int32 pn = column_profile::pages;

    const int32 base_y      = chunk_y * s;
    const int32 bottom_cell = profile.cell_of(params_.world_bottom_y);

    vw::asset::model_writer writer{*mdl};

    for (int32 py = 0; py < pn; ++py) {
        const int32 y0 = base_y + (py * p);
        const int32 y1 = y0 + p - 1;

        if (y1 < bottom_cell) {
            continue;
        }

        const bool one_rock = y0 >= bottom_cell &&
            rock_voxel_at(y0 * voxels_per_cell) ==
                rock_voxel_at((y1 * voxels_per_cell) + voxels_per_cell - 1);

        for (int32 px = 0; px < pn; ++px) {
            for (int32 pz = 0; pz < pn; ++pz) {
                const int32 page = (px * pn) + pz;

                if (y0 > profile.cell_of(profile.page_max_surface[page]) ||
                    y1 < profile.cell_of(profile.page_min_bottom[page])) {
                    continue;
                }

                if (one_rock && y0 > profile.cell_of(profile.page_max_bottom[page]) &&
                    ((y1 * voxels_per_cell) + voxels_per_cell - 1) < profile.page_min_rock[page]) {
                    writer.fill_page(px, py, pz, rock_voxel_at(y0 * voxels_per_cell));
                    continue;
                }

                for (int32 lx = 0; lx < p; ++lx) {
                    const int32 x = (px * p) + lx;

                    for (int32 lz = 0; lz < p; ++lz) {
                        const int32 z = (pz * p) + lz;

                        const int32 surface = profile.surface[column_profile::ring_index(x, z)];
                        const int32 floor   = profile.bottom[(x * s) + z];
                        if (floor > surface) {
                            continue;
                        }
                        const column_paint& paint = profile.paint[(x * s) + z];

                        const int32 surface_cell = profile.cell_of(surface);

                        const int32 top    = std::min(surface_cell, y1);
                        const int32 bottom =
                            std::max(y0, std::max(bottom_cell, profile.cell_of(floor)));

                        for (int32 cell_y = bottom; cell_y <= top; ++cell_y) {
                            const int32 wy = cell_y == surface_cell
                                ? surface
                                : cell_y * voxels_per_cell;

                            writer.set(x, cell_y - base_y, z, voxel_at(wy, surface, paint));
                        }
                    }
                }
            }
        }
    }

    carve_caves_(writer, ctx, chunk_y, profile);
    plant_chunk_(writer, *mdl, vec3i{ctx.cx * s, base_y, ctx.cz * s}, plants);

    writer.compact_pages();

    auto cover = cover_of_(ctx, chunk_y, profile, *mdl, footing);
    ctx.create_chunk(chunk_y) = {
        vec3i{ctx.cx, chunk_y, ctx.cz},
        std::make_shared<vw::asset::chunk_volume>(std::move(mdl), std::move(cover))
    };
}

}  // namespace vw::ecs

