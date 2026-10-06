#include <catch2/catch_test_macros.hpp>

import std;

import vw.core;
import vw.asset;
import vw.ecs;
import vw.world;

using namespace vw;
using namespace vw::ecs;

namespace {

class sampled_column {
public:
    static constexpr int32 chunk = 64;

    sampled_column(perlin_terrain_generator& gen, int32 cx, int32 cz) {
        ecs::gen_column column{cx, cz};

        terrain_context ctx{
            .cx           = cx,
            .cz           = cz,
            .create_chunk = [&column](int32 y) -> chunk_data& {
                return column.create_chunk(y, chunk_data{});
            },
        };
        gen.generate(ctx);

        for (auto& [cy, data] : column.get_all_chunk_data()) {
            models_[cy] = data.volume->shared_voxels();
            min_y_      = std::min(min_y_, cy * chunk);
            max_y_      = std::max(max_y_, (cy * chunk) + chunk - 1);
        }
    }

    [[nodiscard]] auto bottom() const -> int32 {
        return min_y_;
    }

    [[nodiscard]] auto top() const -> int32 {
        return max_y_;
    }

    [[nodiscard]] auto id_at(int32 x, int32 wy, int32 z) const -> voxel {
        const int32 cy = wy >= 0 ? wy / chunk : ((wy - chunk) + 1) / chunk;

        const auto it = models_.find(cy);
        if (it == models_.end()) {
            return voxels::air;
        }
        return it->second->get_voxel(x, wy - (cy * chunk), z);
    }

    [[nodiscard]] auto surface_of(int32 x, int32 z) const -> int32 {
        for (int32 wy = max_y_; wy >= min_y_; --wy) {
            if (id_at(x, wy, z) != voxels::air) {
                return wy;
            }
        }
        return min_y_ - 1;
    }

private:
    std::unordered_map<int32, std::shared_ptr<asset::model>> models_;
    int32 min_y_ = std::numeric_limits<int32>::max();
    int32 max_y_ = std::numeric_limits<int32>::lowest();
};

auto settled_params() -> perlin_terrain_generator::params {
    perlin_terrain_generator::params p{};
    p.caves  = false;
    p.island = false;

    hills_biome settled{};
    settled.grass           = {.row = voxels::green, .from = 4.0F, .to = 4.0F};
    settled.dirt            = {.row = voxels::brown, .from = 0.0F, .to = 0.0F};
    settled.stone           = {.row = voxels::gray, .from = 10.0F, .to = 10.0F};
    settled.height          = 120.0F;
    settled.shape.frequency = 0.02F;
    p.biomes                = {settled};
    return p;
}

auto column_has_turf(const sampled_column& column, int32 x, int32 z) -> bool {
    return column.id_at(x, column.surface_of(x, z), z) == voxels::green[4];
}

}  // namespace

TEST_CASE("ground is layers, not paint", "[world][surface]") {
    const auto p = settled_params();

    asset::model_identity_pool identity_pool;
    asset::page_pool pages;
    perlin_terrain_generator gen{identity_pool, pages, p};

    const sampled_column column{gen, 0, 0};

    REQUIRE(column.bottom() == p.world_bottom_y);

    int32 with_soil = 0;
    int32 bare_rock = 0;

    for (int32 x = 0; x < sampled_column::chunk; ++x) {
        for (int32 z = 0; z < sampled_column::chunk; ++z) {
            const int32 surface = column.surface_of(x, z);
            INFO("column " << x << "," << z << " surface at " << surface);
            REQUIRE(surface >= p.world_bottom_y);

            REQUIRE(column.id_at(x, surface + 1, z) == voxels::air);

            bool unbroken = true;
            for (int32 wy = p.world_bottom_y; wy <= surface; ++wy) {
                unbroken = unbroken && (column.id_at(x, wy, z) != voxels::air);
            }
            REQUIRE(unbroken);

            const auto crown = column.id_at(x, surface, z);

            if (crown == voxels::green[4]) {
                ++with_soil;

                REQUIRE(column.id_at(x, surface - 1, z) == voxels::brown[0]);
                continue;
            }

            REQUIRE(crown == voxels::gray[10]);
            ++bare_rock;

            REQUIRE(column.id_at(x, surface - 1, z) == voxels::gray[10]);
        }
    }

    INFO("soil columns " << with_soil << ", bare rock " << bare_rock);
    REQUIRE(with_soil > 0);
    REQUIRE(bare_rock > 0);
}

