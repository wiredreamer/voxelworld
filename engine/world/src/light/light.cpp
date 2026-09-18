module vw.world;

import std;
import vw.core;
import vw.asset;

namespace vw::ecs {

using namespace ::vw::asset;

namespace {

constexpr int32 s     = light_column::side;
constexpr int32 apron = light_column::apron;
constexpr int32 span  = light_column::span;

constexpr int32 words = 3;
constexpr int32 bit0  = s - apron;

static_assert(bit0 + span <= words * 64);
static_assert(bit0 + apron == 64);

constexpr uint64 outside_low  = (uint64{1} << bit0) - 1;
constexpr uint64 outside_high = ~((uint64{1} << (bit0 + span - 128)) - 1);

[[nodiscard]] auto row_base(int32 y, int32 z) -> std::size_t {
    return ((static_cast<std::size_t>(y) * span) + static_cast<std::size_t>(z)) * words;
}

auto seal_outside(uint64* row) -> void {
    row[0] |= outside_low;
    row[2] |= outside_high;
}

auto pad_as_sky(const uint64* in, uint64* out) -> void {
    out[0] = in[0] | outside_low;
    out[1] = in[1];
    out[2] = in[2] | outside_high;
}

auto shift_west(const uint64* in, uint64* out) -> void {
    out[2] = (in[2] << 1) | (in[1] >> 63);
    out[1] = (in[1] << 1) | (in[0] >> 63);
    out[0] = in[0] << 1;
    seal_outside(out);
}

auto shift_east(const uint64* in, uint64* out) -> void {
    out[0] = (in[0] >> 1) | (in[1] << 63);
    out[1] = (in[1] >> 1) | (in[2] << 63);
    out[2] = in[2] >> 1;
    seal_outside(out);
}

}  // namespace

light_column::light_column(std::span<const chunk_occupancy* const> chunks_bottom_up)
    : light_column{
          neighbourhood{{
              {}, {}, {}, {}, column_slice{.occupancy = chunks_bottom_up, .models = {}},
              {}, {}, {}, {}
          }},
          emission_table{},
          {}
      } {}

light_column::light_column(
    const neighbourhood& around, const emission_table& emission, light_scratch scratch
)
    : buffers_{std::move(scratch)} {
    int32 chunks = 0;
    for (const auto& column : around) {
        chunks = std::max(chunks, static_cast<int32>(column.occupancy.size()));
    }

    height_ = chunks * s;
    if (height_ == 0) {
        return;
    }

    buffers_.levels.assign(static_cast<std::size_t>(height_) * span * span, 0);

    build_solid_(around);

    seed_sky_();
    spread_(light_channel::sky);

    seed_block_(around, emission);
    spread_(light_channel::block);
}

auto light_column::solid_at_(int32 x, int32 y, int32 z) const -> bool {
    const int32 bit = x + bit0;
    return ((buffers_.solid[row_base(y, z) + static_cast<std::size_t>(bit / 64)] >>
             (bit % 64)) &
            1U) != 0;
}

auto light_column::build_solid_(const neighbourhood& around) -> void {
    auto& solid = buffers_.solid;

    const auto plane = static_cast<std::size_t>(height_) * span * words;
    solid.assign(plane, 0);

    for (int32 dz = -1; dz <= 1; ++dz) {
        for (int32 dx = -1; dx <= 1; ++dx) {
            const auto& column =
                around[static_cast<std::size_t>(((dz + 1) * 3) + (dx + 1))].occupancy;
            const int32 word = 1 + dx;

            for (int32 lz = 0; lz < s; ++lz) {
                const int32 z = apron + (dz * s) + lz;
                if (z < 0 || z >= span) {
                    continue;
                }

                if (column.empty()) {
                    for (int32 y = 0; y < height_; ++y) {
                        solid[row_base(y, z) + static_cast<std::size_t>(word)] = ~uint64{0};
                    }
                    continue;
                }

                for (std::size_t i = 0; i < column.size(); ++i) {
                    const chunk_occupancy* occ = column[i];
                    if (occ == nullptr) {
                        continue;
                    }

                    const int32 base = static_cast<int32>(i) * s;
                    for (int32 ly = 0; ly < s; ++ly) {
                        solid[row_base(base + ly, z) + static_cast<std::size_t>(word)] =
                            occ->row(ly, lz);
                    }
                }
            }
        }
    }

    for (int32 y = 0; y < height_; ++y) {
        for (int32 z = 0; z < span; ++z) {
            seal_outside(&solid[row_base(y, z)]);
        }
    }
}

auto light_column::seed_sky_() -> void {
    auto& levels = buffers_.levels;
    auto& solid  = buffers_.solid;
    auto& sky    = buffers_.sky;

    const auto plane = static_cast<std::size_t>(height_) * span * words;

    sky.assign(plane, 0);
    for (int32 z = 0; z < span; ++z) {
        uint64 open[words] = {~uint64{0}, ~uint64{0}, ~uint64{0}};
        for (int32 y = height_ - 1; y >= 0; --y) {
            const auto at = row_base(y, z);

            uint64 any = 0;
            for (int32 w = 0; w < words; ++w) {
                open[w] &= ~solid[at + static_cast<std::size_t>(w)];
                sky[at + static_cast<std::size_t>(w)] = open[w];
                any |= open[w];
            }

            if (any == 0) {
                break;
            }
        }
    }

    auto& seeds = buffers_.seeds[max_level];
    seeds.clear();

    const uint64 all_sky[words] = {~uint64{0}, ~uint64{0}, ~uint64{0}};

    for (int32 y = 0; y < height_; ++y) {
        for (int32 z = 0; z < span; ++z) {
            const auto at        = row_base(y, z);
            const uint64* here   = &sky[at];
            const uint64* north  = (z > 0) ? &sky[row_base(y, z - 1)] : all_sky;
            const uint64* south  = (z + 1 < span) ? &sky[row_base(y, z + 1)] : all_sky;

            if ((here[0] | here[1] | here[2]) == 0) {
                continue;
            }

            uint64 padded[words]{};
            uint64 west[words]{};
            uint64 east[words]{};
            uint64 north_pad[words]{};
            uint64 south_pad[words]{};

            pad_as_sky(here, padded);
            pad_as_sky(north, north_pad);
            pad_as_sky(south, south_pad);
            shift_west(padded, west);
            shift_east(padded, east);

            for (int32 w = 0; w < words; ++w) {
                uint64 lit = here[w];
                while (lit != 0) {
                    const auto b = static_cast<int32>(std::countr_zero(lit));
                    lit &= lit - 1;
                    levels[static_cast<std::size_t>(
                        index_((w * 64) + b - bit0, y, z)
                    )] = max_level;
                }

                uint64 edge =
                    here[w] & ~(west[w] & east[w] & north_pad[w] & south_pad[w]);
                while (edge != 0) {
                    const auto b = static_cast<int32>(std::countr_zero(edge));
                    edge &= edge - 1;
                    seeds.push_back(index_((w * 64) + b - bit0, y, z));
                }
            }
        }
    }

}

auto light_column::seed_block_(
    const neighbourhood& around, const emission_table& emission
) -> void {
    auto& levels = buffers_.levels;

    constexpr int32 ps = model::page_size;

    const auto each_page = [&](auto&& body) {
        for (int32 dz = -1; dz <= 1; ++dz) {
            for (int32 dx = -1; dx <= 1; ++dx) {
                const auto slot    = static_cast<std::size_t>(((dz + 1) * 3) + (dx + 1));
                const auto& models = around[slot].models;

                for (std::size_t i = 0; i < models.size(); ++i) {
                    const model* mdl = models[i];
                    if (mdl == nullptr) {
                        continue;
                    }

                    const int32 floor = static_cast<int32>(i) * s;

                    for (int32 pz = 0; pz < mdl->pages_z(); ++pz) {
                        const int32 z0 = apron + (dz * s) + (pz * ps);
                        if (z0 + ps <= 0 || z0 >= span) {
                            continue;
                        }

                        for (int32 px = 0; px < mdl->pages_x(); ++px) {
                            const int32 x0 = apron + (dx * s) + (px * ps);
                            if (x0 + ps <= 0 || x0 >= span) {
                                continue;
                            }

                            for (int32 py = 0; py < mdl->pages_y(); ++py) {
                                const page_mode mode = mdl->get_page_mode(px, py, pz);
                                if (mode == page_mode::empty) {
                                    continue;
                                }

                                const bool uniform = mode == page_mode::uniform;
                                const uint8 fill =
                                    uniform ? emission.row(mdl->category())
                                                  [mdl->get_page_fill_index(px, py, pz).value]
                                            : uint8{0};

                                if (uniform && fill == 0) {
                                    continue;
                                }

                                body(
                                    *mdl, px, py, pz, x0, floor + (py * ps), z0, uniform, fill
                                );
                            }
                        }
                    }
                }
            }
        }
    };

    each_page([&](const model& mdl, int32 px, int32 py, int32 pz, int32 x0, int32 y0,
                  int32 z0, bool uniform, uint8 fill) {
        const model::page_type* page = uniform ? nullptr : mdl.get_page(px, py, pz);

        const auto& emits = emission.row(mdl.category());

        for (int32 lz = 0; lz < ps; ++lz) {
            const int32 z = z0 + lz;
            if (z < 0 || z >= span) {
                continue;
            }

            for (int32 ly = 0; ly < ps; ++ly) {
                const int32 y = y0 + ly;

                for (int32 lx = 0; lx < ps; ++lx) {
                    const int32 x = x0 + lx;
                    if (x < 0 || x >= span) {
                        continue;
                    }

                    const uint8 level =
                        uniform ? fill
                                : emits[(*page)[static_cast<std::size_t>(
                                                    lx + (ly * ps) + (lz * ps * ps)
                                                )]
                                            .value];
                    if (level == 0) {
                        continue;
                    }

                    const auto at = static_cast<std::size_t>(index_(x, y, z));
                    levels[at]    = static_cast<uint8>((levels[at] & 0x0FU) | (level << 4));
                }
            }
        }
    });

    const auto gives = [&](int32 x, int32 y, int32 z, uint8 level) -> bool {
        const auto child = static_cast<uint8>(level - 1);

        const auto lower = [&](int32 nx, int32 ny, int32 nz) -> bool {
            if (nx < 0 || nx >= span || nz < 0 || nz >= span || ny < 0 || ny >= height_) {
                return false;
            }
            if (solid_at_(nx, ny, nz)) {
                return false;
            }
            return (levels[static_cast<std::size_t>(index_(nx, ny, nz))] >> 4) < child;
        };

        return lower(x - 1, y, z) || lower(x + 1, y, z) || lower(x, y, z - 1) ||
               lower(x, y, z + 1) || lower(x, y - 1, z) || lower(x, y + 1, z);
    };

    each_page([&]([[maybe_unused]] const model& mdl, [[maybe_unused]] int32 px,
                  [[maybe_unused]] int32 py, [[maybe_unused]] int32 pz, int32 x0, int32 y0,
                  int32 z0, bool uniform, [[maybe_unused]] uint8 fill) {
        for (int32 lz = 0; lz < ps; ++lz) {
            const int32 z = z0 + lz;
            if (z < 0 || z >= span) {
                continue;
            }

            for (int32 ly = 0; ly < ps; ++ly) {
                const int32 y = y0 + ly;

                for (int32 lx = 0; lx < ps; ++lx) {
                    const bool shell = lx == 0 || lx == ps - 1 || ly == 0 || ly == ps - 1 ||
                                       lz == 0 || lz == ps - 1;
                    if (uniform && !shell) {
                        continue;
                    }

                    const int32 x = x0 + lx;
                    if (x < 0 || x >= span) {
                        continue;
                    }

                    const auto at    = static_cast<std::size_t>(index_(x, y, z));
                    const auto level = static_cast<uint8>(levels[at] >> 4);

                    if (level <= 1) {
                        continue;
                    }

                    if (gives(x, y, z, level)) {
                        buffers_.seeds[level].push_back(static_cast<int32>(at));
                    }
                }
            }
        }
    });
}

auto light_column::spread_(light_channel channel) -> void {
    auto& levels  = buffers_.levels;
    auto& solid   = buffers_.solid;
    auto& current = buffers_.frontier;
    auto& next    = buffers_.next;

    const int32 shift = shift_of(channel);
    const auto keep   = static_cast<uint8>(channel == light_channel::sky ? 0xF0U : 0x0FU);

    current.clear();

    for (uint8 level = max_level; level > 1; --level) {
        auto& entering = buffers_.seeds[level];
        current.insert(current.end(), entering.begin(), entering.end());
        entering.clear();

        if (current.empty()) {
            continue;
        }

        const auto child = static_cast<uint8>(level - 1);
        next.clear();

        for (const int32 at : current) {
            const int32 x = at % span;
            const int32 z = (at / span) % span;
            const int32 y = at / (span * span);

            const auto visit = [&](int32 nx, int32 ny, int32 nz) {
                if (nx < 0 || nx >= span || nz < 0 || nz >= span || ny < 0 || ny >= height_) {
                    return;
                }

                const int32 bit = nx + bit0;
                if (((solid[row_base(ny, nz) + static_cast<std::size_t>(bit / 64)] >>
                      (bit % 64)) &
                     1U) != 0) {
                    return;
                }

                const auto to = static_cast<std::size_t>(index_(nx, ny, nz));
                if (((levels[to] >> shift) & 0x0FU) >= child) {
                    return;
                }

                levels[to] =
                    static_cast<uint8>((levels[to] & keep) | (child << shift));
                next.push_back(static_cast<int32>(to));
            };

            visit(x - 1, y, z);
            visit(x + 1, y, z);
            visit(x, y, z - 1);
            visit(x, y, z + 1);
            visit(x, y - 1, z);
            visit(x, y + 1, z);
        }

        current.swap(next);
    }

    buffers_.seeds[1].clear();
    buffers_.seeds[0].clear();
    current.clear();
}

auto gather_boundary(
    const light_column& column, int32 y_base, light_channel channel
) -> light_field::boundary_light {
    constexpr int32 side = light_field::side;

    light_field::boundary_light out;

    const auto at = [&](int32 x, int32 y, int32 z) -> uint8 {
        if (y < 0) {
            return 0;
        }
        if (y >= column.height()) {
            return channel == light_channel::sky ? light_column::max_level : uint8{0};
        }
        return column.level_at(x, y, z, channel);
    };

    thread_local std::vector<uint8> packed;

    for (const face_direction face : all_face_directions) {
        packed.assign(static_cast<std::size_t>(side) * side / 2, 0);

        uint8 first  = 0;
        bool uniform = true;

        const auto put = [&](int32 slot, uint8 level) {
            if (slot == 0) {
                first = level;
            }
            uniform = uniform && level == first;

            packed[static_cast<std::size_t>(slot / 2)] |=
                static_cast<uint8>((slot % 2) == 0 ? level : (level << 4));
        };

        switch (axis_of(face)) {
            case 0: {
                const int32 x = is_positive(face) ? side : -1;
                for (int32 y = 0; y < side; ++y) {
                    for (int32 z = 0; z < side; ++z) {
                        put((y * side) + z, at(x, y_base + y, z));
                    }
                }
                break;
            }
            case 1: {
                const int32 y = is_positive(face) ? y_base + side : y_base - 1;
                for (int32 z = 0; z < side; ++z) {
                    for (int32 x = 0; x < side; ++x) {
                        put((x * side) + z, at(x, y, z));
                    }
                }
                break;
            }
            default: {
                const int32 z = is_positive(face) ? side : -1;
                for (int32 y = 0; y < side; ++y) {
                    for (int32 x = 0; x < side; ++x) {
                        put((x * side) + y, at(x, y_base + y, z));
                    }
                }
                break;
            }
        }

        out.uniform[face] = first;
        if (!uniform) {
            out.packed[face].assign(packed.begin(), packed.end());
        }
    }

    return out;
}

auto light_column::bake(int32 y_base, light_channel channel) const -> light_field {
    constexpr int32 pages_side = light_field::pages_side;
    constexpr int32 page_count = light_field::page_count;
    constexpr int32 page_side  = light_field::page;

    if (y_base < 0 || y_base + s > height_) {
        return light_field{};
    }

    const int32 shift          = shift_of(channel);
    constexpr uint64 nibbles   = 0x0F0F0F0F0F0F0F0FULL;

    auto around = gather_boundary(*this, y_base, channel);

    std::array<uint8, page_count> level{};
    std::array<uint8, page_count> mixed{};

    for (int32 py = 0; py < pages_side; ++py) {
        for (int32 ly = 0; ly < page_side; ++ly) {
            const int32 y = y_base + (py * page_side) + ly;

            for (int32 z = 0; z < s; ++z) {
                const uint8* row = row_(y, z);
                const int32 pz   = z / page_side;

                for (int32 px = 0; px < pages_side; ++px) {
                    uint64 run = 0;
                    std::memcpy(&run, row + (px * page_side), sizeof(run));

                    run = (run >> shift) & nibbles;

                    const auto first = static_cast<uint8>(run & 0xFFU);
                    const auto slot  = static_cast<std::size_t>(light_field::page_index(px, py, pz));

                    if (ly == 0 && (z % page_side) == 0) {
                        level[slot] = first;
                    }

                    const bool same =
                        run == (static_cast<uint64>(first) * 0x0101010101010101ULL) &&
                        first == level[slot];
                    mixed[slot] |= static_cast<uint8>(!same);
                }
            }
        }
    }

    const bool chunk_uniform = std::ranges::none_of(mixed, [](uint8 m) -> bool {
        return m != 0;
    }) && std::ranges::all_of(level, [&](uint8 l) -> bool { return l == level[0]; });

    if (chunk_uniform) {
        return light_field{level[0], std::move(around)};
    }

    std::vector<uint16> table(page_count);
    std::vector<light_field::page_type> pages;

    for (int32 pz = 0; pz < pages_side; ++pz) {
        for (int32 py = 0; py < pages_side; ++py) {
            for (int32 px = 0; px < pages_side; ++px) {
                const auto slot = static_cast<std::size_t>(light_field::page_index(px, py, pz));

                if (mixed[slot] == 0) {
                    table[slot] = static_cast<uint16>(level[slot] << 1);
                    continue;
                }

                light_field::page_type packed{};
                for (int32 lz = 0; lz < page_side; ++lz) {
                    for (int32 ly = 0; ly < page_side; ++ly) {
                        const uint8* row =
                            row_(y_base + (py * page_side) + ly, (pz * page_side) + lz) +
                            (px * page_side);

                        for (int32 lx = 0; lx < page_side; ++lx) {
                            const int32 at = lx + (ly * page_side) + (lz * page_side * page_side);
                            const auto nibble =
                                static_cast<uint8>((row[lx] >> shift) & 0x0FU);
                            packed[static_cast<std::size_t>(at / 2)] |= static_cast<uint8>(
                                (at % 2) == 0 ? nibble : (nibble << 4)
                            );
                        }
                    }
                }

                table[slot] = static_cast<uint16>(1U | (pages.size() << 1));
                pages.push_back(packed);
            }
        }
    }

    return light_field{std::move(table), std::move(pages), std::move(around)};
}

}  // namespace vw::ecs

