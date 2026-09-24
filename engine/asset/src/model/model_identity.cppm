export module vw.asset:model.identity;

import std;

import vw.core;

namespace vw::asset::detail {

[[noreturn]] auto report_page_store_exhausted(uint32 requested, uint32 limit) -> void;

}  // namespace vw::asset::detail

export namespace vw::asset {

struct model_identity {
    static constexpr uint32 invalid_index = std::numeric_limits<uint32>::max();

    uint32 index      = invalid_index;
    uint32 generation = 0;

    [[nodiscard]] auto operator==(const model_identity& other) const -> bool {
        return index == other.index && generation == other.generation;
    }

    [[nodiscard]] auto operator!=(const model_identity& other) const -> bool {
        return !(*this == other);
    }

    [[nodiscard]] auto is_valid() const -> bool {
        return index != invalid_index;
    }
};

inline constexpr auto invalid_model_identity = model_identity{};

class model_identity_pool final {
public:
    static constexpr std::size_t default_capacity = 1024;

    explicit model_identity_pool(std::size_t capacity = default_capacity);

    [[nodiscard]] auto create() -> model_identity;
    [[nodiscard]] auto next_generation(model_identity id) -> model_identity;
    [[nodiscard]] auto has(model_identity id) const -> bool;

    auto destroy(model_identity id) -> void;

private:
    [[nodiscard]] auto has_unlocked_(model_identity id) const -> bool {
        return id.index != model_identity::invalid_index && id.index < generations_.size() &&
               generations_[id.index] == id.generation;
    }

    mutable std::mutex mutex_;
    std::vector<uint32> generations_;
    std::vector<uint32> free_indices_;
};

inline constexpr int32 voxel_page_size = 8;
inline constexpr int32 voxel_page_volume =
    voxel_page_size * voxel_page_size * voxel_page_size;

[[nodiscard]] constexpr auto voxel_page_local_index(
    int32 lx, int32 ly, int32 lz
) -> int32 {
    return lx + (ly * voxel_page_size) + (lz * voxel_page_size * voxel_page_size);
}

[[nodiscard]] constexpr auto voxel_page_row_index(
    int32 ly, int32 lz
) -> int32 {
    return ly + (lz * voxel_page_size);
}

using binary_page = std::array<uint8, voxel_page_volume / 8>;

inline constexpr uint32 palette_page_slots = 16;

struct palette_page {
    std::array<uint8, voxel_page_volume / 2> nibbles;
    std::array<voxel, palette_page_slots> palette;

    [[nodiscard]] auto slot_at(
        int32 local
    ) const -> uint32 {
        const uint32 pair = nibbles[static_cast<std::size_t>(local >> 1)];
        return (local & 1) != 0 ? pair >> 4U : pair & 0xFU;
    }

    [[nodiscard]] auto voxel_at(
        int32 local
    ) const -> voxel {
        return palette[slot_at(local)];
    }

    auto set_slot(
        int32 local, uint32 slot
    ) -> void {
        uint8& pair = nibbles[static_cast<std::size_t>(local >> 1)];
        pair        = (local & 1) != 0
                          ? static_cast<uint8>((pair & 0x0FU) | (slot << 4U))
                          : static_cast<uint8>((pair & 0xF0U) | slot);
    }
};

template <typename T, uint32 BlockSlots, uint32 MaxBlocks>
class page_store {
public:
    using slot_type = T;

    static constexpr uint32 block_slots = BlockSlots;
    static constexpr uint32 max_blocks  = MaxBlocks;
    static constexpr uint32 capacity    = BlockSlots * MaxBlocks;
    static constexpr uint32 slot_bytes  = static_cast<uint32>(sizeof(T));

    page_store() {
        blocks_.reserve(max_blocks);
    }

    [[nodiscard]] auto alloc() -> uint32 {
        if (!free_indices_.empty()) {
            const uint32 index = free_indices_.back();
            free_indices_.pop_back();
            return index;
        }

        const uint32 index = next_index_++;
        ensure_capacity_(index);
        return index;
    }

    auto free(uint32 index) -> void {
        free_indices_.push_back(index);
    }

