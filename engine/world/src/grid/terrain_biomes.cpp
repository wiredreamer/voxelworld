module vw.world;

import std;
import vw.core;

namespace vw::ecs {

namespace {

constexpr float64 wave_stretch = 2.5;

constexpr float64 grass_edge     = 0.08;
constexpr float64 grass_lush_share = 0.5;
constexpr float64 flower_bed_cells = 12.0;

[[nodiscard]] auto column_hash(float64 x, float64 z, uint64 salt) -> uint64 {
    auto h = (static_cast<uint64>(static_cast<int64>(std::floor(x))) * 0x9E3779B97F4A7C15ULL) ^
             (static_cast<uint64>(static_cast<int64>(std::floor(z))) * 0xC2B2AE3D27D4EB4FULL) ^ salt;
    h ^= h >> 30U;
    h *= 0xBF58476D1CE4E5B9ULL;
    h ^= h >> 27U;
    h *= 0x94D049BB133111EBULL;
    h ^= h >> 31U;
    return h;
}

[[nodiscard]] auto column_unit(float64 x, float64 z, uint64 salt) -> float64 {
    return static_cast<float64>(column_hash(x, z, salt) >> 11U) * 0x1.0p-53;
}

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

namespace {

[[nodiscard]] auto lushness_at(
    const column_facts& column, const grass_cover& cover
) -> std::optional<float64> {
    const float64 f = cover.patch_frequency;
    const float64 n = column.noise->fractal((column.x * f) + 1009.0, (column.z * f) - 1009.0, 2);
    const float64 t = 0.5 + (0.5 * std::tanh(n * wave_stretch));

    const float64 threshold = ((1.0 - static_cast<float64>(cover.density)) * (1.0 + (2.0 * grass_edge))) - grass_edge;
    const float64 chance    = smoothstep(threshold - grass_edge, threshold + grass_edge, t);
    if (column_unit(column.x, column.z, 0x6772617373ULL) >= chance) {
        return std::nullopt;
    }
    const float64 run = std::max((1.0 - threshold) * grass_lush_share, 0.02);
    return std::clamp((t - threshold) / run, 0.0, 1.0);
}

}  // namespace

// см. docs/world.md#покров
auto cover_form_at(
    const column_facts& column, const grass_cover& cover
) -> uint8 {
    const auto lushness = lushness_at(column, cover);
    if (!lushness) {
        return 0;
    }

    const auto height_class = static_cast<uint8>(
        std::min<int32>(grass_height_classes - 1, static_cast<int32>(*lushness * grass_height_classes))
    );

    if (column_unit(column.x, column.z, 0x666C6F776572ULL) < static_cast<float64>(cover.flower_share)) {
        const float64 bed_x = std::floor(column.x / flower_bed_cells);
        const float64 bed_z = std::floor(column.z / flower_bed_cells);
        const auto color    = static_cast<uint8>(column_hash(bed_x, bed_z, 0x636F6C6FULL) % flower_colors);
        const auto bunch    = static_cast<uint8>(column_hash(column.x, column.z, 0x62756E63ULL) % flower_bunch_count);
        return flower_form_of(color, bunch);
    }

    const auto layout = static_cast<uint8>(column_hash(column.x, column.z, 0x666F726DULL) % grass_layouts);
    return grass_form_of(height_class, layout);
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
