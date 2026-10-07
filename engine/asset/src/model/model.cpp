module vw.asset;

import std;
import vw.core;

namespace vw::asset {
namespace {
constexpr log::log_category lc_pool_{"page_pool"};
constexpr log::log_category lc_registry_{"model_registry"};

auto drop_owned(std::vector<uint32>& owned, uint32 slot) -> void {
    const auto it = std::ranges::find(owned, slot);
    if (it != owned.end()) {
        std::iter_swap(it, owned.end() - 1);
        owned.pop_back();
    }
}

auto set_page_bit(binary_page& page, int32 at, bool on) -> void {
    const auto byte = static_cast<std::size_t>(at / 8);
    const auto mask = static_cast<uint8>(1U << (at % 8));

    page[byte] = static_cast<uint8>(on ? (page[byte] | mask) : (page[byte] & ~mask));
}

[[nodiscard]] auto page_bit(const binary_page& page, int32 at) -> bool {
    return ((page[static_cast<std::size_t>(at / 8)] >> (at % 8)) & 1U) != 0;
}

struct page_summary {
    voxel solid{};
    bool air   = false;
    bool mixed = false;
};

[[nodiscard]] auto classify_page(const page_pool::page_type& page) -> page_summary {
    constexpr uint64 lanes = 0x0101010101010101ULL;

    page_summary out;

    for (std::size_t at = 0; at < page.size(); at += sizeof(uint64)) {
        uint64 word = 0;
        std::memcpy(&word, &page[at], sizeof(word));

        if (word == 0) {
            out.air = true;
            continue;
        }

        if (out.solid.is_empty()) {
            for (uint32 lane = 0; lane < sizeof(uint64); ++lane) {
                const auto value = static_cast<uint8>(word >> (lane * 8));
                if (value != 0) {
                    out.solid = voxel{value};
                    break;
                }
            }
        }

        if (word == lanes * out.solid.value) {
            continue;
        }

        for (uint32 lane = 0; lane < sizeof(uint64); ++lane) {
            const auto value = static_cast<uint8>(word >> (lane * 8));
            if (value == 0) {
                out.air = true;
            } else if (value != out.solid.value) {
                out.mixed = true;
                return out;
            }
        }
    }

    return out;
}

[[nodiscard]] auto pack_palette(
    const page_pool::page_type& page
) -> std::optional<palette_page> {
    std::array<uint8, voxel_type_capacity> slot_of{};
    palette_page packed{};
    uint32 count = 1;

    for (const voxel v : page) {
        if (v.is_empty() || slot_of[v.value] != 0) {
            continue;
        }
        if (count == palette_page_slots) {
            return std::nullopt;
        }
        slot_of[v.value]      = static_cast<uint8>(count);
        packed.palette[count] = v;
        ++count;
    }

    packed.nibbles.fill(0);
    for (int32 local = 0; local < voxel_page_volume; ++local) {
        packed.set_slot(local, slot_of[page[static_cast<std::size_t>(local)].value]);
    }

    return packed;
}

}  // namespace

namespace detail {

[[noreturn]] auto report_page_store_exhausted(
    uint32 requested, uint32 limit
) -> void {
    log::critical(
        lc_pool_, "page pool exhausted: {} pages requested, {} is the addressable limit",
        requested, limit
    );
    std::terminate();
}

}  // namespace detail

model_identity_pool::model_identity_pool(std::size_t capacity) {
    generations_.reserve(capacity);
}

auto model_identity_pool::create() -> model_identity {
    std::scoped_lock lock(mutex_);
    if (!free_indices_.empty()) [[unlikely]] {
        const uint32 index = free_indices_.back();
        free_indices_.pop_back();
        return {.index = index, .generation = generations_[index]};
    }

    const auto index = static_cast<uint32>(generations_.size());
    generations_.push_back(0);
    return {.index = index, .generation = 0};
}

auto model_identity_pool::next_generation(model_identity id) -> model_identity {
    std::scoped_lock lock(mutex_);
    if (has_unlocked_(id)) [[likely]] {
        return {.index = id.index, .generation = ++generations_[id.index]};
    }
    return invalid_model_identity;
}

auto model_identity_pool::has(model_identity id) const -> bool {
    std::scoped_lock lock(mutex_);
    return has_unlocked_(id);
}

auto model_identity_pool::destroy(model_identity id) -> void {
    std::scoped_lock lock(mutex_);
    if (has_unlocked_(id)) [[likely]] {
        ++generations_[id.index];
        free_indices_.push_back(id.index);
    }
}

model::model(model_identity_pool& identity_pool, page_pool& pool, int32 width, int32 height,
             int32 depth, int32 world_units_per_voxel)
    : identity_pool_(&identity_pool)
    , pool_ptr_(&pool)
    , width_(width)
    , height_(height)
    , depth_(depth)
    , world_units_per_voxel_(world_units_per_voxel)
    , pages_x_((width + page_size - 1) / page_size)
    , pages_y_((height + page_size - 1) / page_size)
    , pages_z_((depth + page_size - 1) / page_size) {
    pages_.resize(static_cast<std::size_t>(pages_x_) * static_cast<std::size_t>(pages_y_) *
                  static_cast<std::size_t>(pages_z_));
    identity_ = identity_pool_->create();
}

model::~model() {
    release_all_pages_();
    if (identity_pool_ != nullptr) {
        identity_pool_->destroy(identity_);
    }
}

model::model(model&& other) noexcept
    : identity_pool_(other.identity_pool_)
    , pool_ptr_(other.pool_ptr_)
    , width_(other.width_)
    , height_(other.height_)
    , depth_(other.depth_)
    , world_units_per_voxel_(other.world_units_per_voxel_)
    , pivot_(other.pivot_)
    , pages_x_(other.pages_x_)
    , pages_y_(other.pages_y_)
    , pages_z_(other.pages_z_)
    , pages_(std::move(other.pages_))
    , owned_dense_(std::move(other.owned_dense_))
    , owned_binary_(std::move(other.owned_binary_))
    , owned_palette_(std::move(other.owned_palette_))
    , identity_(other.identity_)
    , fill_(other.fill_)
    , fill_known_(other.fill_known_) {
    other.identity_pool_ = nullptr;
    other.pool_ptr_      = nullptr;
}

auto model::operator=(model&& other) noexcept -> model& {
    if (this != &other) {
        release_all_pages_();
        if (identity_pool_ != nullptr) {
            identity_pool_->destroy(identity_);
        }
        identity_pool_       = other.identity_pool_;
        pool_ptr_            = other.pool_ptr_;
        width_               = other.width_;
        height_              = other.height_;
        depth_               = other.depth_;
        world_units_per_voxel_         = other.world_units_per_voxel_;
        pivot_               = other.pivot_;
        pages_x_             = other.pages_x_;
        pages_y_             = other.pages_y_;
        pages_z_             = other.pages_z_;
        pages_               = std::move(other.pages_);
        owned_dense_         = std::move(other.owned_dense_);
        owned_binary_        = std::move(other.owned_binary_);
        owned_palette_       = std::move(other.owned_palette_);
        identity_            = other.identity_;
        fill_                = other.fill_;
        fill_known_          = other.fill_known_;
        other.identity_pool_ = nullptr;
        other.pool_ptr_      = nullptr;
    }
    return *this;
}

auto model::set_voxel(int32 x, int32 y, int32 z, voxel v) -> void {
    set_voxel_raw_(x, y, z, v);
    increment_generation_();
}

auto model::set_voxel_raw_(int32 x, int32 y, int32 z, voxel v) -> void {
    const voxel index = v;

    fill_known_    = false;
    const int32 px = x / page_size;
    const int32 py = y / page_size;
    const int32 pz = z / page_size;
    const int32 li = voxel_page_local_index(x % page_size, y % page_size, z % page_size);
    auto& entry    = pages_[page_index(px, py, pz)];

    switch (entry.mode()) {
        case page_mode::empty:
            if (index.is_empty()) {
                return;
            }
            promote_to_dense(px, py, pz)[li] = index;
            break;
        case page_mode::uniform:
            if (index == entry.fill_voxel()) {
                return;
            }
            if (index.is_empty()) {
                set_page_bit(make_binary_(entry, entry.fill_voxel(), true), li, false);
                return;
            }
            promote_to_dense(px, py, pz)[li] = index;
            break;
        case page_mode::binary:
            if (index.is_empty() || index == entry.fill_voxel()) {
                set_page_bit(
                    pool_ptr_->get_binary(entry.binary_slot()), li, !index.is_empty()
                );
                return;
            }
            promote_to_dense(px, py, pz)[li] = index;
            break;
        case page_mode::palette:
            promote_to_dense(px, py, pz)[li] = index;
            break;
        case page_mode::dense:
            pool_ptr_->get_dense(entry.dense_slot())[li] = index;
            break;
    }
}

auto model::build_occupancy(chunk_occupancy& out) const -> bool {
    constexpr int32 ps   = page_size;
    constexpr int32 side = chunk_occupancy::side;

    if (width_ != side || height_ != side || depth_ != side) {
        return false;
    }

    out.clear();

    for (int32 px = 0; px < pages_x_; ++px) {
        const int32 x0 = px * ps;

        for (int32 py = 0; py < pages_y_; ++py) {
            for (int32 pz = 0; pz < pages_z_; ++pz) {
                const auto mode = get_page_mode(px, py, pz);
                if (mode == page_mode::empty) {
                    continue;
                }

                const int32 y0 = py * ps;
                const int32 z0 = pz * ps;

                if (mode == page_mode::uniform) {
                    const uint64 xspan = uint64{0xFF} << x0;
                    const uint64 zspan = uint64{0xFF} << z0;
                    for (int32 ly = 0; ly < ps; ++ly) {
                        for (int32 l = 0; l < ps; ++l) {
                            out.set_row(y0 + ly, z0 + l, xspan);
                            out.set_zrow(y0 + ly, x0 + l, zspan);
                        }
                    }
                    continue;
                }

                const auto page = get_page(px, py, pz);
                for (int32 ly = 0; ly < ps; ++ly) {
                    for (int32 lz = 0; lz < ps; ++lz) {
                        const uint64 bits = page.row_bits(ly, lz);
                        if (bits == 0) {
                            continue;
                        }

                        out.set_row(y0 + ly, z0 + lz, bits << x0);
                        for (int32 lx = 0; lx < ps; ++lx) {
                            if (((bits >> lx) & 1U) != 0) {
                                out.set_zrow(y0 + ly, x0 + lx, uint64{1} << (z0 + lz));
                            }
                        }
                    }
                }
            }
        }
    }

    return true;
}

auto model::build_rows_page_by_page(chunk_occupancy& out) const -> bool {
    constexpr int32 ps   = page_size;
    constexpr int32 side = chunk_occupancy::side;

    if (width_ != side || height_ != side || depth_ != side) {
        return false;
    }

    out.rows.fill(0);

    for (int32 px = 0; px < pages_x_; ++px) {
        const int32 x0 = px * ps;

        for (int32 py = 0; py < pages_y_; ++py) {
            const int32 y0 = py * ps;

            for (int32 pz = 0; pz < pages_z_; ++pz) {
                const auto mode = get_page_mode(px, py, pz);
                if (mode == page_mode::empty) {
                    continue;
                }

                const int32 z0 = pz * ps;

                if (mode == page_mode::uniform) {
                    const uint64 span = uint64{0xFF} << x0;
                    for (int32 ly = 0; ly < ps; ++ly) {
                        for (int32 lz = 0; lz < ps; ++lz) {
                            out.rows[((y0 + ly) * side) + z0 + lz] |= span;
                        }
                    }
                    continue;
                }

                const auto page = get_page(px, py, pz);
                for (int32 ly = 0; ly < ps; ++ly) {
                    for (int32 lz = 0; lz < ps; ++lz) {
                        out.rows[((y0 + ly) * side) + z0 + lz] |=
                            uint64{page.row_bits(ly, lz)} << x0;
                    }
                }
            }
        }
    }

    return true;
}

auto model::build_bit_rows(std::span<uint32> out) const -> void {
    constexpr int32 ps = page_size;

    const uint32 row_words = bit_row_words(width_);
    const auto needed      = static_cast<std::size_t>(row_words) *
                             static_cast<std::size_t>(height_) * static_cast<std::size_t>(depth_);
    if (out.size() != needed) {
        throw std::invalid_argument("bit rows need a span of exactly the model volume");
    }

    std::ranges::fill(out, uint32{0});

    const uint32 tail_bits = static_cast<uint32>(width_) & 31U;
    const uint32 tail_mask = tail_bits == 0 ? ~uint32{0} : (uint32{1} << tail_bits) - 1;

    for (int32 pz = 0; pz < pages_z_; ++pz) {
        for (int32 py = 0; py < pages_y_; ++py) {
            for (int32 px = 0; px < pages_x_; ++px) {
                const auto mode = get_page_mode(px, py, pz);
                if (mode == page_mode::empty) {
                    continue;
                }

                const bool whole  = mode == page_mode::uniform;
                const auto page   = whole ? page_view{} : get_page(px, py, pz);
                const auto word   = static_cast<std::size_t>((px * ps) >> 5);
                const uint32 from = static_cast<uint32>(px * ps) & 31U;

                for (int32 lz = 0; lz < ps; ++lz) {
                    const int32 z = (pz * ps) + lz;
                    if (z >= depth_) {
                        break;
                    }
                    for (int32 ly = 0; ly < ps; ++ly) {
                        const int32 y = (py * ps) + ly;
                        if (y >= height_) {
                            break;
                        }

                        const uint32 bits = whole ? 0xFFU : uint32{page.row_bits(ly, lz)};
                        const auto row    = static_cast<std::size_t>(y + (height_ * z)) *
                                            row_words;
                        out[row + word] |= bits << from;
                    }
                }
            }
        }
    }

    for (std::size_t row = 0; row < needed; row += row_words) {
        out[row + row_words - 1] &= tail_mask;
    }
}

auto model::build_x_rows(
    chunk_occupancy& out, int32 px0, int32 px1, int32 pz0, int32 pz1
) const -> bool {
    constexpr int32 ps   = page_size;
    constexpr int32 side = chunk_occupancy::side;

    static_assert(sizeof(voxel) == 1);

    if (width_ != side || height_ != side || depth_ != side) {
        return false;
    }

    for (int32 py = 0; py < pages_y_; ++py) {
        for (int32 pz = pz0; pz < pz1; ++pz) {
            for (int32 ly = 0; ly < ps; ++ly) {
                const int32 y = (py * ps) + ly;

                for (int32 lz = 0; lz < ps; ++lz) {
                    const int32 z = (pz * ps) + lz;
                    uint64 bits   = 0;

                    for (int32 px = px0; px < px1; ++px) {
                        const auto& entry = pages_[page_index(px, py, pz)];

                        if (entry.mode() == page_mode::empty) {
                            continue;
                        }
                        if (entry.mode() == page_mode::uniform) {
                            bits |= uint64{0xFF} << (px * ps);
                            continue;
                        }

                        bits |= uint64{view_of(entry).row_bits(ly, lz)} << (px * ps);
                    }

                    out.rows[(y * side) + z] = bits;
                }
            }
        }
    }

    return true;
}

auto build_emission_table(
    const voxel_registry& registry
) -> emission_table {
    emission_table table{};

    for (const voxel_type& type : registry.all()) {
        table[type.id.value] = type.material.emission;
    }

    return table;
}

namespace {

auto build_cell_links(
    const chunk_occupancy& occupancy, vec3i origin, int32 size, chunk_link_scratch& scratch
) -> cell_links {
    const int32 rows           = size * size;
    constexpr int32 face_span  = chunk_pocket::face_span;
    const int32 face_block     = std::max(1, size / face_span);

    auto& masks     = scratch.masks;
    auto& row_begin = scratch.row_begin;
    auto& seen      = scratch.seen;
    auto& stack     = scratch.stack;

    masks.clear();
    row_begin.assign(rows + 1, 0);

    const uint64 span_mask = (size == 64) ? ~uint64{0} : ((uint64{1} << size) - 1);

    for (int32 y = 0; y < size; ++y) {
        for (int32 z = 0; z < size; ++z) {
            const int32 row = (y * size) + z;
            row_begin[row]  = static_cast<int32>(masks.size());

            uint64 free_bits =
                (~occupancy.row(origin.y + y, origin.z + z) >> origin.x) & span_mask;

            while (free_bits != 0) {
                const int32 start = std::countr_zero(free_bits);
                const uint64 tail = free_bits >> start;
                const int32 len   = std::countr_one(tail);

                const uint64 run = (len == 64)
                    ? ~uint64{0}
                    : (((uint64{1} << len) - 1) << start);

                masks.push_back(run);
                free_bits &= ~run;
            }
        }
    }
    row_begin[rows] = static_cast<int32>(masks.size());

    seen.assign(masks.size(), 0);
    stack.clear();

    cell_links links;

    const int32 volume_block = std::max(1, size / chunk_pocket::volume_span);

    const auto add_volume = [&](chunk_pocket& pocket, int32 y, int32 z, uint64 run) {
        uint64 bits = run;
        while (bits != 0) {
            const int32 x = std::countr_zero(bits);
            pocket.volume |= chunk_pocket::volume_bit(x, y, z, volume_block);

            const int32 next = ((x / volume_block) + 1) * volume_block;
            if (next >= 64) {
                break;
            }
            bits &= ~((uint64{1} << next) - 1);
        }
    };

    const auto add_faces = [&](chunk_pocket& pocket, int32 y, int32 z, uint64 run) {
        const auto block_bit = [face_block](int32 a, int32 b) -> uint64 {
            return uint64{1} << (((b / face_block) * face_span) + (a / face_block));
        };

        if ((run & 1U) != 0) {
            pocket.faces[face_direction::neg_x] |= block_bit(y, z);
        }
        if (((run >> (size - 1)) & 1U) != 0) {
            pocket.faces[face_direction::pos_x] |= block_bit(y, z);
        }

        const bool on_y = (y == 0) || (y == size - 1);
        const bool on_z = (z == 0) || (z == size - 1);
        if (!on_y && !on_z) {
            return;
        }

        for (int32 i = 0; i < face_span; ++i) {
            const uint64 chunk_of_run = (run >> (i * face_block)) &
                ((uint64{1} << face_block) - 1);
            if (chunk_of_run == 0) {
                continue;
            }
            if (y == 0) {
                pocket.faces[face_direction::neg_y] |=
                    uint64{1} << (((z / face_block) * face_span) + i);
            }
            if (y == size - 1) {
                pocket.faces[face_direction::pos_y] |=
                    uint64{1} << (((z / face_block) * face_span) + i);
            }
            if (z == 0) {
                pocket.faces[face_direction::neg_z] |=
                    uint64{1} << (((y / face_block) * face_span) + i);
            }
            if (z == size - 1) {
                pocket.faces[face_direction::pos_z] |=
                    uint64{1} << (((y / face_block) * face_span) + i);
            }
        }
    };

    for (int32 row = 0; row < rows; ++row) {
        for (int32 r = row_begin[row]; r < row_begin[row + 1]; ++r) {
            if (seen[r] != 0) {
                continue;
            }

            chunk_pocket pocket;
            seen[r] = 1;
            stack.push_back(r);
            stack.push_back(row);

            while (!stack.empty()) {
                const int32 at_row = stack.back();
                stack.pop_back();
                const int32 at = stack.back();
                stack.pop_back();

                const int32 ay   = at_row / size;
                const int32 az   = at_row % size;
                const uint64 run = masks[at];

                add_faces(pocket, ay, az, run);
                add_volume(pocket, ay, az, run);

                const auto visit = [&](int32 ny, int32 nz) {
                    if (ny < 0 || nz < 0 || ny >= size || nz >= size) {
                        return;
                    }
                    const int32 neighbour_row = (ny * size) + nz;
                    for (int32 n = row_begin[neighbour_row]; n < row_begin[neighbour_row + 1];
                         ++n) {
                        if (seen[n] != 0 || (masks[n] & run) == 0) {
                            continue;
                        }
                        seen[n] = 1;
                        stack.push_back(n);
                        stack.push_back(neighbour_row);
                    }
                };

                visit(ay - 1, az);
                visit(ay + 1, az);
                visit(ay, az - 1);
                visit(ay, az + 1);
            }

            const bool reaches_a_face = std::ranges::any_of(
                pocket.faces, [](uint64 blocks) -> bool { return blocks != 0; }
            );
            if (reaches_a_face) {
                links.pockets.push_back(pocket);
            }
        }
    }

    if (links.pockets.size() > cell_links::max_pockets) {
        links.merged = true;
        chunk_pocket merged;
        for (const auto& pocket : links.pockets) {
            merged.volume |= pocket.volume;
            for (const face_direction face : all_face_directions) {
                merged.faces[face] |= pocket.faces[face];
            }
        }
        links.pockets.assign(1, merged);
    }

    return links;
}

}  // namespace

auto build_chunk_links(
    const chunk_occupancy& occupancy, chunk_link_scratch& scratch
) -> chunk_links {
    constexpr int32 size = chunk_links::cell_size;
    constexpr int32 per  = chunk_links::cells_per_side;

    chunk_links links;

    for (int32 x = 0; x < per; ++x) {
        for (int32 y = 0; y < per; ++y) {
            for (int32 z = 0; z < per; ++z) {
                links.cells[chunk_links::cell_index(x, y, z)] = build_cell_links(
                    occupancy, vec3i{x * size, y * size, z * size}, size, scratch
                );
            }
        }
    }

    return links;
}

auto build_chunk_links(
    const chunk_occupancy& occupancy
) -> chunk_links {
    chunk_link_scratch scratch;
    return build_chunk_links(occupancy, scratch);
}

auto model::compact_pages() -> uint32 {
    fill_known_      = false;
    uint32 compacted = 0;

    for (auto& entry : pages_) {
        const page_entry was = entry;

        if (was.mode() == page_mode::binary) {
            const auto& bits = pool_ptr_->get_binary(was.binary_slot());

            const bool none = std::ranges::all_of(bits, [](uint8 b) -> bool { return b == 0; });
            const bool all =
                std::ranges::all_of(bits, [](uint8 b) -> bool { return b == 0xFF; });

            if (!none && !all) {
                continue;
            }

            entry = none ? page_entry::make_empty()
                         : page_entry::make_uniform(was.fill_voxel());
            pool_ptr_->free_binary(was.binary_slot());
            drop_owned(owned_binary_, was.binary_slot());
            ++compacted;
            continue;
        }

        if (was.mode() != page_mode::dense) {
            continue;
        }

        const auto& page = pool_ptr_->get_dense(was.dense_slot());

        const auto [solid, air, mixed] = classify_page(page);

        if (mixed) {
            const std::optional<palette_page> packed = pack_palette(page);
            if (!packed.has_value()) {
                continue;
            }

            const uint32 slot = pool_ptr_->alloc_palette();
            owned_palette_.push_back(slot);
            pool_ptr_->get_palette(slot) = *packed;

            entry = page_entry::make_palette(slot);

            pool_ptr_->free_dense(was.dense_slot());
            drop_owned(owned_dense_, was.dense_slot());
            ++compacted;
            continue;
        }

        if (solid.is_empty()) {
            entry = page_entry::make_empty();
        } else if (!air) {
            entry = page_entry::make_uniform(solid);
        } else {
            const uint32 slot = pool_ptr_->alloc_binary();
            owned_binary_.push_back(slot);

            auto& bits = pool_ptr_->get_binary(slot);
            const page_view dense{page};

            for (int32 lz = 0; lz < page_size; ++lz) {
                for (int32 ly = 0; ly < page_size; ++ly) {
                    bits[static_cast<std::size_t>(voxel_page_row_index(ly, lz))] =
                        static_cast<uint8>(dense.row_bits(ly, lz));
                }
            }

            entry = page_entry::make_binary(solid, slot);
        }

        pool_ptr_->free_dense(was.dense_slot());
        drop_owned(owned_dense_, was.dense_slot());
        ++compacted;
    }

    return compacted;
}

auto model::scan_fill() const -> model_fill {
    if (fill_known_) {
        return fill_;
    }

    bool any_empty = false;
    bool any_solid = false;
    fill_          = model_fill::mixed;
    fill_known_    = true;

    for (const auto& entry : pages_) {
        switch (entry.mode()) {
            case page_mode::empty:
                any_empty = true;
                break;
            case page_mode::uniform:
                any_solid = true;
                break;
            default:
                return fill_;
        }

        if (any_empty && any_solid) {
            return fill_;
        }
    }

    fill_ = any_solid ? model_fill::solid : model_fill::air;
    return fill_;
}

auto model::extract_face(face_direction face, face_occupancy& out) const -> bool {
    constexpr int32 side  = face_occupancy::side;
    constexpr int32 ps    = page_size;
    constexpr int32 pages = side / ps;

    if (width_ != side || height_ != side || depth_ != side) {
        return false;
    }

    out.clear();

    const int32 layer = boundary_layer(face, side);
    const int32 pl    = layer / ps;
    const int32 ll    = layer % ps;

    for (int32 pb = 0; pb < pages; ++pb) {
        for (int32 pa = 0; pa < pages; ++pa) {
            const auto page = lift_off_face_plane(face, vec2i{pa, pb}, pl);
            const auto mode = get_page_mode(page.x, page.y, page.z);

            if (mode == page_mode::empty) {
                continue;
            }

            if (mode == page_mode::uniform) {
                const uint64 bits = uint64{0xFF} << (pa * ps);
                for (int32 b = 0; b < ps; ++b) {
                    out.rows[(pb * ps) + b] |= bits;
                }
                continue;
            }

            const auto data = get_page(page.x, page.y, page.z);

            for (int32 b = 0; b < ps; ++b) {
                uint64 bits = 0;
                for (int32 a = 0; a < ps; ++a) {
                    const auto cell = lift_off_face_plane(face, vec2i{a, b}, ll);
                    if (!data.voxel_at(cell.x, cell.y, cell.z).is_empty()) {
                        bits |= uint64{1} << a;
                    }
                }
                out.rows[(pb * ps) + b] |= bits << (pa * ps);
            }
        }
    }

    return true;
}

auto model::page_may_hold(
    int32 px, int32 py, int32 pz, const voxel_set& wanted
) const -> bool {
    const page_entry& entry = pages_[page_index(px, py, pz)];
    switch (entry.mode()) {
        case page_mode::empty:
            return false;
        case page_mode::uniform:
        case page_mode::binary:
            return wanted.test(entry.fill_voxel().value);
        case page_mode::palette:
            return std::ranges::any_of(pool_ptr_->get_palette(entry.palette_slot()).palette, [&](voxel v) {
                return !v.is_empty() && wanted.test(v.value);
            });
        default:
            return true;
    }
}

auto model::build_rows_of(
    chunk_occupancy& out, const voxel_set& wanted
) const -> bool {
    constexpr int32 ps   = page_size;
    constexpr int32 side = chunk_occupancy::side;

    out.rows.fill(0);
    if (width_ != side || height_ != side || depth_ != side) {
        return false;
    }

    bool any = false;
    for (int32 pz = 0; pz < pages_z_; ++pz) {
        for (int32 py = 0; py < pages_y_; ++py) {
            for (int32 px = 0; px < pages_x_; ++px) {
                if (!page_may_hold(px, py, pz, wanted)) {
                    continue;
                }
                any            = true;
                const int32 x0 = px * ps;
                const int32 y0 = py * ps;
                const int32 z0 = pz * ps;

                if (get_page_mode(px, py, pz) == page_mode::uniform) {
                    for (int32 ly = 0; ly < ps; ++ly) {
                        for (int32 lz = 0; lz < ps; ++lz) {
                            out.set_row(y0 + ly, z0 + lz, uint64{0xFF} << x0);
                        }
                    }
                    continue;
                }

                const auto page = get_page(px, py, pz);
                for (int32 ly = 0; ly < ps; ++ly) {
                    for (int32 lz = 0; lz < ps; ++lz) {
                        uint64 bits = 0;
                        for (int32 lx = 0; lx < ps; ++lx) {
                            const voxel v = page.voxel_at(lx, ly, lz);
                            if (!v.is_empty() && wanted.test(v.value)) {
                                bits |= uint64{1} << lx;
                            }
                        }
                        if (bits != 0) {
                            out.set_row(y0 + ly, z0 + lz, bits << x0);
                        }
                    }
                }
            }
        }
    }
    return any;
}

auto model::extract_face(
    face_direction face, face_occupancy& out, const voxel_set& wanted, face_occupancy& wanted_out
) const -> bool {
    constexpr int32 side  = face_occupancy::side;
    constexpr int32 ps    = page_size;
    constexpr int32 pages = side / ps;

    out.clear();
    wanted_out.clear();
    if (width_ != side || height_ != side || depth_ != side) {
        return false;
    }

    const int32 layer = boundary_layer(face, side);
    const int32 pl    = layer / ps;
    const int32 ll    = layer % ps;

    for (int32 pb = 0; pb < pages; ++pb) {
        for (int32 pa = 0; pa < pages; ++pa) {
            const auto page       = lift_off_face_plane(face, vec2i{pa, pb}, pl);
            const page_entry& entry = pages_[page_index(page.x, page.y, page.z)];
            const auto mode       = entry.mode();

            if (mode == page_mode::empty) {
                continue;
            }

            if (mode == page_mode::uniform) {
                const uint64 bits = uint64{0xFF} << (pa * ps);
                const bool held   = wanted.test(entry.fill_voxel().value);
                for (int32 b = 0; b < ps; ++b) {
                    out.rows[(pb * ps) + b] |= bits;
                    if (held) {
                        wanted_out.rows[(pb * ps) + b] |= bits;
                    }
                }
                continue;
            }

            const auto data = view_of(entry);

            for (int32 b = 0; b < ps; ++b) {
                uint64 bits      = 0;
                uint64 held_bits = 0;
                for (int32 a = 0; a < ps; ++a) {
                    const auto cell = lift_off_face_plane(face, vec2i{a, b}, ll);
                    const voxel v   = data.voxel_at(cell.x, cell.y, cell.z);
                    if (!v.is_empty()) {
                        bits |= uint64{1} << a;
                        if (wanted.test(v.value)) {
                            held_bits |= uint64{1} << a;
                        }
                    }
                }
                out.rows[(pb * ps) + b] |= bits << (pa * ps);
                wanted_out.rows[(pb * ps) + b] |= held_bits << (pa * ps);
            }
        }
    }

    return true;
}

auto model::invalidate() -> void {
    increment_generation_();
}

auto model::fill(voxel v) -> void {
    release_all_pages_();

    if (v.is_empty()) {
        std::ranges::fill(pages_, page_entry::make_empty());
        fill_ = model_fill::air;
    } else {
        std::ranges::fill(pages_, page_entry::make_uniform(v));
        fill_ = model_fill::solid;
    }
    fill_known_ = true;
    increment_generation_();
}

auto model::fill_page_raw_(int32 px, int32 py, int32 pz, voxel v) -> void {
    fill_known_ = false;
    auto& entry = pages_[page_index(px, py, pz)];

    release_page_(entry);

    entry = v.is_empty() ? page_entry::make_empty() : page_entry::make_uniform(v);
}

auto model::clone_pages_from(const model& source) -> void {
    fill_known_ = false;
    release_all_pages_();

    pages_ = source.pages_;

    for (auto& entry : pages_) {
        switch (entry.mode()) {
            case page_mode::dense: {
                const uint32 slot = pool_ptr_->alloc_dense();
                owned_dense_.push_back(slot);
                pool_ptr_->get_dense(slot) = source.pool_ptr_->get_dense(entry.dense_slot());
                entry                      = page_entry::make_dense(slot);
                break;
            }
            case page_mode::binary: {
                const uint32 slot = pool_ptr_->alloc_binary();
                owned_binary_.push_back(slot);
                pool_ptr_->get_binary(slot) =
                    source.pool_ptr_->get_binary(entry.binary_slot());
                entry = page_entry::make_binary(entry.fill_voxel(), slot);
                break;
            }
            case page_mode::palette: {
                const uint32 slot = pool_ptr_->alloc_palette();
                owned_palette_.push_back(slot);
                pool_ptr_->get_palette(slot) =
                    source.pool_ptr_->get_palette(entry.palette_slot());
                entry = page_entry::make_palette(slot);
                break;
            }
            default:
                break;
        }
    }

    increment_generation_();
}

auto model::release_page_(page_entry entry) -> void {
    switch (entry.mode()) {
        case page_mode::dense:
            pool_ptr_->free_dense(entry.dense_slot());
            drop_owned(owned_dense_, entry.dense_slot());
            break;
        case page_mode::binary:
            pool_ptr_->free_binary(entry.binary_slot());
            drop_owned(owned_binary_, entry.binary_slot());
            break;
        case page_mode::palette:
            pool_ptr_->free_palette(entry.palette_slot());
            drop_owned(owned_palette_, entry.palette_slot());
            break;
        default:
            break;
    }
}

auto model::release_all_pages_() -> void {
    if (pool_ptr_ == nullptr) {
        return;
    }

    if (!owned_dense_.empty()) {
        pool_ptr_->free_dense_batch(owned_dense_);
        owned_dense_.clear();
    }
    if (!owned_binary_.empty()) {
        pool_ptr_->free_binary_batch(owned_binary_);
        owned_binary_.clear();
    }
    if (!owned_palette_.empty()) {
        pool_ptr_->free_palette_batch(owned_palette_);
        owned_palette_.clear();
    }
}

auto model::make_binary_(page_entry& entry, voxel fill, bool solid) -> binary_page& {
    release_page_(entry);

    const uint32 slot = pool_ptr_->alloc_binary();
    owned_binary_.push_back(slot);

    auto& page = pool_ptr_->get_binary(slot);
    page.fill(solid ? uint8{0xFF} : uint8{0});

    entry = page_entry::make_binary(fill, slot);
    return page;
}

auto model::promote_to_dense(int32 px, int32 py, int32 pz) -> page_type& {
    auto& entry          = pages_[page_index(px, py, pz)];
    const page_entry was = entry;

    const uint32 slot = pool_ptr_->alloc_dense();
    owned_dense_.push_back(slot);

    auto& page = pool_ptr_->get_dense(slot);

    switch (was.mode()) {
        case page_mode::uniform:
            page.fill(was.fill_voxel());
            break;
        case page_mode::binary: {
            const auto& bits = pool_ptr_->get_binary(was.binary_slot());
            for (int32 at = 0; at < page_volume; ++at) {
                page[static_cast<std::size_t>(at)] =
                    page_bit(bits, at) ? was.fill_voxel() : voxel{};
            }
            break;
        }
        case page_mode::palette: {
            const auto& packed = pool_ptr_->get_palette(was.palette_slot());
            for (int32 at = 0; at < page_volume; ++at) {
                page[static_cast<std::size_t>(at)] = packed.voxel_at(at);
            }
            break;
        }
        default:
            page.fill(voxel{});
            break;
    }

    release_page_(was);
    entry = page_entry::make_dense(slot);
    return page;
}

auto model::increment_generation_() -> void {
    identity_ = identity_pool_->next_generation(identity_);
}

auto model_registry::has(std::string_view name) const -> bool {
    return models_.contains(name);
}

auto model_registry::get(std::string_view name) const -> std::shared_ptr<model> {
    const auto iter = models_.find(name);
    return iter != models_.end() ? iter->second : nullptr;
}

auto model_registry::create(std::string_view name, int32 width,
                            int32 height, int32 depth) -> std::shared_ptr<model> {
    auto new_model =
        std::make_shared<model>(identity_pool_, page_pool_, width, height, depth);

    const auto [it, inserted] = models_.try_emplace(std::string(name), new_model);
    if (!inserted) {
        log::warn(lc_registry_, "model '{}' is already registered, the entry is replaced", name);
        it->second = new_model;
    }

    return new_model;
}

auto model_registry::create(std::string_view name, vec3i size)
    -> std::shared_ptr<model> {
    return create(name, size.x, size.y, size.z);
}

auto model_registry::create_unnamed(int32 width, int32 height,
                                    int32 depth) -> std::shared_ptr<model> {
    return std::make_shared<model>(identity_pool_, page_pool_, width, height, depth);
}

auto model_registry::create_unnamed(vec3i size)
    -> std::shared_ptr<model> {
    return create_unnamed(size.x, size.y, size.z);
}

auto model_registry::create_clone(std::string_view name) -> std::shared_ptr<model> {
    const auto original = get(name);
    if (!original) {
        return nullptr;
    }

    auto cloned_model = std::make_shared<model>(
        identity_pool_, page_pool_, original->width(), original->height(),
        original->depth());
    cloned_model->clone_pages_from(*original);

    return cloned_model;
}

auto model_registry::erase(std::string_view name) -> void {
    models_.erase(std::string(name));
}

}  // namespace vw::asset