    [[nodiscard]] auto alloc_batch(uint32 count) -> std::vector<uint32> {
        std::vector<uint32> result;
        result.reserve(count);

        const uint32 reused =
            std::min(count, static_cast<uint32>(free_indices_.size()));
        const uint32 fresh = count - reused;

        const uint32 bump_start = next_index_;
        for (uint32 i = 0; i < fresh; ++i) {
            result.push_back(bump_start + i);
        }
        next_index_ = bump_start + fresh;
        if (fresh > 0) {
            ensure_capacity_(next_index_ - 1);
        }

        for (uint32 i = 0; i < reused; ++i) {
            result.push_back(free_indices_.back());
            free_indices_.pop_back();
        }

        return result;
    }

    auto free_batch(std::span<const uint32> indices) -> void {
        free_indices_.reserve(free_indices_.size() + indices.size());
        for (const uint32 index : indices) {
            free_indices_.push_back(index);
        }
    }

    [[nodiscard]] auto get(
        uint32 index
    ) -> T& {
        return (*blocks_[index / block_slots])[index % block_slots];
    }

    [[nodiscard]] auto get(
        uint32 index
    ) const -> const T& {
        return (*blocks_[index / block_slots])[index % block_slots];
    }

    [[nodiscard]] auto allocated_count() const -> uint32 {
        return next_index_ - static_cast<uint32>(free_indices_.size());
    }

    [[nodiscard]] auto free_count() const -> uint32 {
        return static_cast<uint32>(free_indices_.size());
    }

private:
    auto ensure_capacity_(
        uint32 index
    ) -> void {
        if (index >= capacity) {
            detail::report_page_store_exhausted(index + 1, capacity);
        }

        const uint32 block_index = index / block_slots;
        while (blocks_.size() <= block_index) {
            blocks_.push_back(std::make_unique<std::array<T, block_slots>>());
        }
    }

    std::vector<std::unique_ptr<std::array<T, block_slots>>> blocks_;
    std::vector<uint32> free_indices_;
    uint32 next_index_ = 0;
};

class page_pool final {
public:
    using page_type    = std::array<voxel, voxel_page_volume>;
    using dense_store   = page_store<page_type, 4096, 256>;
    using binary_store  = page_store<binary_page, 8192, 256>;
    using palette_store = page_store<palette_page, 8192, 256>;

    static constexpr uint32 block_size = dense_store::block_slots;
    static constexpr uint32 max_blocks = dense_store::max_blocks;

    [[nodiscard]] auto alloc_dense() -> uint32 {
        const std::scoped_lock lock(dense_mutex_);
        return dense_.alloc();
    }

    auto free_dense(
        uint32 index
    ) -> void {
        const std::scoped_lock lock(dense_mutex_);
        dense_.free(index);
    }

    [[nodiscard]] auto alloc_dense_batch(
        uint32 count
    ) -> std::vector<uint32> {
        const std::scoped_lock lock(dense_mutex_);
        return dense_.alloc_batch(count);
    }

    auto free_dense_batch(
        std::span<const uint32> indices
    ) -> void {
        const std::scoped_lock lock(dense_mutex_);
        dense_.free_batch(indices);
    }

    [[nodiscard]] auto alloc_binary() -> uint32 {
        const std::scoped_lock lock(binary_mutex_);
        return binary_.alloc();
    }

    auto free_binary(
        uint32 index
    ) -> void {
        const std::scoped_lock lock(binary_mutex_);
        binary_.free(index);
    }

    [[nodiscard]] auto alloc_binary_batch(
        uint32 count
    ) -> std::vector<uint32> {
        const std::scoped_lock lock(binary_mutex_);
        return binary_.alloc_batch(count);
    }

    auto free_binary_batch(
        std::span<const uint32> indices
    ) -> void {
        const std::scoped_lock lock(binary_mutex_);
        binary_.free_batch(indices);
    }

    [[nodiscard]] auto alloc_palette() -> uint32 {
        const std::scoped_lock lock(palette_mutex_);
        return palette_.alloc();
    }

