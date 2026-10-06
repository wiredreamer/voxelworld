export module vw.world:grid.cell;

import std;

import vw.core;

export namespace vw::ecs {

inline constexpr uint8 grass_height_classes = 3;
inline constexpr uint8 grass_layouts        = 2;
inline constexpr uint8 grass_form_count     = grass_height_classes * grass_layouts;

[[nodiscard]] constexpr auto grass_form_of(uint8 height_class, uint8 layout) -> uint8 {
    return static_cast<uint8>(1U + (height_class * grass_layouts) + layout);
}

inline constexpr uint8 flower_colors     = 5;
inline constexpr std::array<uint8, 2> flower_bunches{1, 3};
inline constexpr auto flower_bunch_count = static_cast<uint8>(flower_bunches.size());
inline constexpr uint8 cover_form_count  = grass_form_count + (flower_colors * flower_bunch_count);

[[nodiscard]] constexpr auto grass_height_class_of(uint8 form) -> uint8 {
    return static_cast<uint8>((form - 1U) / grass_layouts);
}

// см. docs/world.md#цветы
[[nodiscard]] constexpr auto flower_form_of(uint8 color, uint8 bunch) -> uint8 {
    return static_cast<uint8>(grass_form_count + 1U + (color * flower_bunch_count) + bunch);
}

[[nodiscard]] constexpr auto is_flower_form(uint8 form) -> bool {
    return form > grass_form_count && form <= cover_form_count;
}

[[nodiscard]] constexpr auto flower_color_of(uint8 form) -> uint8 {
    return static_cast<uint8>((form - grass_form_count - 1U) / flower_bunch_count);
}

[[nodiscard]] constexpr auto flower_count_of(uint8 form) -> uint8 {
    return flower_bunches[(form - grass_form_count - 1U) % flower_bunch_count];
}

enum class occupant_kind : uint8 {
    empty,
    terrain,
    grass,
    flower,
};

struct cell_traits {
    bool solid  = false;
    bool opaque = false;
    bool shades = false;

    auto operator==(const cell_traits&) const -> bool = default;
};

// см. docs/world.md#клетка
[[nodiscard]] constexpr auto traits_of(occupant_kind kind) -> cell_traits {
    switch (kind) {
        case occupant_kind::terrain:
            return {.solid = true, .opaque = true, .shades = true};
        case occupant_kind::empty:
        case occupant_kind::grass:
        case occupant_kind::flower:
            return {};
    }
    return {};
}

struct cell {
    occupant_kind kind = occupant_kind::empty;
    voxel look         = voxels::air;
    uint8 form         = 0;

    [[nodiscard]] auto traits() const -> cell_traits {
        return traits_of(kind);
    }

    [[nodiscard]] auto is_empty() const -> bool {
        return kind == occupant_kind::empty;
    }

    auto operator==(const cell&) const -> bool = default;
};

}  // namespace vw::ecs