TEST_CASE("rock changes with absolute depth", "[world][surface]") {
    const auto p = settled_params();

    asset::model_identity_pool identity_pool;
    asset::page_pool pages;
    perlin_terrain_generator gen{identity_pool, pages, p};

    const sampled_column column{gen, 3, 5};

    struct probe {
        int32 wy;
        voxel expected;
    };

    for (const auto& [wy, expected] : {
             probe{p.world_bottom_y, voxels::gray[0]},
             probe{p.world_bottom_y + p.bedrock_thickness - 1, voxels::gray[0]},
             probe{p.world_bottom_y + p.bedrock_thickness, voxels::gray[2]},
             probe{p.rock_bottom_y - 1, voxels::gray[2]},
             probe{p.rock_bottom_y, voxels::gray[4]},
             probe{p.rock_deep_y - 1, voxels::gray[4]},
             probe{p.rock_deep_y, voxels::gray[6]},
         }) {
        INFO("at height " << wy);
        REQUIRE(column.id_at(0, wy, 0) == expected);
        REQUIRE(column.id_at(37, wy, 21) == expected);
    }
}

TEST_CASE("hills turn to stone only where they are steep", "[world][surface]") {
    auto p = settled_params();

    asset::model_identity_pool identity_pool;
    asset::page_pool pages;

    std::get<hills_biome>(p.biomes[0]).stone_slope = 1.0e6F;

    perlin_terrain_generator gentle{identity_pool, pages, p};
    perlin_terrain_generator strict{identity_pool, pages, settled_params()};

    int32 soft_soil = 0;
    int32 hard_soil = 0;

    for (int32 cx = 6; cx < 9; ++cx) {
        for (int32 cz = 1; cz < 4; ++cz) {
            const sampled_column soft{gentle, cx, cz};
            const sampled_column hard{strict, cx, cz};

            for (int32 x = 0; x < sampled_column::chunk; ++x) {
                for (int32 z = 0; z < sampled_column::chunk; ++z) {
                    soft_soil += column_has_turf(soft, x, z) ? 1 : 0;
                    hard_soil += column_has_turf(hard, x, z) ? 1 : 0;
                }
            }
        }
    }

    INFO("turf with the limit lifted " << soft_soil << ", with it in place " << hard_soil);
    REQUIRE(soft_soil > hard_soil);
}

TEST_CASE("the home region sits on the origin and its edge distance runs out at the border", "[world][regions]") {
    const region_map regions{region_map::params{}};
    const region_map same{region_map::params{}};

    const region_sample centre = regions.sample(0.0, 0.0);
    REQUIRE(region_map::is_home(centre.cell));
    REQUIRE(centre.edge_distance > 300.0);

    for (const auto& [dx, dz] : {std::pair{1.0, 0.0}, std::pair{0.0, -1.0}, std::pair{-0.7, 0.7}}) {
        INFO("heading " << dx << "," << dz);
        float64 last_inside = -1.0;
        bool crossed = false;

        for (float64 step = 8.0; step < 4096.0 && !crossed; step += 8.0) {
            const region_sample at = regions.sample(dx * step, dz * step);
            const region_sample again = same.sample(dx * step, dz * step);
            REQUIRE(at.cell == again.cell);
            REQUIRE(at.edge_distance == again.edge_distance);

            if (!region_map::is_home(at.cell)) {
                crossed = true;
                break;
            }
            REQUIRE(at.edge_distance >= 0.0);
            last_inside = at.edge_distance;
        }

        REQUIRE(crossed);
        REQUIRE(last_inside < 16.0);
    }
}

TEST_CASE("past the edge of the home region there is no ground at all", "[world][regions]") {
    asset::model_identity_pool identity_pool;
    asset::page_pool pages;
    const perlin_terrain_generator::params p{};
    perlin_terrain_generator gen{identity_pool, pages, p};

    REQUIRE(gen.surface_height_at(0, 0).has_value());

    const auto far = static_cast<int32>(p.region_spacing_voxels * 2.0F);
    REQUIRE_FALSE(gen.surface_height_at(far, 0).has_value());
    REQUIRE_FALSE(gen.surface_height_at(0, -far).has_value());

    perlin_terrain_generator::params endless = p;
    endless.island = false;
    perlin_terrain_generator open{identity_pool, pages, endless};
    REQUIRE(open.surface_height_at(far, 0).has_value());
}

