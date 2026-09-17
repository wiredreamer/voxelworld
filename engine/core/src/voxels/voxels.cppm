export module vw.core:voxels;

import std;

import :types;
import :color;

export namespace vw {

struct voxel_category {
    uint8 value = 0;

    constexpr voxel_category() = default;

    constexpr explicit voxel_category(
        uint8 value_
    )
        : value(value_) {}

    constexpr auto operator==(const voxel_category&) const -> bool = default;
};

struct voxel {
    uint16 value = 0;

    constexpr voxel() = default;

    constexpr voxel(
        voxel_category category, uint8 index
    )
        : value(static_cast<uint16>((static_cast<uint16>(category.value) << 8U) | index)) {}

    [[nodiscard]] static constexpr auto from_raw(
        uint16 raw
    ) -> voxel {
        voxel id;
        id.value = raw;
        return id;
    }

    [[nodiscard]] constexpr auto category() const -> voxel_category {
        return voxel_category{static_cast<uint8>(value >> 8U)};
    }

    [[nodiscard]] constexpr auto index() const -> uint8 {
        return static_cast<uint8>(value & 0xFFU);
    }

    [[nodiscard]] constexpr auto is_empty() const -> bool {
        return value == 0;
    }

    constexpr auto operator==(const voxel&) const -> bool = default;
};

namespace voxels {
inline constexpr auto air = voxel{};
}  // namespace voxels

struct voxel_index {
    uint8 value = 0;

    constexpr voxel_index() = default;

    constexpr explicit voxel_index(
        uint8 value_
    )
        : value(value_) {}

    [[nodiscard]] constexpr auto is_empty() const -> bool {
        return value == 0;
    }

    constexpr auto operator==(const voxel_index&) const -> bool = default;
};

struct voxel_span {
    voxel first;
    uint8 count = 1;

    constexpr voxel_span() = default;

    constexpr voxel_span(
        voxel_category category, uint8 index, uint8 count_
    )
        : first(category, index), count(count_) {}

    [[nodiscard]] constexpr auto operator[](
        uint32 variant
    ) const -> voxel {
        return voxel{first.category(), static_cast<uint8>(first.index() + (variant % count))};
    }

    [[nodiscard]] constexpr auto pick(
        uint32 patch_noise
    ) const -> voxel {
        return (*this)[patch_noise];
    }

    [[nodiscard]] constexpr auto contains(
        voxel id
    ) const -> bool {
        return                                    //
            id.category() == first.category() &&  //
            id.index() >= first.index() &&        //
            id.index() < first.index() + count;
    }
};

enum class voxel_surface : uint8 {
    invisible,
    opaque,
};

struct voxel_material {
    color clr = colors::empty;

    uint8 emission = 0;

    uint8 glow = 0;

    constexpr auto operator==(const voxel_material&) const -> bool = default;
};

struct voxel_desc {
    voxel id;
    std::string_view name;
    voxel_material material;
    voxel_surface surface = voxel_surface::opaque;
};

struct voxel_group {
    std::string_view name;
    voxel first;
    uint8 count = 1;

    [[nodiscard]] constexpr auto at(
        uint8 offset
    ) const -> voxel {
        return voxel{first.category(), static_cast<uint8>(first.index() + offset)};
    }
};

enum class voxel_set_kind : uint8 { palette, materials };

struct voxel_set {
    voxel_category category;
    std::string_view name;

    voxel_set_kind kind = voxel_set_kind::materials;

    std::span<const voxel_group> groups;
};

struct voxel_slot {
    uint16 value = 0;

    constexpr auto operator==(const voxel_slot&) const -> bool = default;
};

inline constexpr uint32 voxel_slot_capacity = 1024;

inline constexpr auto missing_voxel_slot = voxel_slot{0};

struct voxel_type {
    voxel id;
    voxel_slot slot;
    std::string_view name;
    voxel_material material;
    voxel_surface surface = voxel_surface::invisible;
};

template <typename T>
class voxel_table {
public:
    voxel_table() = default;

    explicit voxel_table(
        T fallback
    ) {
        rows_[0].fill(fallback);
    }

    [[nodiscard]] auto get(
        voxel id
    ) const -> const T& {
        return rows_[row_of_[id.category().value]][id.index()];
    }

    [[nodiscard]] auto row(
        voxel_category category
    ) const -> const std::array<T, 256>& {
        return rows_[row_of_[category.value]];
    }

    auto set(
        voxel id, T value
    ) -> void {
        uint16& row = row_of_[id.category().value];
        if (row == 0) {
            const row_type defaults = rows_.front();
            row                     = static_cast<uint16>(rows_.size());
            rows_.push_back(defaults);
        }
        rows_[row][id.index()] = std::move(value);
    }

private:
    using row_type = std::array<T, 256>;

    std::vector<row_type> rows_{1};
    std::array<uint16, 256> row_of_{};
};

class voxel_registry {
public:
    voxel_registry();

    explicit voxel_registry(std::span<const voxel_desc> extra);

    [[nodiscard]] auto get(
        voxel id
    ) const -> const voxel_type& {
        return by_slot_[slot_of(id).value];
    }

    [[nodiscard]] auto get(
        voxel_slot slot
    ) const -> const voxel_type& {
        return by_slot_[slot.value];
    }

    [[nodiscard]] auto slot_of(
        voxel id
    ) const -> voxel_slot {
        return voxel_slot{slots_.get(id)};
    }

    [[nodiscard]] auto slot_row(
        voxel_category category
    ) const -> const std::array<uint16, 256>& {
        return slots_.row(category);
    }

    [[nodiscard]] auto find(std::string_view name) const -> std::optional<voxel>;

    [[nodiscard]] auto sets() const -> std::span<const voxel_set> {
        return sets_;
    }

    [[nodiscard]] auto set_of(voxel_category category) const -> const voxel_set*;

    [[nodiscard]] auto first_set(voxel_set_kind kind) const -> const voxel_set*;

    [[nodiscard]] auto all() const -> std::span<const voxel_type> {
        return by_slot_;
    }

private:
    auto add_(const voxel_desc& desc) -> void;

    std::vector<voxel_type> by_slot_;
    std::vector<voxel_set> sets_;
    voxel_table<uint16> slots_;
    std::unordered_map<std::string_view, voxel> by_name_;
};

[[nodiscard]] auto default_voxel_registry() -> const voxel_registry&;

}  // namespace vw
