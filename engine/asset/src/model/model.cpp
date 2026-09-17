module vw.asset;

import std;
import vw.core;

namespace vw::asset {
namespace {
constexpr log::log_category lc_pool_{"page_pool"};
constexpr log::log_category lc_registry_{"model_registry"};

}  // namespace

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

page_pool::page_pool() {
    blocks_.reserve(max_blocks);
}

auto page_pool::alloc() -> uint32 {
    std::scoped_lock lock(mutex_);
    if (!free_indices_.empty()) {
        const uint32 idx = free_indices_.back();
        free_indices_.pop_back();
        return idx;
    }
    const uint32 idx = next_index_++;
    ensure_capacity_(idx);
    return idx;
}

auto page_pool::free(uint32 index) -> void {
    std::scoped_lock lock(mutex_);
    free_indices_.push_back(index);
}

auto page_pool::alloc_batch(uint32 count) -> std::vector<uint32> {
    std::scoped_lock lock(mutex_);
    std::vector<uint32> result;
    result.reserve(count);

    const uint32 consecutive =
        count - static_cast<uint32>(std::min(static_cast<std::size_t>(count), free_indices_.size()));

    const uint32 bump_start = next_index_;
    for (uint32 i = 0; i < consecutive; ++i) {
        result.push_back(bump_start + i);
    }
    next_index_ = bump_start + consecutive;
    if (consecutive > 0) {
        ensure_capacity_(next_index_ - 1);
    }

    const uint32 remaining = count - consecutive;
    for (uint32 i = 0; i < remaining; ++i) {
        const uint32 idx = free_indices_.back();
        free_indices_.pop_back();
        result.push_back(idx);
    }

    return result;
}

auto page_pool::free_batch(std::span<const uint32> indices) -> void {
    std::scoped_lock lock(mutex_);
    free_indices_.reserve(free_indices_.size() + indices.size());
    for (uint32 idx : indices) {
        free_indices_.push_back(idx);
    }
}

auto page_pool::allocated_count() const -> uint32 {
    std::scoped_lock lock(mutex_);
    return next_index_ - static_cast<uint32>(free_indices_.size());
}

auto page_pool::free_count() const -> uint32 {
    std::scoped_lock lock(mutex_);
    return static_cast<uint32>(free_indices_.size());
}

auto page_pool::ensure_capacity_(uint32 index) -> void {
    if (index >= block_size * max_blocks) {
        log::critical(
            lc_pool_,
            "page pool exhausted: {} pages requested, {} is the addressable limit",
            index + 1,
            block_size * max_blocks
        );
        std::terminate();
    }

    const uint32 block_idx = index / block_size;
    while (blocks_.size() <= block_idx) {
        blocks_.push_back(std::make_unique<std::array<page_type, block_size>>());
    }
}

model::model(model_identity_pool& identity_pool, page_pool& pool, voxel_category category,
             int32 width, int32 height, int32 depth, int32 world_units_per_voxel)
    : identity_pool_(&identity_pool)
    , pool_ptr_(&pool)
    , category_(category)
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
    if (pool_ptr_ != nullptr && !owned_pages_.empty()) {
        pool_ptr_->free_batch(owned_pages_);
    }
    if (identity_pool_ != nullptr) {
        identity_pool_->destroy(identity_);
    }
}

model::model(model&& other) noexcept
    : identity_pool_(other.identity_pool_)
    , pool_ptr_(other.pool_ptr_)
    , category_(other.category_)
    , width_(other.width_)
    , height_(other.height_)
    , depth_(other.depth_)
    , world_units_per_voxel_(other.world_units_per_voxel_)
    , pivot_(other.pivot_)
    , pages_x_(other.pages_x_)
    , pages_y_(other.pages_y_)
    , pages_z_(other.pages_z_)
    , pages_(std::move(other.pages_))
    , owned_pages_(std::move(other.owned_pages_))
    , identity_(other.identity_)
    , fill_(other.fill_)
    , fill_known_(other.fill_known_) {
    other.identity_pool_ = nullptr;
    other.pool_ptr_      = nullptr;
}

