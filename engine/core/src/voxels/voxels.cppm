export module vw.core:voxels;

import std;

import :types;
import :color;

export namespace vw {

struct voxel {
    uint8 value = 0;

    constexpr voxel() = default;

    constexpr explicit voxel(
        uint8 value_
    )
        : value(value_) {}

    [[nodiscard]] constexpr auto is_empty() const -> bool {
        return value == 0;
    }

    constexpr auto operator==(const voxel&) const -> bool = default;
};

inline constexpr uint32 voxel_type_capacity = 256;

namespace voxels {
inline constexpr auto air = voxel{};
}  // namespace voxels

struct voxel_span {
    voxel first;
    uint8 count = 1;

    constexpr voxel_span() = default;

    constexpr voxel_span(
        uint8 first_value, uint8 count_
    )
        : first(first_value), count(count_) {}

    [[nodiscard]] constexpr auto operator[](
        uint32 variant
    ) const -> voxel {
        return voxel{static_cast<uint8>(first.value + (variant % count))};
    }

    [[nodiscard]] constexpr auto pick(
        uint32 patch_noise
    ) const -> voxel {
        return (*this)[patch_noise];
    }

    [[nodiscard]] constexpr auto contains(
        voxel id
    ) const -> bool {
        return id.value >= first.value && id.value < first.value + count;
    }
};

enum class voxel_surface : uint8 {
    invisible,
    opaque,
};

enum class voxel_kind : uint8 {
    plain,
    wood,
    leaf,
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
    voxel_kind kind       = voxel_kind::plain;
};

struct voxel_group {
    std::string_view name;
    voxel first;
    uint8 count = 1;

    [[nodiscard]] constexpr auto at(
        uint8 offset
    ) const -> voxel {
        return voxel{static_cast<uint8>(first.value + offset)};
    }
};

struct voxel_type {
    voxel id;
    std::string_view name;
    voxel_material material;
    voxel_surface surface = voxel_surface::invisible;
    voxel_kind kind       = voxel_kind::plain;
};

class voxel_registry {
public:
    voxel_registry();

    explicit voxel_registry(std::span<const voxel_desc> extra);

    [[nodiscard]] auto get(
        voxel id
    ) const -> const voxel_type& {
        return by_value_[id.value];
    }

    [[nodiscard]] auto known(
        voxel id
    ) const -> bool {
        return known_[id.value];
    }

    [[nodiscard]] auto find(std::string_view name) const -> std::optional<voxel>;

    [[nodiscard]] auto groups() const -> std::span<const voxel_group> {
        return groups_;
    }

    [[nodiscard]] auto all() const -> std::span<const voxel_type> {
        return by_value_;
    }

private:
    auto add_(const voxel_desc& desc) -> void;

    std::vector<voxel_type> by_value_;
    std::vector<voxel_group> groups_;
    std::bitset<voxel_type_capacity> known_;
    std::unordered_map<std::string_view, voxel> by_name_;
};

[[nodiscard]] auto default_voxel_registry() -> const voxel_registry&;

}  // namespace vw