    auto free_palette(
        uint32 index
    ) -> void {
        const std::scoped_lock lock(palette_mutex_);
        palette_.free(index);
    }

    auto free_palette_batch(
        std::span<const uint32> indices
    ) -> void {
        const std::scoped_lock lock(palette_mutex_);
        palette_.free_batch(indices);
    }

    [[nodiscard]] auto get_palette(
        uint32 index
    ) -> palette_page& {
        return palette_.get(index);
    }

    [[nodiscard]] auto get_palette(
        uint32 index
    ) const -> const palette_page& {
        return palette_.get(index);
    }

    [[nodiscard]] auto palette_count() const -> uint32 {
        const std::scoped_lock lock(palette_mutex_);
        return palette_.allocated_count();
    }

    [[nodiscard]] auto get_dense(
        uint32 index
    ) -> page_type& {
        return dense_.get(index);
    }

    [[nodiscard]] auto get_dense(
        uint32 index
    ) const -> const page_type& {
        return dense_.get(index);
    }

    [[nodiscard]] auto get_binary(
        uint32 index
    ) -> binary_page& {
        return binary_.get(index);
    }

    [[nodiscard]] auto get_binary(
        uint32 index
    ) const -> const binary_page& {
        return binary_.get(index);
    }

    [[nodiscard]] auto dense_count() const -> uint32 {
        const std::scoped_lock lock(dense_mutex_);
        return dense_.allocated_count();
    }

    [[nodiscard]] auto binary_count() const -> uint32 {
        const std::scoped_lock lock(binary_mutex_);
        return binary_.allocated_count();
    }

    [[nodiscard]] auto allocated_count() const -> uint32 {
        const std::scoped_lock lock(dense_mutex_, binary_mutex_, palette_mutex_);
        return dense_.allocated_count() + binary_.allocated_count() +
               palette_.allocated_count();
    }

    [[nodiscard]] auto free_count() const -> uint32 {
        const std::scoped_lock lock(dense_mutex_, binary_mutex_, palette_mutex_);
        return dense_.free_count() + binary_.free_count() + palette_.free_count();
    }

    [[nodiscard]] auto bytes_reserved() const -> std::size_t {
        const std::scoped_lock lock(dense_mutex_, binary_mutex_, palette_mutex_);
        return (std::size_t{dense_.allocated_count() + dense_.free_count()} *
                dense_store::slot_bytes) +
               (std::size_t{binary_.allocated_count() + binary_.free_count()} *
                binary_store::slot_bytes) +
               (std::size_t{palette_.allocated_count() + palette_.free_count()} *
                palette_store::slot_bytes);
    }

private:
    mutable std::mutex dense_mutex_;
    mutable std::mutex binary_mutex_;
    mutable std::mutex palette_mutex_;
    dense_store dense_;
    binary_store binary_;
    palette_store palette_;
};

enum class page_mode : uint8 {
    empty   = 0,
    uniform = 1,
    dense   = 2,
    binary  = 3,
    palette = 4,
};

enum class model_fill : uint8 { mixed = 0, air = 1, solid = 2 };

struct page_entry {
    static constexpr uint32 mode_bits     = 3;
    static constexpr uint32 fill_bits     = 8;
    static constexpr uint32 binary_shift  = mode_bits + fill_bits;
    static constexpr uint32 binary_limit  = 1U << (32 - binary_shift);
    static constexpr uint32 slot_limit    = 1U << (32 - mode_bits);

    uint32 data = 0;

    [[nodiscard]] auto mode() const -> page_mode {
        return static_cast<page_mode>(data & 0x7U);
    }

    [[nodiscard]] auto fill_voxel() const -> voxel {
        return voxel{static_cast<uint8>((data >> mode_bits) & 0xFFU)};
    }

    [[nodiscard]] auto dense_slot() const -> uint32 {
        return data >> mode_bits;
    }

    [[nodiscard]] auto binary_slot() const -> uint32 {
        return data >> binary_shift;
    }

    [[nodiscard]] auto palette_slot() const -> uint32 {
        return data >> mode_bits;
    }

    [[nodiscard]] static auto make_empty() -> page_entry {
        return {0U};
    }