TEST_CASE("dump the surface of the home region", "[.terrain_map]") {
    const char* out = std::getenv("VW_TERRAIN_MAP");
    REQUIRE(out != nullptr);

    const auto settings = load_terrain_settings(
        std::filesystem::path{VW_ASSET_DIR} / "data" / "world_gen.json", default_voxel_registry(),
        terrain_params{}
    );
    REQUIRE(settings.has_value());

    asset::model_identity_pool identity_pool;
    asset::page_pool pages;
    perlin_terrain_generator gen{identity_pool, pages, *settings};

    constexpr int32 half = 800;
    constexpr int32 step = 2;
    constexpr int32 side = (2 * half) / step;

    std::vector<int16> heights;
    std::vector<uint8> tops;
    heights.reserve(static_cast<std::size_t>(side) * side);
    tops.reserve(static_cast<std::size_t>(side) * side);
    for (int32 z = -half; z < half; z += step) {
        for (int32 x = -half; x < half; x += step) {
            heights.push_back(static_cast<int16>(gen.surface_height_at(x, z).value_or(-1000)));
            tops.push_back(static_cast<uint8>(gen.surface_voxel_at(x, z).value_or(voxels::air).value));
        }
    }

    std::ofstream file{out, std::ios::binary};
    file.write(reinterpret_cast<const char*>(heights.data()),
               static_cast<std::streamsize>(heights.size() * sizeof(int16)));
    file.write(reinterpret_cast<const char*>(tops.data()),
               static_cast<std::streamsize>(tops.size()));
    REQUIRE(file.good());
}

TEST_CASE("a step up has no trench dug in front of it", "[world][surface]") {
    asset::model_identity_pool identity_pool;
    asset::page_pool pages;
    perlin_terrain_generator::params p{};
    for (auto& biome : p.biomes) {
        std::visit([](auto& b) { b.shape.frequency = 0.004F; }, biome);
    }
    perlin_terrain_generator gen{identity_pool, pages, p};

    constexpr int32 reach = 384;
    int32 steps    = 0;
    int32 trenches = 0;

    for (int32 z = -reach; z < reach; z += 3) {
        std::vector<int32> row;
        row.reserve(static_cast<std::size_t>(2 * reach));
        for (int32 x = -reach; x < reach; ++x) {
            row.push_back(gen.surface_height_at(x, z).value_or(0));
        }
        for (std::size_t i = 1; i + 2 < row.size(); ++i) {
            const int32 before = row[i - 1];
            const int32 here   = row[i];
            const int32 next   = row[i + 1];
            steps += std::abs(next - here) == 1 ? 1 : 0;
            const bool dip = here < before && here < next;
            trenches += dip ? 1 : 0;
        }
    }

    INFO("steps " << steps << ", one column dips " << trenches);
    REQUIRE(steps > 100);
    REQUIRE(trenches * 1000 < steps);
}

TEST_CASE("the world settings file reads and writes back the same settings", "[world][settings]") {
    const auto& voxels = default_voxel_registry();
    const auto loaded  = load_terrain_settings(
        std::filesystem::path{VW_ASSET_DIR} / "data" / "world_gen.json", voxels, terrain_params{}
    );
    REQUIRE(loaded.has_value());
    REQUIRE(loaded->biomes.size() == 2);
    REQUIRE(std::get<plains_biome>(loaded->biomes[0]).grass.to > std::get<plains_biome>(loaded->biomes[0]).grass.from);

    const std::string text = dump_terrain_settings(*loaded, voxels);
    const auto again       = parse_terrain_settings(text, voxels, terrain_params{});
    REQUIRE(again.has_value());
    REQUIRE(dump_terrain_settings(*again, voxels) == text);
    REQUIRE(again->region_spacing_voxels == loaded->region_spacing_voxels);
    REQUIRE(std::get<hills_biome>(again->biomes[1]).stone == std::get<hills_biome>(loaded->biomes[1]).stone);
}

TEST_CASE("the settings file keeps only what differs from the code", "[world][settings]") {
    const auto& voxels = default_voxel_registry();

    REQUIRE(dump_terrain_settings(terrain_params{}, voxels).find("height") == std::string::npos);

    const auto tuned = parse_terrain_settings(
        R"({"world": {"biome_blend": 0.2}, "biomes": {"plains": {"height": 9,
            "grass": {"row": "amber", "from": 2, "to": 3}}}})",
        voxels, terrain_params{}
    );
    REQUIRE(tuned.has_value());

    const auto& plains = std::get<plains_biome>(tuned->biomes[0]);
    REQUIRE(plains.height == 9.0F);
    REQUIRE(plains.grass.row.first == voxels::amber[0]);
    REQUIRE(plains.grass.from == 2.0F);
    REQUIRE(plains.grass.to == 3.0F);
    REQUIRE(plains.grass.spot_contrast == plains_biome{}.grass.spot_contrast);
    REQUIRE(std::get<hills_biome>(tuned->biomes[1]).height == hills_biome{}.height);

    const std::string text = dump_terrain_settings(*tuned, voxels);
    REQUIRE(text.find("\"height\"") != std::string::npos);
    REQUIRE(text.find("hills") == std::string::npos);
    REQUIRE(text.find("\"frequency\"") == std::string::npos);
}