auto model::operator=(model&& other) noexcept -> model& {
    if (this != &other) {
        if (pool_ptr_ != nullptr && !owned_pages_.empty()) {
            pool_ptr_->free_batch(owned_pages_);
        }
        if (identity_pool_ != nullptr) {
            identity_pool_->destroy(identity_);
        }
        identity_pool_       = other.identity_pool_;
        pool_ptr_            = other.pool_ptr_;
        category_            = other.category_;
        width_               = other.width_;
        height_              = other.height_;
        depth_               = other.depth_;
        world_units_per_voxel_         = other.world_units_per_voxel_;
        pivot_               = other.pivot_;
        pages_x_             = other.pages_x_;
        pages_y_             = other.pages_y_;
        pages_z_             = other.pages_z_;
        pages_               = std::move(other.pages_);
        owned_pages_         = std::move(other.owned_pages_);
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
    const voxel_index index = to_index_(v);

    fill_known_    = false;
    const int32 px = x / page_size;
    const int32 py = y / page_size;
    const int32 pz = z / page_size;
    const int32 li = local_index(x % page_size, y % page_size, z % page_size);
    auto& entry    = pages_[page_index(px, py, pz)];

    switch (entry.mode()) {
        case page_mode::empty:
            if (index.is_empty()) {
                return;
            }
            promote_to_sparse(px, py, pz)[li] = index;
            break;
        case page_mode::uniform:
            if (index == entry.fill_index()) {
                return;
            }
            promote_to_sparse(px, py, pz)[li] = index;
            break;
        case page_mode::sparse:
            pool_ptr_->get(entry.pool_index())[li] = index;
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

                const auto* page = get_page(px, py, pz);
                for (int32 ly = 0; ly < ps; ++ly) {
                    for (int32 lz = 0; lz < ps; ++lz) {
                        uint64 bits = 0;
                        for (int32 lx = 0; lx < ps; ++lx) {
                            if (!(*page)[local_index(lx, ly, lz)].is_empty()) {
                                bits |= uint64{1} << lx;
                            }
                        }
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

auto model::build_x_rows(
    chunk_occupancy& out, int32 px0, int32 px1, int32 pz0, int32 pz1
) const -> bool {
    constexpr int32 ps   = page_size;
    constexpr int32 side = chunk_occupancy::side;

    static_assert(sizeof(voxel_index) == 1);

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

                        const auto& page = pool_ptr_->get(entry.pool_index());

                        uint64 run = 0;
                        std::memcpy(&run, &page[local_index(0, ly, lz)], sizeof(run));

                        run |= run >> 4;
                        run |= run >> 2;
                        run |= run >> 1;
                        run &= 0x0101010101010101ULL;

                        bits |= ((run * 0x0102040810204080ULL) >> 56) << (px * ps);
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
    emission_table table;

    for (const voxel_type& type : registry.all()) {
        if (type.material.emission != 0) {
            table.set(type.id, type.material.emission);
        }
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
    fill_known_ = false;
    std::vector<uint32> released;

    for (auto& entry : pages_) {
        if (entry.mode() != page_mode::sparse) {
            continue;
        }

        const uint32 idx        = entry.pool_index();
        const auto& page        = pool_ptr_->get(idx);
        const voxel_index first = page[0];

        const bool uniform = std::ranges::all_of(page, [first](voxel_index index) -> bool {
            return index == first;
        });
        if (!uniform) {
            continue;
        }

        entry = first.is_empty() ? page_entry::make_empty() : page_entry::make_uniform(first);
        released.push_back(idx);
    }

    if (released.empty()) {
        return 0;
    }

    std::ranges::sort(released);
    std::erase_if(owned_pages_, [&released](uint32 owned) -> bool {
        return std::ranges::binary_search(released, owned);
    });
    pool_ptr_->free_batch(released);

    return static_cast<uint32>(released.size());
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
            case page_mode::sparse:
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

            const auto* data = get_page(page.x, page.y, page.z);

            for (int32 b = 0; b < ps; ++b) {
                uint64 bits = 0;
                for (int32 a = 0; a < ps; ++a) {
                    const auto cell = lift_off_face_plane(face, vec2i{a, b}, ll);
                    if (!(*data)[local_index(cell.x, cell.y, cell.z)].is_empty()) {
                        bits |= uint64{1} << a;
                    }
                }
                out.rows[(pb * ps) + b] |= bits << (pa * ps);
            }
        }
    }

    return true;
}

auto model::invalidate() -> void {
    increment_generation_();
}

auto model::fill(voxel v) -> void {
    if (!owned_pages_.empty()) {
        pool_ptr_->free_batch(owned_pages_);
        owned_pages_.clear();
    }

    if (v.is_empty()) {
        std::ranges::fill(pages_, page_entry::make_empty());
        fill_ = model_fill::air;
    } else {
        std::ranges::fill(pages_, page_entry::make_uniform(to_index_(v)));
        fill_ = model_fill::solid;
    }
    fill_known_ = true;
    increment_generation_();
}

auto model::fill_page_raw_(int32 px, int32 py, int32 pz, voxel v) -> void {
    fill_known_ = false;
    auto& entry = pages_[page_index(px, py, pz)];

    if (entry.mode() == page_mode::sparse) {
        free_sparse_page(entry.pool_index());
    }

    entry = v.is_empty() ? page_entry::make_empty() : page_entry::make_uniform(to_index_(v));
}

auto model::clone_pages_from(const model& source) -> void {
    fill_known_ = false;
    if (!owned_pages_.empty()) {
        pool_ptr_->free_batch(owned_pages_);
        owned_pages_.clear();
    }

    pages_ = source.pages_;

    if (!source.owned_pages_.empty()) {
        auto new_pages = pool_ptr_->alloc_batch(static_cast<uint32>(source.owned_pages_.size()));

        std::unordered_map<uint32, uint32> remap;
        remap.reserve(source.owned_pages_.size());
        for (std::size_t i = 0; i < source.owned_pages_.size(); ++i) {
            remap[source.owned_pages_[i]] = new_pages[i];
            pool_ptr_->get(new_pages[i])  = pool_ptr_->get(source.owned_pages_[i]);
        }

        for (auto& entry : pages_) {
            if (entry.mode() == page_mode::sparse) {
                entry = page_entry::make_sparse(remap[entry.pool_index()]);
            }
        }

        owned_pages_ = std::move(new_pages);
    }

    increment_generation_();
}

auto model::alloc_sparse_page() -> uint32 {
    const uint32 idx = pool_ptr_->alloc();
    owned_pages_.push_back(idx);
    return idx;
}

auto model::free_sparse_page(uint32 index) -> void {
    pool_ptr_->free(index);
    const auto it = std::ranges::find(owned_pages_, index);
    if (it != owned_pages_.end()) {
        std::iter_swap(it, owned_pages_.end() - 1);
        owned_pages_.pop_back();
    }
}

auto model::promote_to_sparse(int32 px, int32 py, int32 pz) -> page_type& {
    auto& entry      = pages_[page_index(px, py, pz)];
    const uint32 idx = alloc_sparse_page();
    auto& page       = pool_ptr_->get(idx);
    page.fill(entry.mode() == page_mode::uniform ? entry.fill_index() : voxel_index{});
    entry = page_entry::make_sparse(idx);
    return page;
}

auto model::to_index_(voxel v) const -> voxel_index {
    if (v.is_empty()) {
        return voxel_index{};
    }

    if (v.category() != category_) {
        log::critical(
            lc_pool_,
            "voxel {}:{} does not belong to this model's set {}",
            v.category().value,
            v.index(),
            category_.value
        );
        std::terminate();
    }

    return voxel_index{v.index()};
}

auto model::increment_generation_() -> void {
    identity_ = identity_pool_->next_generation(identity_);
}

auto model_registry::has(std::string_view name) const -> bool {
    return models_.contains(std::string(name));
}

auto model_registry::get(std::string_view name) const -> std::shared_ptr<model> {
    const auto iter = models_.find(std::string(name));
    return iter != models_.end() ? iter->second : nullptr;
}

auto model_registry::create(std::string_view name, voxel_category category, int32 width,
                            int32 height, int32 depth) -> std::shared_ptr<model> {
    auto new_model =
        std::make_shared<model>(identity_pool_, page_pool_, category, width, height, depth);

    const auto [it, inserted] = models_.try_emplace(std::string(name), new_model);
    if (!inserted) {
        log::warn(lc_registry_, "model '{}' is already registered, the entry is replaced", name);
        it->second = new_model;
    }

    return new_model;
}

auto model_registry::create(std::string_view name, voxel_category category, vec3i size)
    -> std::shared_ptr<model> {
    return create(name, category, size.x, size.y, size.z);
}

auto model_registry::create_unnamed(voxel_category category, int32 width, int32 height,
                                    int32 depth) -> std::shared_ptr<model> {
    return std::make_shared<model>(identity_pool_, page_pool_, category, width, height, depth);
}

auto model_registry::create_unnamed(voxel_category category, vec3i size)
    -> std::shared_ptr<model> {
    return create_unnamed(category, size.x, size.y, size.z);
}

auto model_registry::create_clone(std::string_view name) -> std::shared_ptr<model> {
    const auto original = get(name);
    if (!original) {
        return nullptr;
    }

    auto cloned_model = std::make_shared<model>(
        identity_pool_, page_pool_, original->category(), original->width(), original->height(),
        original->depth());
    cloned_model->clone_pages_from(*original);

    return cloned_model;
}

auto model_registry::erase(std::string_view name) -> void {
    models_.erase(std::string(name));
}

}  // namespace vw::asset