    [[nodiscard]] static auto make_uniform(voxel index) -> page_entry {
        return {1U | (static_cast<uint32>(index.value) << mode_bits)};
    }

    [[nodiscard]] static auto make_dense(uint32 slot) -> page_entry {
        return {2U | (slot << mode_bits)};
    }

    [[nodiscard]] static auto make_binary(voxel fill, uint32 slot) -> page_entry {
        return {
            3U | (static_cast<uint32>(fill.value) << mode_bits) | (slot << binary_shift)
        };
    }

    [[nodiscard]] static auto make_palette(uint32 slot) -> page_entry {
        return {4U | (slot << mode_bits)};
    }
};

static_assert(page_pool::binary_store::capacity <= page_entry::binary_limit);
static_assert(page_pool::palette_store::capacity <= page_entry::slot_limit);
static_assert(page_pool::dense_store::capacity <= page_entry::slot_limit);
static_assert(sizeof(palette_page) == (voxel_page_volume / 2) + palette_page_slots);

class page_view {
public:
    page_view() = default;

    explicit page_view(
        const page_pool::page_type& dense
    )
        : dense_{&dense} {}

    page_view(
        const binary_page& bits, voxel fill
    )
        : binary_{&bits}, fill_{fill} {}

    explicit page_view(
        const palette_page& packed
    )
        : palette_{&packed} {}

    [[nodiscard]] auto valid() const -> bool {
        return dense_ != nullptr || binary_ != nullptr || palette_ != nullptr;
    }

    [[nodiscard]] auto voxel_at(
        int32 lx, int32 ly, int32 lz
    ) const -> voxel {
        if (binary_ != nullptr) {
            const uint32 row =
                (*binary_)[static_cast<std::size_t>(voxel_page_row_index(ly, lz))];
            return ((row >> lx) & 1U) != 0 ? fill_ : voxel{};
        }

        if (palette_ != nullptr) {
            return palette_->voxel_at(voxel_page_local_index(lx, ly, lz));
        }

        return (*dense_)[static_cast<std::size_t>(voxel_page_local_index(lx, ly, lz))];
    }

    [[nodiscard]] auto row_bits(
        int32 ly, int32 lz
    ) const -> uint32 {
        static_assert(sizeof(voxel) == 1);

        if (binary_ != nullptr) {
            return (*binary_)[static_cast<std::size_t>(voxel_page_row_index(ly, lz))];
        }

        if (palette_ != nullptr) {
            uint32 nibbles = 0;
            std::memcpy(
                &nibbles,
                &palette_->nibbles[static_cast<std::size_t>(
                    voxel_page_local_index(0, ly, lz) / 2
                )],
                sizeof(nibbles)
            );

            nibbles |= nibbles >> 2;
            nibbles |= nibbles >> 1;
            nibbles &= 0x11111111U;
            nibbles = (nibbles | (nibbles >> 3)) & 0x03030303U;

            return (nibbles * 0x01041040U) >> 24;
        }

        uint64 run = 0;
        std::memcpy(
            &run, &(*dense_)[static_cast<std::size_t>(voxel_page_local_index(0, ly, lz))],
            sizeof(run)
        );

        run |= run >> 4;
        run |= run >> 2;
        run |= run >> 1;
        run &= 0x0101010101010101ULL;

        return static_cast<uint32>((run * 0x0102040810204080ULL) >> 56);
    }

private:
    const page_pool::page_type* dense_ = nullptr;
    const binary_page* binary_         = nullptr;
    const palette_page* palette_       = nullptr;
    voxel fill_{};
};

}  // namespace vw::asset
export template <>
struct std::hash<vw::asset::model_identity> {
    auto operator()(const vw::asset::model_identity& id) const noexcept -> std::size_t {
        std::size_t x = (std::size_t{id.generation} << 32) | std::size_t{id.index};

        x += 0x9e3779b97f4a7c15ULL;
        x = (x ^ (x >> 30)) * 0xbf58476d1ce4e5b9ULL;
        x = (x ^ (x >> 27)) * 0x94d049bb133111ebULL;
        x = x ^ (x >> 31);

        return x;
    }
};