namespace vw::ecs {

namespace {

constexpr int32 light_page     = asset::model::page_size;
constexpr int32 pages_per_side = ecs::light_column::side / light_page;
constexpr int32 skirt_pages = (ecs::light_column::apron + light_page - 1) / light_page;

static_assert(skirt_pages * light_page >= ecs::light_column::apron);

}  // namespace

light_baker::light_baker(
    const voxel_registry& voxel_types, uint32 workers
)
    : emission_{asset::build_emission_table(voxel_types)} {
    auto count = workers != 0 ? workers : std::min(std::thread::hardware_concurrency(), 4U);
    if (count == 0) {
        count = 1;
    }
    for (uint32 i = 0; i < count; ++i) {
        threads_.emplace_back(&light_baker::worker_, this);
    }
}

light_baker::~light_baker() {
    {
        std::scoped_lock lock(mutex_);
        running_ = false;
    }
    cv_.notify_all();

    for (auto& t : threads_) {
        if (t.joinable()) {
            t.join();
        }
    }
}

auto light_baker::request(
    light_request job
) -> bool {
    if (pending_.contains(job.coord)) {
        return false;
    }
    pending_.insert(job.coord);

    {
        std::scoped_lock lock(mutex_);
        queue_.push(std::move(job));
        queue_peak_ = std::max(queue_peak_, static_cast<uint32>(queue_.size()));
    }
    cv_.notify_one();
    return true;
}

auto light_baker::try_pop_completed() -> std::optional<light_result> {
    light_result result;
    {
        std::scoped_lock lock(completed_mutex_);
        if (completed_.empty()) {
            return std::nullopt;
        }
        result = std::move(completed_.front());
        completed_.pop();
    }
    pending_.erase(result.coord);
    return result;
}

auto light_baker::is_pending(
    vec2i coord
) const -> bool {
    return pending_.contains(coord);
}

auto light_baker::pending_count() const -> uint32 {
    return static_cast<uint32>(pending_.size());
}

auto light_baker::record_column_(
    uint64 rows_nanos, uint64 flood_nanos, uint64 bake_nanos
) -> void {
    std::scoped_lock lock(stats_mutex_);
    latency_.record(rows_nanos + flood_nanos + bake_nanos);
    rows_nanos_ += rows_nanos;
    flood_nanos_ += flood_nanos;
    bake_nanos_ += bake_nanos;
}

auto light_baker::get_stats() const -> light_stats {
    light_stats out{};

    {
        std::scoped_lock lock(stats_mutex_);
        const auto summary = latency_.summarize();

        out.columns  = summary.count;
        out.rows_ms  = static_cast<float32>(static_cast<float64>(rows_nanos_) / 1.0e6);
        out.flood_ms = static_cast<float32>(static_cast<float64>(flood_nanos_) / 1.0e6);
        out.bake_ms  = static_cast<float32>(static_cast<float64>(bake_nanos_) / 1.0e6);
        out.mean_us  = summary.mean_us;
        out.p50_us   = summary.p50_us;
        out.p99_us   = summary.p99_us;
        out.max_us   = summary.max_us;
    }

    {
        std::scoped_lock lock(mutex_);
        out.queue_depth = static_cast<uint32>(queue_.size());
        out.queue_peak  = queue_peak_;
    }

    return out;
}

auto light_baker::worker_() -> void {
    std::vector<std::vector<asset::chunk_occupancy>> held(9);
    std::vector<std::vector<const asset::chunk_occupancy*>> pointers(9);

    std::vector<std::vector<const asset::model*>> emitters(9);

    ecs::light_scratch scratch;

    while (true) {
        light_request job;

        {
            std::unique_lock lock(mutex_);
            cv_.wait(lock, [this] -> bool { return !queue_.empty() || !running_; });

            if (!running_ && queue_.empty()) {
                break;
            }
            if (queue_.empty()) {
                continue;
            }

            job = std::move(queue_.front());
            queue_.pop();
        }

        const auto started = std::chrono::steady_clock::now();

        ecs::light_column::neighbourhood around{};

        for (int32 dz = -1; dz <= 1; ++dz) {
            for (int32 dx = -1; dx <= 1; ++dx) {
                const auto slot  = static_cast<std::size_t>(((dz + 1) * 3) + (dx + 1));
                const auto& from = job.around[slot];

                pointers[slot].clear();
                emitters[slot].clear();

                // см. docs/lighting.md#небесный-свет
                const int32 px0 = dx < 0 ? pages_per_side - skirt_pages : 0;
                const int32 px1 = dx > 0 ? skirt_pages : pages_per_side;
                const int32 pz0 = dz < 0 ? pages_per_side - skirt_pages : 0;
                const int32 pz1 = dz > 0 ? skirt_pages : pages_per_side;

                held[slot].resize(from.size());

                for (std::size_t i = 0; i < from.size(); ++i) {
                    emitters[slot].push_back(from[i].get());

                    if (from[i] == nullptr) {
                        pointers[slot].push_back(nullptr);
                        continue;
                    }

                    static_cast<void>(from[i]->build_x_rows(held[slot][i], px0, px1, pz0, pz1));
                    pointers[slot].push_back(&held[slot][i]);
                }

                around[slot] = ecs::light_column::column_slice{
                    .occupancy = pointers[slot],
                    .models    = emitters[slot],
                };
            }
        }

        const auto rowed = std::chrono::steady_clock::now();

        ecs::light_column light{around, emission_, std::move(scratch)};

        const auto flooded = std::chrono::steady_clock::now();

        light_result result;
        result.coord    = job.coord;
        result.bottom_y = job.bottom_y;
        result.sky.reserve(job.around[4].size());
        result.block.reserve(job.around[4].size());

        for (std::size_t i = 0; i < job.around[4].size(); ++i) {
            const auto y_base = static_cast<int32>(i) * asset::light_field::side;
            result.sky.push_back(light.bake(y_base, asset::light_channel::sky));
            result.block.push_back(light.bake(y_base, asset::light_channel::block));
        }

        const auto baked = std::chrono::steady_clock::now();

        scratch = std::move(light).release();

        const auto span = [](auto from, auto to) -> uint64 {
            return static_cast<uint64>(
                std::chrono::duration_cast<std::chrono::nanoseconds>(to - from).count()
            );
        };

        record_column_(span(started, rowed), span(rowed, flooded), span(flooded, baked));

        {
            std::scoped_lock lock(completed_mutex_);
            completed_.push(std::move(result));
        }
    }
}

}  // namespace vw::ecs
