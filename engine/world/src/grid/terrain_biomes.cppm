export module vw.world:terrain.biomes;
import :terrain.noise;
import :grid.cell;

import std;

import vw.core;

namespace vw::ecs {

[[nodiscard]] inline auto smoothstep(float64 edge0, float64 edge1, float64 x) -> float64 {
    const float64 t = std::clamp((x - edge0) / (edge1 - edge0), 0.0, 1.0);
    return t * t * (3.0 - (2.0 * t));
}

[[nodiscard]] inline auto unit_noise(float64 n) -> float64 {
    return (n + 1.0) * 0.5;
}

}  // namespace vw::ecs

export namespace vw::ecs {

struct tone_ramp {
    voxel_span row         = voxels::green;
    float32 from           = 0.0F;
    float32 to             = 4.0F;
    float32 spot_frequency = 0.01F;
    float32 spot_contrast  = 2.5F;
    material made_of       = materials::inert;

    [[nodiscard]] auto operator==(const tone_ramp& other) const -> bool {
        return row.first == other.row.first && row.count == other.row.count && from == other.from &&
               to == other.to && spot_frequency == other.spot_frequency &&
               spot_contrast == other.spot_contrast && made_of == other.made_of;
    }
};

struct wave {
    float32 frequency   = 0.01F;
    int32 octaves       = 4;
    float32 persistence = 0.25F;

    auto operator==(const wave&) const -> bool = default;
};

[[nodiscard]] auto wave_at(const wave& shape, const perlin_noise& noise, float64 x, float64 z,
                           float64 salt) -> float64;

struct biome_climate {
    float32 relief   = 0.5F;
    float32 moisture = 0.5F;

    auto operator==(const biome_climate&) const -> bool = default;
};

struct biome_point {
    const perlin_noise* noise = nullptr;
    float64 x        = 0.0;
    float64 z        = 0.0;
    float64 relief   = 0.0;
    float64 moisture = 0.0;
};

struct neighbour_tone {
    std::string_view role;
    const tone_ramp* ramp = nullptr;
};

struct column_facts {
    static constexpr std::size_t tone_capacity = 4;

    const perlin_noise* noise = nullptr;
    float64 x     = 0.0;
    float64 z     = 0.0;
    int32 surface = 0;
    float32 slope = 0.0F;

    std::array<neighbour_tone, tone_capacity> neighbour{};
    uint8 neighbours        = 0;
    float32 neighbour_share = 0.0F;
};

[[nodiscard]] auto tone_at(const column_facts& column, const tone_ramp& own, std::string_view role,
                           float64 salt) -> voxel;

struct grass_cover {
    float32 density         = 0.4F;
    float32 patch_frequency = 0.04F;
    float32 flower_share    = 0.05F;

    auto operator==(const grass_cover&) const -> bool = default;
};

[[nodiscard]] auto cover_form_at(const column_facts& column, const grass_cover& cover) -> uint8;

inline constexpr int32 plant_cell_voxels = 8;

// см. docs/world.md#деревья
struct tree_species {
    int32 min_height     = 10;
    int32 max_height     = 20;
    float32 fork_share   = 0.6F;
    int32 branches       = 4;
    float32 branch_reach = 0.4F;
    float32 crown_share  = 0.27F;
    float32 roughness    = 0.35F;
    tone_ramp bark{
        .row = voxels::amber, .from = 3.0F, .to = 5.0F, .spot_frequency = 0.02F, .made_of = materials::wood
    };
    tone_ramp leaves{
        .row            = voxels::green,
        .from           = 2.0F,
        .to             = 6.0F,
        .spot_frequency = 0.012F,
        .made_of        = materials::leaves,
    };

    auto operator==(const tree_species&) const -> bool = default;
};

struct forest {
    float32 density         = 0.3F;
    float32 patch_frequency = 0.008F;
    float32 fill            = 0.6F;
    float32 lone_trees      = 0.02F;
    tree_species tree;

