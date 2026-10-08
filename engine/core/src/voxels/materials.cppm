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
    uint8 glow     = 0;
    uint8 emission = 0;
    bool sways     = false;

    [[nodiscard]] constexpr auto operator==(const material_type&) const -> bool = default;
};

using material_set    = std::bitset<material_capacity>;
using material_levels = std::array<uint8, material_capacity>;

class material_table {
public:
    explicit material_table(const voxel_registry& registry);

    [[nodiscard]] auto all() const -> std::span<const material_type> {
        return rows_;
    }

    [[nodiscard]] auto get(material id) const -> const material_type& {
        return rows_[id.value];
    }

    [[nodiscard]] auto of(voxel id) const -> material {
        return by_voxel_[id.value];
    }

    [[nodiscard]] auto swaying() const -> material_set;

    [[nodiscard]] auto emission() const -> material_levels;

private:
    std::vector<material_type> rows_;
    std::array<material, voxel_type_capacity> by_voxel_{};
};

[[nodiscard]] auto default_material_table() -> const material_table&;

}  // namespace vw
