export module vw.core:voxels.materials;

import std;

import :types;
import :voxels;

export namespace vw {

inline constexpr int32 material_capacity = 256;

struct material {
    uint8 value = 0;

    [[nodiscard]] constexpr auto operator==(const material&) const -> bool = default;
};

// см. docs/ENGINE.md#материалы
struct material_type {
    std::string_view name;
    uint8 glow     = 0;
    uint8 emission = 0;
    bool sways     = false;

    [[nodiscard]] constexpr auto operator==(const material_type&) const -> bool = default;
};

namespace materials {
inline constexpr auto inert  = material{0};
inline constexpr auto wood   = material{1};
inline constexpr auto leaves = material{2};
inline constexpr auto glow   = material{3};
inline constexpr auto lamp   = material{4};
inline constexpr auto fire   = material{5};
}  // namespace materials

inline constexpr std::array default_material_catalog = {
    material_type{.name = "inert"},
    material_type{.name = "wood"},
    material_type{.name = "leaves", .sways = true},
    material_type{.name = "glow", .glow = 200},
    material_type{.name = "lamp", .glow = 200, .emission = 14},
    material_type{.name = "fire", .glow = 255, .emission = 15},
};

// см. docs/ENGINE.md#слой-материала
struct matter {
    voxel color;
    material made_of;

    constexpr matter() = default;

    constexpr matter(
        voxel color_, material made_of_ = material{}
    )
        : color(color_), made_of(made_of_) {}

    [[nodiscard]] constexpr auto is_empty() const -> bool {
        return color.is_empty();
    }

    [[nodiscard]] constexpr auto operator==(const matter&) const -> bool = default;
};

using material_set    = std::bitset<material_capacity>;
using material_levels = std::array<uint8, material_capacity>;

class material_table {
public:
    material_table();

    explicit material_table(std::span<const material_type> rows);

    [[nodiscard]] auto all() const -> std::span<const material_type> {
        return rows_;
    }

    [[nodiscard]] auto named() const -> std::span<const material_type> {
        return std::span{rows_}.first(named_);
    }

    [[nodiscard]] auto get(material id) const -> const material_type& {
        return rows_[id.value];
    }

    [[nodiscard]] auto find(std::string_view name) const -> std::optional<material>;

    [[nodiscard]] auto swaying() const -> material_set;

    [[nodiscard]] auto emission() const -> material_levels;

private:
    std::vector<material_type> rows_;
    std::size_t named_ = 0;
};

[[nodiscard]] auto default_material_table() -> const material_table&;

}  // namespace vw