TEST_CASE("a typo in the world settings is refused with its place", "[world][settings]") {
    const auto& voxels = default_voxel_registry();

    const auto key = parse_terrain_settings(R"({"world": {"hils_from": 0.3}})", voxels, terrain_params{});
    REQUIRE_FALSE(key.has_value());
    REQUIRE(key.error().find("hils_from") != std::string::npos);

    const auto shade = parse_terrain_settings(
        R"({"biomes": {"plains": {"grass": {"row": "mos"}}}})", voxels, terrain_params{}
    );
    REQUIRE_FALSE(shade.has_value());
    REQUIRE(shade.error().find("mos") != std::string::npos);
    REQUIRE(shade.error().find("green") != std::string::npos);

    const auto biome = parse_terrain_settings(R"({"biomes": {"swamp": {}}})", voxels, terrain_params{});
    REQUIRE_FALSE(biome.has_value());
    REQUIRE(biome.error().find("swamp") != std::string::npos);
    REQUIRE(biome.error().find("plains") != std::string::npos);

    const auto setting = parse_terrain_settings(
        R"({"biomes": {"plains": {"stone_slope": 4}}})", voxels, terrain_params{}
    );
    REQUIRE_FALSE(setting.has_value());
    REQUIRE(setting.error().find("stone_slope") != std::string::npos);
}

TEST_CASE("grass spots step through neighbouring shades only", "[world][surface]") {
    const perlin_noise noise{7};
    const tone_ramp ramp{.row = voxels::green, .from = 2.0F, .to = 8.0F};
    const column_facts column{.noise = &noise};

    std::set<uint8> seen;
    voxel last = voxels::air;
    for (int32 x = 0; x < 2000; ++x) {
        column_facts at = column;
        at.x            = static_cast<float64>(x);
        at.z            = 17.0;
        const voxel shade = tone_at(at, ramp, "grass", 401.0);
        REQUIRE(voxels::green.contains(shade));
        if (last != voxels::air) {
            REQUIRE(std::abs(static_cast<int32>(shade.value) - static_cast<int32>(last.value)) <= 1);
        }
        seen.insert(shade.value);
        last = shade;
    }
    REQUIRE(seen.size() >= 4);
}

TEST_CASE("both sides of a biome border pick the same grass on the border", "[world][surface]") {
    const perlin_noise noise{7};
    const tone_ramp light{.row = voxels::green, .from = 6.0F, .to = 10.0F};
    const tone_ramp dark{.row = voxels::green, .from = 2.0F, .to = 6.0F};

    for (int32 x = 0; x < 200; x += 7) {
        column_facts from_light{.noise = &noise, .x = static_cast<float64>(x), .z = 3.0};
        from_light.neighbour[0]    = {.role = "grass", .ramp = &dark};
        from_light.neighbours      = 1;
        from_light.neighbour_share = 0.5F;

        column_facts from_dark      = from_light;
        from_dark.neighbour[0].ramp = &light;

        REQUIRE(tone_at(from_light, light, "grass", 401.0) == tone_at(from_dark, dark, "grass", 401.0));
    }
}

TEST_CASE("the home region holds both biomes of the settings file", "[world][settings]") {
    const auto loaded = load_terrain_settings(
        std::filesystem::path{VW_ASSET_DIR} / "data" / "world_gen.json", default_voxel_registry(),
        terrain_params{}
    );
    REQUIRE(loaded.has_value());

    asset::model_identity_pool identity_pool;
    asset::page_pool pages;
    perlin_terrain_generator gen{identity_pool, pages, *loaded};

    std::set<std::string> seen;
    const auto reach = static_cast<int32>(loaded->region_spacing_voxels * 0.45F);
    for (int32 z = -reach; z <= reach; z += 16) {
        for (int32 x = -reach; x <= reach; x += 16) {
            if (gen.surface_height_at(x, z)) {
                seen.insert(std::string{biome_name(gen.biome_at(x, z))});
            }
        }
    }

    REQUIRE(seen.contains("plains"));
    REQUIRE(seen.contains("hills"));
}
