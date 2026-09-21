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


perlin_terrain_generator::perlin_terrain_generator(
    vw::asset::model_identity_pool& identity_pool, vw::asset::page_pool& pool
)
    : perlin_terrain_generator(identity_pool, pool, params{}) {}

perlin_terrain_generator::perlin_terrain_generator(
    vw::asset::model_identity_pool& identity_pool, vw::asset::page_pool& pool, params p
)
    : identity_pool_(&identity_pool), page_pool_(&pool), params_(p) {
    for (int32 i = 0; i < 256; ++i) {
        perm_[i] = i;
    }

    uint32 state = params_.seed;
    for (int32 i = 255; i > 0; --i) {
        state   = state * 1664525u + 1013904223u;
        int32 j = static_cast<int32>(state % static_cast<uint32>(i + 1));
        std::swap(perm_[i], perm_[j]);
    }

    for (int32 i = 0; i < 256; ++i) {
        perm_[i + 256] = perm_[i];
    }
}

auto perlin_terrain_generator::fade(
    float64 t
) -> float64 {
    return t * t * t * (t * (t * 6.0 - 15.0) + 10.0);
}

auto perlin_terrain_generator::lerp(
    float64 t, float64 a, float64 b
) -> float64 {
    return a + t * (b - a);
}

auto perlin_terrain_generator::grad(
    int32 hash, float64 x, float64 y
) -> float64 {
    int32 h   = hash & 3;
    float64 u = h < 2 ? x : y;
    float64 v = h < 2 ? y : x;
    return ((h & 1) ? -u : u) + ((h & 2) ? -v : v);
}

auto perlin_terrain_generator::noise2d(
    float64 x, float64 y
) const -> float64 {
    int32 xi = static_cast<int32>(std::floor(x)) & 255;
    int32 yi = static_cast<int32>(std::floor(y)) & 255;

    float64 xf = x - std::floor(x);
    float64 yf = y - std::floor(y);

    float64 u = fade(xf);
    float64 v = fade(yf);

    int32 aa = perm_[perm_[xi] + yi];
    int32 ab = perm_[perm_[xi] + yi + 1];
    int32 ba = perm_[perm_[xi + 1] + yi];
    int32 bb = perm_[perm_[xi + 1] + yi + 1];

    float64 x1 = lerp(u, grad(aa, xf, yf), grad(ba, xf - 1.0, yf));
    float64 x2 = lerp(u, grad(ab, xf, yf - 1.0), grad(bb, xf - 1.0, yf - 1.0));

    return lerp(v, x1, x2);
}

auto perlin_terrain_generator::noise3d(
    float64 x, float64 y, float64 z
) const -> float64 {
    const int32 xi = static_cast<int32>(std::floor(x)) & 255;
    const int32 yi = static_cast<int32>(std::floor(y)) & 255;
    const int32 zi = static_cast<int32>(std::floor(z)) & 255;

    const float64 xf = x - std::floor(x);
    const float64 yf = y - std::floor(y);
    const float64 zf = z - std::floor(z);

    const float64 u = fade(xf);
    const float64 v = fade(yf);
    const float64 w = fade(zf);

    const int32 a  = perm_[xi] + yi;
    const int32 aa = perm_[a] + zi;
    const int32 ab = perm_[a + 1] + zi;
    const int32 b  = perm_[xi + 1] + yi;
    const int32 ba = perm_[b] + zi;
    const int32 bb = perm_[b + 1] + zi;

    const auto g = [](int32 hash, float64 px, float64 py, float64 pz) -> float64 {
        const int32 h    = hash & 15;
        const float64 gu = h < 8 ? px : py;
        const float64 gv = h < 4 ? py : (h == 12 || h == 14 ? px : pz);
        return ((h & 1) != 0 ? -gu : gu) + ((h & 2) != 0 ? -gv : gv);
    };

    const float64 x1 = lerp(u, g(perm_[aa], xf, yf, zf), g(perm_[ba], xf - 1.0, yf, zf));
    const float64 x2 = lerp(u, g(perm_[ab], xf, yf - 1.0, zf), g(perm_[bb], xf - 1.0, yf - 1.0, zf));
    const float64 x3 =
        lerp(u, g(perm_[aa + 1], xf, yf, zf - 1.0), g(perm_[ba + 1], xf - 1.0, yf, zf - 1.0));
    const float64 x4 = lerp(
        u, g(perm_[ab + 1], xf, yf - 1.0, zf - 1.0), g(perm_[bb + 1], xf - 1.0, yf - 1.0, zf - 1.0)
    );

    return lerp(w, lerp(v, x1, x2), lerp(v, x3, x4));
}

