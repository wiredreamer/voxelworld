module vw.world;

import std;
import vw.core;

namespace vw::ecs {

namespace {

using P = terrain_params;

struct terrain_number_field {
    std::string_view key;
    std::variant<float32 terrain_params::*, int32 terrain_params::*> member;
    float32 min = 0.0F;
    float32 max = 1.0F;
};

constexpr std::array number_fields = {
    terrain_number_field{"region_spacing_voxels", &P::region_spacing_voxels, 256.0F, 4096.0F},
    terrain_number_field{"region_jitter", &P::region_jitter, 0.0F, 0.45F},
    terrain_number_field{"region_warp_voxels", &P::region_warp_voxels, 0.0F, 200.0F},
    terrain_number_field{"region_warp_frequency", &P::region_warp_frequency, 0.0005F, 0.02F},
    terrain_number_field{"island_taper_voxels", &P::island_taper_voxels, 8.0F, 512.0F},
    terrain_number_field{"island_min_thickness", &P::island_min_thickness, 1.0F, 64.0F},
    terrain_number_field{"island_jag_voxels", &P::island_jag_voxels, 0.0F, 40.0F},
    terrain_number_field{"island_jag_frequency", &P::island_jag_frequency, 0.002F, 0.1F},
    terrain_number_field{"relief_warp_frequency", &P::relief_warp_frequency, 0.0005F, 0.02F},
    terrain_number_field{"relief_warp_voxels", &P::relief_warp_voxels, 0.0F, 120.0F},
    terrain_number_field{"landscape_frequency", &P::landscape_frequency, 0.0002F, 0.01F},
    terrain_number_field{"moisture_frequency", &P::moisture_frequency, 0.0002F, 0.02F},
    terrain_number_field{"biome_blend", &P::biome_blend, 0.01F, 0.5F},
    terrain_number_field{"tone_blend", &P::tone_blend, 0.01F, 1.0F},
};

struct terrain_flag_field {
    std::string_view key;
    bool terrain_params::* member;
};

constexpr std::array flag_fields = {
    terrain_flag_field{"island", &P::island},
    terrain_flag_field{"caves", &P::caves},
    terrain_flag_field{"plants", &P::plants},
};

auto failure(const json::cursor& at, std::string_view message) -> std::unexpected<std::string> {
    return std::unexpected{std::format("{}: {}", at.path(), message)};
}

auto plain_number(float32 value) -> json::value {
    float64 shortest = 0.0;
    const std::string text = std::format("{}", value);
    std::from_chars(text.data(), text.data() + text.size(), shortest);
    return json::value{shortest};
}

auto parse_world(const json::cursor& world, terrain_params& out) -> std::expected<void, std::string> {
    const auto fields = world.fields();
    if (!fields) {
        return std::unexpected{json::describe(fields.error())};
    }

    for (const auto& [key, item] : *fields) {
        if (const auto flag = std::ranges::find(flag_fields, key, &terrain_flag_field::key);
            flag != flag_fields.end()) {
            const auto value = item.boolean();
            if (!value) {
                return std::unexpected{json::describe(value.error())};
            }
            out.*(flag->member) = *value;
            continue;
        }

        const auto field = std::ranges::find(number_fields, key, &terrain_number_field::key);
        if (field == number_fields.end()) {
            return failure(item, "unknown setting");
        }
        const auto value = item.number();
        if (!value) {
            return std::unexpected{json::describe(value.error())};
        }
        std::visit(
            [&](auto member) {
                using field_type = std::remove_reference_t<decltype(out.*member)>;
                if constexpr (std::is_same_v<field_type, int32>) {
                    out.*member = static_cast<int32>(std::lround(*value));
                } else {
                    out.*member = static_cast<float32>(*value);
                }
            },
            field->member
        );
    }
    return {};
}

auto read_value(const json::cursor& item, float32& out, const voxel_registry&)
    -> std::expected<void, std::string> {
    const auto value = item.number();
    if (!value) {
        return std::unexpected{json::describe(value.error())};
    }
    out = static_cast<float32>(*value);
    return {};
}

auto read_value(const json::cursor& item, int32& out, const voxel_registry&)
    -> std::expected<void, std::string> {
    const auto value = item.number();
    if (!value) {
        return std::unexpected{json::describe(value.error())};
    }
    out = static_cast<int32>(std::lround(*value));
    return {};
}

auto row_named(std::string_view name) -> const voxel_group* {
    const auto found = std::ranges::find(voxels::groups, name, &voxel_group::name);
    return found == voxels::groups.end() ? nullptr : &*found;
}

auto row_name(const voxel_span& row) -> std::string_view {
    const auto found = std::ranges::find(voxels::groups, row.first, &voxel_group::first);
    return found == voxels::groups.end() ? std::string_view{"?"} : found->name;
}

auto read_value(const json::cursor& item, tone_ramp& out, const voxel_registry& voxels)
    -> std::expected<void, std::string> {
    const auto fields = item.fields();
    if (!fields) {
        return std::unexpected{json::describe(fields.error())};
    }

    for (const auto& [key, value] : *fields) {
        if (key == "row") {
            const auto name = value.string();
            if (!name) {
                return std::unexpected{json::describe(name.error())};
            }
            const voxel_group* row = row_named(*name);
            if (row == nullptr) {
                std::string known;
                for (const auto& group : voxels::groups) {
                    known += std::format("{}{}", known.empty() ? "" : ", ", group.name);
                }
                return failure(value, std::format("no palette row named '{}', known: {}", *name, known));
            }
            out.row = voxel_span{row->first.value, row->count};
            continue;
        }

        if (key == "material") {
            const auto name = value.string();
            if (!name) {
                return std::unexpected{json::describe(name.error())};
            }
            const auto found = default_material_table().find(*name);
            if (!found) {
                std::string known;
                for (const material_type& row : default_material_table().named()) {
                    known += std::format("{}{}", known.empty() ? "" : ", ", row.name);
                }
                return failure(value, std::format("no material named '{}', known: {}", *name, known));
            }
            out.made_of = *found;
            continue;
        }

        float32* number = key == "from"             ? &out.from
                        : key == "to"               ? &out.to
                        : key == "spot_frequency" ? &out.spot_frequency
                        : key == "spot_contrast"  ? &out.spot_contrast
                                                  : nullptr;
        if (number == nullptr) {
            return failure(
                value,
                "unknown ramp setting, expected row, material, from, to, spot_frequency or spot_contrast"
            );
        }
        const auto read = read_value(value, *number, voxels);
        if (!read) {
            return read;
        }
    }
    return {};
}

auto parse_biome(const json::cursor& entry, terrain_biome& biome, const voxel_registry& voxels)
    -> std::expected<void, std::string> {
    const auto fields = entry.fields();
    if (!fields) {
        return std::unexpected{json::describe(fields.error())};
    }

    for (const auto& [key, item] : *fields) {
        bool known = false;
        std::expected<void, std::string> read{};
        visit_biome_fields(biome, [&](std::string_view name, auto& value, auto...) {
            if (known || name != key) {
                return;
            }
            known = true;
            read  = read_value(item, value, voxels);
        });
        if (!known) {
            return failure(item, std::format("unknown setting of biome '{}'", biome_name(biome)));
        }
        if (!read) {
            return read;
        }
    }
    return {};
}

auto parse_biomes(
    const json::cursor& section, std::vector<terrain_biome>& biomes, const voxel_registry& voxels
) -> std::expected<void, std::string> {
    const auto entries = section.fields();
    if (!entries) {
        return std::unexpected{json::describe(entries.error())};
    }

    for (const auto& [key, entry] : *entries) {
        const auto biome = std::ranges::find(biomes, key, biome_name);
        if (biome == biomes.end()) {
            std::string known;
            for (const auto& each : biomes) {
                known += std::format("{}{}", known.empty() ? "" : ", ", biome_name(each));
            }
            return failure(entry, std::format("no biome named '{}', known: {}", key, known));
        }
        const auto parsed = parse_biome(entry, *biome, voxels);
        if (!parsed) {
            return parsed;
        }
    }
    return {};
}

auto write_value(float32 value, const voxel_registry&) -> json::value {
    return plain_number(value);
}

auto write_value(int32 value, const voxel_registry&) -> json::value {
    return json::value{value};
}

auto write_value(const tone_ramp& ramp, const voxel_registry&) -> json::value {
    json::object out;
    out.set("row", json::value{std::string{row_name(ramp.row)}});
    if (ramp.made_of != materials::inert) {
        out.set("material", json::value{std::string{default_material_table().get(ramp.made_of).name}});
    }
    out.set("from", plain_number(ramp.from));
    out.set("to", plain_number(ramp.to));
    out.set("spot_frequency", plain_number(ramp.spot_frequency));
    out.set("spot_contrast", plain_number(ramp.spot_contrast));
    return json::value{std::move(out)};
}

auto dump_biome(const terrain_biome& biome, const voxel_registry& voxels) -> std::optional<json::value> {
    const terrain_biome stock = std::visit(
        [](const auto& tuned) -> terrain_biome { return std::remove_cvref_t<decltype(tuned)>{}; }, biome
    );

    std::vector<std::string> stock_values;
    visit_biome_fields(stock, [&](std::string_view, const auto& value, auto...) {
        stock_values.push_back(json::dump(write_value(value, voxels), {}));
    });

    json::object entry;
    bool changed      = false;
    std::size_t index = 0;
    visit_biome_fields(biome, [&](std::string_view key, const auto& value, auto...) {
        json::value written = write_value(value, voxels);
        if (json::dump(written, {}) != stock_values[index]) {
            entry.set(std::string{key}, std::move(written));
            changed = true;
        }
        ++index;
    });

    if (!changed) {
        return std::nullopt;
    }
    return json::value{std::move(entry)};
}

}  // namespace

auto terrain_number_count() -> std::size_t {
    return number_fields.size();
}

auto terrain_number(terrain_params& params, std::size_t index) -> terrain_number_slot {
    const auto& field = number_fields[index];
    return {
        .key   = field.key,
        .value = std::visit(
            [&params](auto member) -> std::variant<float32*, int32*> { return &(params.*member); },
            field.member
        ),
        .min = field.min,
        .max = field.max,
    };
}

auto terrain_flag_count() -> std::size_t {
    return flag_fields.size();
}

auto terrain_flag_key(std::size_t index) -> std::string_view {
    return flag_fields[index].key;
}

auto terrain_flag(terrain_params& params, std::size_t index) -> bool& {
    return params.*(flag_fields[index].member);
}

auto parse_terrain_settings(
    std::string_view text, const voxel_registry& voxels, terrain_params base
) -> std::expected<terrain_params, std::string> {
    const auto document = json::parse(text);
    if (!document) {
        return std::unexpected{json::describe(document.error())};
    }

    const json::cursor root{*document};
    const auto sections = root.fields();
    if (!sections) {
        return std::unexpected{json::describe(sections.error())};
    }

    for (const auto& [key, section] : *sections) {
        if (key == "world") {
            const auto parsed = parse_world(section, base);
            if (!parsed) {
                return std::unexpected{parsed.error()};
            }
            continue;
        }

        if (key == "biomes") {
            const auto parsed = parse_biomes(section, base.biomes, voxels);
            if (!parsed) {
                return std::unexpected{parsed.error()};
            }
            continue;
        }

        return failure(section, "unknown section, expected 'world' or 'biomes'");
    }

    return base;
}

auto load_terrain_settings(
    const std::filesystem::path& file, const voxel_registry& voxels, terrain_params base
) -> std::expected<terrain_params, std::string> {
    std::ifstream stream{file, std::ios::binary};
    if (!stream) {
        return std::unexpected{std::format("cannot open {}", file.generic_string())};
    }

    const std::string text{std::istreambuf_iterator<char>{stream}, {}};
    auto params = parse_terrain_settings(text, voxels, std::move(base));
    if (!params) {
        return std::unexpected{std::format("{}: {}", file.generic_string(), params.error())};
    }
    return params;
}

auto dump_terrain_settings(
    const terrain_params& params, const voxel_registry& voxels
) -> std::string {
    const terrain_params stock{};

    json::object world;
    for (const auto& flag : flag_fields) {
        if (params.*(flag.member) != stock.*(flag.member)) {
            world.set(std::string{flag.key}, json::value{params.*(flag.member)});
        }
    }
    for (const auto& field : number_fields) {
        std::visit(
            [&](auto member) {
                if (params.*member != stock.*member) {
                    world.set(std::string{field.key}, write_value(params.*member, voxels));
                }
            },
            field.member
        );
    }

    json::object biomes;
    for (const auto& biome : params.biomes) {
        if (auto entry = dump_biome(biome, voxels)) {
            biomes.set(std::string{biome_name(biome)}, std::move(*entry));
        }
    }

    json::object root;
    root.set("world", json::value{std::move(world)});
    root.set("biomes", json::value{std::move(biomes)});
    return json::dump(json::value{std::move(root)}, {.indent = 2});
}

auto save_terrain_settings(
    const std::filesystem::path& file, const terrain_params& params, const voxel_registry& voxels
) -> std::expected<void, std::string> {
    std::ofstream stream{file, std::ios::binary | std::ios::trunc};
    if (!stream) {
        return std::unexpected{std::format("cannot write {}", file.generic_string())};
    }
    stream << dump_terrain_settings(params, voxels) << '\n';
    if (!stream) {
        return std::unexpected{std::format("cannot write {}", file.generic_string())};
    }
    return {};
}

}  // namespace vw::ecs