    auto operator==(const forest&) const -> bool = default;
};

template <class Self, class Visit>
auto visit_forest_fields(Self& self, Visit&& visit) -> void {
    visit("forest_density", self.woods.density, 0.0F, 1.0F);
    visit("forest_patch_frequency", self.woods.patch_frequency, 0.001F, 0.05F);
    visit("forest_fill", self.woods.fill, 0.0F, 1.0F);
    visit("lone_trees", self.woods.lone_trees, 0.0F, 0.3F);
    visit("tree_min_height", self.woods.tree.min_height, 6.0F, 30.0F);
    visit("tree_max_height", self.woods.tree.max_height, 6.0F, 30.0F);
    visit("tree_fork", self.woods.tree.fork_share, 0.3F, 0.9F);
    visit("tree_branches", self.woods.tree.branches, 2.0F, 6.0F);
    visit("tree_reach", self.woods.tree.branch_reach, 0.1F, 0.7F);
    visit("tree_crown", self.woods.tree.crown_share, 0.12F, 0.45F);
    visit("tree_roughness", self.woods.tree.roughness, 0.0F, 0.8F);
    visit("bark", self.woods.tree.bark);
    visit("leaves", self.woods.tree.leaves);
}

struct paint_layer {
    matter fill;
    uint8 thickness = 0;
};

struct column_paint {
    static constexpr std::size_t capacity = 3;

    std::array<paint_layer, capacity> layers{};
    uint8 count = 0;
    uint8 cover = 0;
    bool fertile = false;

    auto add(matter fill, int32 thickness) -> column_paint& {
        if (count < capacity && thickness > 0) {
            layers[count] = {.fill = fill, .thickness = static_cast<uint8>(std::min(thickness, 255))};
            ++count;
        }
        return *this;
    }

    [[nodiscard]] auto depth() const -> int32 {
        int32 total = 0;
        for (uint8 index = 0; index < count; ++index) {
            total += layers[index].thickness;
        }
        return total;
    }

    [[nodiscard]] auto at(int32 below_surface) const -> std::optional<matter> {
        int32 bottom = 0;
        for (uint8 index = 0; index < count; ++index) {
            bottom += layers[index].thickness;
            if (below_surface < bottom) {
                return layers[index].fill;
            }
        }
        return std::nullopt;
    }
};

template <class Self, class Visit>
auto visit_climate_fields(Self& self, Visit&& visit) -> void {
    visit("relief", self.climate.relief, 0.0F, 1.0F);
    visit("moisture", self.climate.moisture, 0.0F, 1.0F);
}

template <class Self, class Visit>
auto visit_cover_fields(Self& self, Visit&& visit) -> void {
    visit("grass_density", self.cover.density, 0.0F, 1.0F);
    visit("grass_patch_frequency", self.cover.patch_frequency, 0.005F, 0.5F);
    visit("flower_share", self.cover.flower_share, 0.0F, 1.0F);
}

template <class Self, class Visit>
auto visit_wave_fields(Self& self, Visit&& visit) -> void {
    visit("frequency", self.shape.frequency, 0.0005F, 0.05F);
    visit("octaves", self.shape.octaves, 1.0F, 5.0F);
    visit("persistence", self.shape.persistence, 0.1F, 0.8F);
}

struct plains_biome {
    static constexpr std::string_view name = "plains";

    biome_climate climate{.relief = 0.3F};

    int32 base_height = 20;
    float32 height    = 12.0F;
    wave shape{.frequency = 0.01F, .octaves = 4, .persistence = 0.2F};

    tone_ramp grass{.row = voxels::green, .from = 4.0F, .to = 8.0F};
    tone_ramp dirt{.row = voxels::brown, .from = 4.0F, .to = 8.0F};
    int32 dirt_depth = 4;
    grass_cover cover{.density = 0.28F, .patch_frequency = 0.18F, .flower_share = 0.02F};
    forest woods{
        .density = 0.06F, .fill = 0.5F, .lone_trees = 0.008F,
        .tree    = {.min_height = 14, .max_height = 20, .crown_share = 0.3F},
    };

    [[nodiscard]] auto height_at(const biome_point& at) const -> float64;
    [[nodiscard]] auto paint_at(const column_facts& column) const -> column_paint;

