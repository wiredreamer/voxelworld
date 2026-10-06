module vw.world;

import std;
import vw.core;

namespace vw::ecs {

// см. docs/world.md#plains
auto plains_biome::height_at(
    const biome_point& at
) const -> float64 {
    return static_cast<float64>(base_height) + (wave_at(shape, *at.noise, at.x, at.z, 31.0) * height);
}

auto plains_biome::paint_at(
    const column_facts& column
) const -> column_paint {
    column_paint paint;
    paint.add(tone_at(column, grass, "grass", 401.0), 1);
    paint.add(tone_at(column, dirt, "dirt", 613.0), dirt_depth);
    paint.cover = cover_form_at(column, cover);
    return paint;
}

}  // namespace vw::ecs
