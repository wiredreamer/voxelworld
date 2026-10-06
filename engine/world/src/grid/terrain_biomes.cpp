module vw.world;

import std;
import vw.core;

namespace vw::ecs {

namespace {

constexpr float64 wave_stretch = 2.5;

[[nodiscard]] auto same_row(const tone_ramp& a, const tone_ramp& b) -> bool {
    return a.row.first == b.row.first && a.row.count == b.row.count;
}

}  // namespace

// см. docs/world.md#волна
auto wave_at(
    const wave& shape, const perlin_noise& noise, float64 x, float64 z, float64 salt
) -> float64 {
    const float64 f = shape.frequency;
    const float64 n = noise.fractal(
        (x * f) + salt, (z * f) + (salt * 0.618), std::max(1, shape.octaves), shape.persistence
    );
    return 0.5 + (0.5 * std::tanh(n * wave_stretch));
}

// см. docs/world.md#цвет-слоёв
auto tone_at(
    const column_facts& column, const tone_ramp& own, std::string_view role, float64 salt
) -> voxel {
    float64 from      = own.from;
    float64 to        = own.to;
    float64 frequency = own.spot_frequency;
    float64 contrast  = own.spot_contrast;

    const auto share = static_cast<float64>(column.neighbour_share);
    for (uint8 index = 0; index < column.neighbours; ++index) {
        const neighbour_tone& other = column.neighbour[index];
        if (other.role != role || !same_row(own, *other.ramp)) {
            continue;
        }
        from += (other.ramp->from - from) * share;
        to += (other.ramp->to - to) * share;
        frequency += (other.ramp->spot_frequency - frequency) * share;
        contrast += (other.ramp->spot_contrast - contrast) * share;
        break;
    }

    const float64 n = column.noise->fractal((column.x * frequency) + salt, (column.z * frequency) - salt, 2);
    const float64 t = 0.5 + (0.5 * std::tanh(n * contrast));

    const auto last  = static_cast<int64>(own.row.count) - 1;
    const auto index = std::clamp(static_cast<int64>(std::lround(from + (t * (to - from)))), int64{0}, last);
    return voxel{static_cast<uint8>(own.row.first.value + index)};
}

auto lend_tones(
    const terrain_biome& biome, column_facts& column
) -> void {
    visit_biome_fields(biome, [&column](std::string_view key, const auto& value, auto...) {
        if constexpr (std::is_same_v<std::remove_cvref_t<decltype(value)>, tone_ramp>) {
            if (column.neighbours < column_facts::tone_capacity) {
                column.neighbour[column.neighbours] = {.role = key, .ramp = &value};
                ++column.neighbours;
            }
        }
    });
}

auto default_biomes() -> std::vector<terrain_biome> {
    return {plains_biome{}, hills_biome{}};
}

}  // namespace vw::ecs