    template <class Self, class Visit>
    static auto visit_fields(Self& self, Visit&& visit) -> void {
        visit_climate_fields(self, visit);
        visit("base_height", self.base_height, -40.0F, 120.0F);
        visit("height", self.height, 0.0F, 80.0F);
        visit_wave_fields(self, visit);
        visit("grass", self.grass);
        visit("dirt", self.dirt);
        visit("dirt_depth", self.dirt_depth, 1.0F, 16.0F);
        visit_cover_fields(self, visit);
        visit_forest_fields(self, visit);
    }
};

struct hills_biome {
    static constexpr std::string_view name = "hills";

    biome_climate climate{.relief = 0.7F};

    int32 base_height = 24;
    float32 height    = 25.48F;
    wave shape{.frequency = 0.015F, .octaves = 4, .persistence = 0.25F};

    tone_ramp grass{.row = voxels::green, .from = 2.0F, .to = 6.0F};
    tone_ramp dirt{.row = voxels::brown, .from = 3.0F, .to = 7.0F};
    tone_ramp stone{.row = voxels::gray, .from = 10.0F, .to = 14.0F};
    int32 dirt_depth    = 3;
    int32 stone_depth   = 4;
    float32 stone_slope = 1.2F;
    grass_cover cover{.density = 0.2F, .patch_frequency = 0.2F, .flower_share = 0.012F};
    forest woods{
        .density = 0.45F, .fill = 0.7F, .lone_trees = 0.03F,
        .tree    = {.min_height = 10, .max_height = 16},
    };

    [[nodiscard]] auto height_at(const biome_point& at) const -> float64;
    [[nodiscard]] auto paint_at(const column_facts& column) const -> column_paint;

    template <class Self, class Visit>
    static auto visit_fields(Self& self, Visit&& visit) -> void {
        visit_climate_fields(self, visit);
        visit("base_height", self.base_height, -40.0F, 120.0F);
        visit("height", self.height, 0.0F, 160.0F);
        visit_wave_fields(self, visit);
        visit("grass", self.grass);
        visit("dirt", self.dirt);
        visit("stone", self.stone);
        visit("dirt_depth", self.dirt_depth, 1.0F, 16.0F);
        visit("stone_depth", self.stone_depth, 1.0F, 16.0F);
        visit("stone_slope", self.stone_slope, 0.2F, 4.0F);
        visit_cover_fields(self, visit);
        visit_forest_fields(self, visit);
    }
};

using terrain_biome = std::variant<plains_biome, hills_biome>;

[[nodiscard]] auto default_biomes() -> std::vector<terrain_biome>;

[[nodiscard]] inline auto biome_name(const terrain_biome& biome) -> std::string_view {
    return std::visit([](const auto& b) -> std::string_view { return b.name; }, biome);
}

[[nodiscard]] inline auto climate_of(const terrain_biome& biome) -> const biome_climate& {
    return std::visit([](const auto& b) -> const biome_climate& { return b.climate; }, biome);
}

[[nodiscard]] inline auto height_of(const terrain_biome& biome, const biome_point& at) -> float64 {
    return std::visit([&at](const auto& b) -> float64 { return b.height_at(at); }, biome);
}

[[nodiscard]] inline auto paint_of(const terrain_biome& biome, const column_facts& column)
    -> column_paint {
    return std::visit([&column](const auto& b) -> column_paint { return b.paint_at(column); }, biome);
}

[[nodiscard]] inline auto woods_of(const terrain_biome& biome) -> const forest& {
    return std::visit([](const auto& b) -> const forest& { return b.woods; }, biome);
}

template <class Visit>
auto visit_biome_fields(terrain_biome& biome, Visit&& visit) -> void {
    std::visit([&](auto& b) { std::remove_cvref_t<decltype(b)>::visit_fields(b, visit); }, biome);
}

template <class Visit>
auto visit_biome_fields(const terrain_biome& biome, Visit&& visit) -> void {
    std::visit([&](const auto& b) { std::remove_cvref_t<decltype(b)>::visit_fields(b, visit); }, biome);
}

auto lend_tones(const terrain_biome& biome, column_facts& column) -> void;

}  // namespace vw::ecs