auto perlin_terrain_generator::cave_field_at(
    int32 wx, int32 wy, int32 wz, int32 depth
) const -> float32 {
    const auto f = params_.cave_field_frequency;
    const auto n = static_cast<float32>(noise3d(
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
    const auto n  = static_cast<float32>(noise2d(
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
        noise3d(x * fc, y * fc * params_.cave_cheese_squash, z * fc)
    );
    const float32 cheese_open =
        (params_.cave_cheese_width * field * band) - std::abs(cheese);

    const auto ft = params_.cave_tunnel_frequency;
    const auto ta = static_cast<float32>(noise3d((x * ft) + 71.5, (y * ft) + 13.7, (z * ft) + 39.1));
    const auto tb = static_cast<float32>(noise3d((x * ft) - 128.3, (y * ft) + 96.2, (z * ft) - 57.4));
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

                const int32 surface = profile.surface[(lx * s) + lz];
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

auto perlin_terrain_generator::octave_noise(
    float64 x, float64 y
) const -> float64 {
    float64 total   = 0.0;
    float64 freq    = 1.0;
    float64 amp     = 1.0;
    float64 max_amp = 0.0;

    for (int32 i = 0; i < params_.octaves; ++i) {
        total += noise2d(x * freq, y * freq) * amp;
        max_amp += amp;
        freq *= params_.lacunarity;
        amp *= params_.persistence;
    }

    return total / max_amp;
}

auto perlin_terrain_generator::ridged_noise(
    float64 x, float64 y
) const -> float64 {
    float64 total   = 0.0;
    float64 freq    = 1.0;
    float64 amp     = 1.0;
    float64 max_amp = 0.0;

    for (int32 i = 0; i < params_.octaves; ++i) {
        float64 n = noise2d(x * freq, y * freq);
        n         = 1.0 - std::abs(n);
        n         = n * n;
        total += n * amp;
        max_amp += amp;
        freq *= params_.lacunarity;
        amp *= params_.persistence;
    }

    return total / max_amp;
}

auto perlin_terrain_generator::continent_at(
    float64 nx, float64 nz
) const -> float64 {
    float64 c = octave_noise(nx * params_.continent_frequency, nz * params_.continent_frequency);
    return (c + 1.0) * 0.5;
}

auto perlin_terrain_generator::stone_height_at(
    int32 wx, int32 wz
) const -> int32 {
    auto nx = static_cast<float64>(wx);
    auto nz = static_cast<float64>(wz);

    float64 warp_x =
        octave_noise(nx * params_.warp_frequency + 5.2, nz * params_.warp_frequency + 1.3) *
        params_.warp_strength;
    float64 warp_z =
        octave_noise(nx * params_.warp_frequency + 9.7, nz * params_.warp_frequency + 6.1) *
        params_.warp_strength;

    float64 warped_x = nx + warp_x;
    float64 warped_z = nz + warp_z;

    float64 continent = continent_at(nx, nz);

    float64 terrain =
        octave_noise(warped_x * params_.terrain_frequency, warped_z * params_.terrain_frequency);
    terrain = (terrain + 1.0) * 0.5;

    float64 ridge =
        ridged_noise(warped_x * params_.ridge_frequency, warped_z * params_.ridge_frequency);

    float64 mixed = terrain * (1.0 - params_.ridge_weight) + ridge * params_.ridge_weight;

    int32 base_h;
    float64 amplitude;
    if (continent < 0.35) {
        base_h    = params_.plains_height;
        amplitude = 5.0;
    } else if (continent < 0.55) {
        float64 t = (continent - 0.35) / 0.2;
        base_h    = params_.plains_height +
            static_cast<int32>(t * (params_.hills_height - params_.plains_height));
        amplitude = 5.0 + t * 15.0;
    } else if (continent < 0.75) {
        float64 t = (continent - 0.55) / 0.2;
        base_h    = params_.hills_height +
            static_cast<int32>(t * (params_.mountains_height - params_.hills_height));
        amplitude = 20.0 + t * 20.0;
    } else {
        base_h    = params_.mountains_height;
        amplitude = 40.0;
    }

    return base_h + static_cast<int32>(mixed * amplitude);
}

auto perlin_terrain_generator::soil_depth_at(
    int32 wx, int32 wz, int32 stone, float32 slope
) const -> int32 {
    const float64 n = noise2d(
        static_cast<float64>(wx) * params_.soil_frequency,
        static_cast<float64>(wz) * params_.soil_frequency
    );

    auto t = static_cast<float32>((n + 1.0) * 0.5);

    t *= std::clamp(1.0F - (slope / params_.soil_slope_limit), 0.0F, 1.0F);

    if (stone > params_.soil_altitude_start) {
        const auto start = static_cast<float32>(params_.soil_altitude_start);
        const auto end   = static_cast<float32>(params_.soil_altitude_end);
        t *= std::clamp((end - static_cast<float32>(stone)) / (end - start), 0.0F, 1.0F);
    }

    return static_cast<int32>((t * static_cast<float32>(params_.soil_depth_max)) + 0.5F);
}

auto perlin_terrain_generator::rock_voxel_at(
    int32 wy
) const -> voxel {
    if (wy < (params_.world_bottom_y + params_.bedrock_thickness)) {
        return voxels::world::bedrock;
    }
    if (wy < params_.rock_bottom_y) {
        return voxels::world::stone_deep[0];
    }
    if (wy < params_.rock_deep_y) {
        return voxels::world::stone_deep[1];
    }
    return voxels::world::stone_deep[2];
}

auto perlin_terrain_generator::voxel_at(
    int32 wy, int32 stone_top, int32 surface_top
) const -> voxel {
    if (wy > stone_top) {
        return wy == surface_top ? voxels::world::grass[0] : voxels::world::dirt[0];
    }

    if (wy == stone_top && surface_top == stone_top) {
        return wy > params_.snow_line ? voxels::world::snow[1] : voxels::world::stone[1];
    }
    if ((stone_top - wy) < params_.rock_skin) {
        return voxels::world::stone[0];
    }

    return rock_voxel_at(wy);
}

auto perlin_terrain_generator::surface_height_at(
    int32 wx, int32 wz
) const -> int32 {
    const int32 stone = stone_height_at(wx, wz);

    const auto dx = stone_height_at(wx + 1, wz) - stone_height_at(wx - 1, wz);
    const auto dz = stone_height_at(wx, wz + 1) - stone_height_at(wx, wz - 1);

    const auto slope =
        0.5F * static_cast<float32>(std::max(std::abs(dx), std::abs(dz)));

    return stone + soil_depth_at(wx, wz, stone, slope);
}

auto perlin_terrain_generator::generate(
    terrain_context& ctx
) -> void {
    constexpr int32 s = 64;

    const auto profile = sample_column_(ctx.cx, ctx.cz, std::max(ctx.voxels_per_cell, 1));

    auto floor_div = [](int32 a, int32 b) -> int32 { return a >= 0 ? a / b : (a - b + 1) / b; };

    int32 min_cy = floor_div(profile.cell_of(params_.world_bottom_y), s);
    int32 max_cy = floor_div(profile.cell_of(profile.max_surface), s);

    for (int32 cy = max_cy; cy >= min_cy; --cy) {
        generate_chunk(ctx, cy, profile);
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
    profile.min_stone       = std::numeric_limits<int32>::max();
    profile.max_surface     = std::numeric_limits<int32>::lowest();

    for (int32 i = 0; i < column_profile::stride; ++i) {
        for (int32 j = 0; j < column_profile::stride; ++j) {
            profile.stone[(i * column_profile::stride) + j] = stone_height_at(
                ((cx * s) + i - a) * voxels_per_cell, ((cz * s) + j - a) * voxels_per_cell
            );
        }
    }

    profile.page_min_stone.fill(std::numeric_limits<int32>::max());
    profile.page_max_surface.fill(std::numeric_limits<int32>::lowest());

    for (int32 x = 0; x < s; ++x) {
        for (int32 z = 0; z < s; ++z) {
            const int32 stone = profile.stone[column_profile::stone_index(x, z)];

            const auto dx = profile.stone[column_profile::stone_index(x + 1, z)] -
                profile.stone[column_profile::stone_index(x - 1, z)];
            const auto dz = profile.stone[column_profile::stone_index(x, z + 1)] -
                profile.stone[column_profile::stone_index(x, z - 1)];

            const auto slope = 0.5F * static_cast<float32>(std::max(std::abs(dx), std::abs(dz))) /
                               static_cast<float32>(voxels_per_cell);

            const int32 surface = stone +
                soil_depth_at(
                    ((cx * s) + x) * voxels_per_cell, ((cz * s) + z) * voxels_per_cell, stone,
                    slope
                );

            profile.surface[(x * s) + z] = surface;

            const int32 page = ((x / p) * column_profile::pages) + (z / p);
            profile.page_min_stone[page]   = std::min(profile.page_min_stone[page], stone);
            profile.page_max_surface[page] = std::max(profile.page_max_surface[page], surface);

            profile.min_stone   = std::min(profile.min_stone, stone);
            profile.max_surface = std::max(profile.max_surface, surface);
        }
    }

    return profile;
}

auto perlin_terrain_generator::generate_chunk(
    terrain_context& ctx, int32 chunk_y, const column_profile& profile
) -> void {
    constexpr int32 s = 64;

    const int32 voxels_per_cell = profile.voxels_per_cell;

    auto mdl = std::make_shared<vw::asset::model>(
        *identity_pool_, *page_pool_, vw::voxels::world::category, s, s, s,
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

                if (y0 > profile.cell_of(profile.page_max_surface[page])) {
                    continue;
                }

                if (one_rock &&
                    ((y1 * voxels_per_cell) + voxels_per_cell - 1) <
                        (profile.page_min_stone[page] - params_.rock_skin)) {
                    writer.fill_page(px, py, pz, rock_voxel_at(y0 * voxels_per_cell));
                    continue;
                }

                for (int32 lx = 0; lx < p; ++lx) {
                    const int32 x = (px * p) + lx;

                    for (int32 lz = 0; lz < p; ++lz) {
                        const int32 z = (pz * p) + lz;

                        const int32 stone   = profile.stone[column_profile::stone_index(x, z)];
                        const int32 surface = profile.surface[(x * s) + z];

                        const int32 surface_cell = profile.cell_of(surface);

                        const int32 top    = std::min(surface_cell, y1);
                        const int32 bottom = std::max(y0, bottom_cell);

                        for (int32 cell_y = bottom; cell_y <= top; ++cell_y) {
                            const int32 wy = cell_y == surface_cell
                                ? surface
                                : cell_y * voxels_per_cell;

                            writer.set(x, cell_y - base_y, z, voxel_at(wy, stone, surface));
                        }
                    }
                }
            }
        }
    }

    carve_caves_(writer, ctx, chunk_y, profile);

    writer.compact_pages();

    ctx.create_chunk(chunk_y) = {
        vec3i{ctx.cx, chunk_y, ctx.cz},
        std::make_shared<vw::asset::chunk_volume>(std::move(mdl))
    };
}

}  // namespace vw::ecs

