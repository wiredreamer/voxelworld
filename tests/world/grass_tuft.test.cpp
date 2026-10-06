#include <catch2/catch_test_macros.hpp>

import std;

import vw.core;
import vw.asset;
import vw.ecs;
import vw.world;

using namespace vw;
using namespace vw::ecs;

namespace {

struct tuft_facts {
    int32 height   = 0;
    int32 rooted   = 0;
    int32 filled   = 0;
    bool same_ramp = true;
};

auto facts_of(const asset::model& tuft, voxel look) -> tuft_facts {
    tuft_facts facts;
    for (int32 y = 0; y < tuft.height(); ++y) {
        for (int32 z = 0; z < tuft.depth(); ++z) {
            for (int32 x = 0; x < tuft.width(); ++x) {
                const voxel v = tuft.get_voxel(x, y, z);
                if (v.is_empty()) {
                    continue;
                }
                ++facts.filled;
                facts.height = std::max(facts.height, y + 1);
                facts.rooted += y == 0 ? 1 : 0;
                facts.same_ramp = facts.same_ramp && voxels::green.contains(v) == voxels::green.contains(look);
            }
        }
    }
    return facts;
}

auto voxels_of(const asset::model& tuft) -> std::vector<uint8> {
    std::vector<uint8> out;
    for (int32 y = 0; y < tuft.height(); ++y) {
        for (int32 z = 0; z < tuft.depth(); ++z) {
            for (int32 x = 0; x < tuft.width(); ++x) {
                out.push_back(tuft.get_voxel(x, y, z).value);
            }
        }
    }
    return out;
}

}  // namespace

TEST_CASE("every grass form stands on the ground and fits its cell", "[world][grass]") {
    asset::model_registry models;

    for (uint8 form = 1; form <= grass_form_count; ++form) {
        const auto tuft  = grow_grass_tuft(models, form, voxels::green[5]);
        const auto facts = facts_of(*tuft, voxels::green[5]);
        INFO("form " << static_cast<int32>(form));

        REQUIRE(tuft->width() == grass_tuft_footprint);
        REQUIRE(tuft->depth() == grass_tuft_footprint);
        REQUIRE(facts.rooted > 0);
        REQUIRE(facts.height >= grass_tuft_min_height);
        REQUIRE(facts.height <= grass_tuft_max_height);
        REQUIRE(facts.same_ramp);
    }
}

TEST_CASE("a grass form grows the same way every time", "[world][grass]") {
    asset::model_registry models;

    REQUIRE(voxels_of(*grow_grass_tuft(models, 2, voxels::green[4])) ==
            voxels_of(*grow_grass_tuft(models, 2, voxels::green[4])));
    REQUIRE(voxels_of(*grow_grass_tuft(models, 1, voxels::green[4])) !=
            voxels_of(*grow_grass_tuft(models, 2, voxels::green[4])));
}

TEST_CASE("grass forms grow taller class by class", "[world][grass]") {
    asset::model_registry models;

    int32 previous = 0;
    for (uint8 height_class = 0; height_class < grass_height_classes; ++height_class) {
        int32 tallest = 0;
        for (uint8 layout = 0; layout < grass_layouts; ++layout) {
            const uint8 form = grass_form_of(height_class, layout);
            REQUIRE(grass_height_class_of(form) == height_class);
            tallest = std::max(tallest, facts_of(*grow_grass_tuft(models, form, voxels::green[5]), voxels::green[5]).height);
        }
        INFO("class " << static_cast<int32>(height_class));
        REQUIRE(tallest > previous);
        previous = tallest;
    }
}

TEST_CASE("a tuft takes its shades from the ramp of the grass under it", "[world][grass]") {
    asset::model_registry models;

    const auto tuft = grow_grass_tuft(models, 2, voxels::amber[0]);
    const auto facts = facts_of(*tuft, voxels::amber[0]);
    REQUIRE(facts.filled > 0);

    for (const uint8 v : voxels_of(*tuft)) {
        REQUIRE((v == 0 || voxels::amber.contains(voxel{v})));
    }
}

TEST_CASE("every flower form stands on the ground with a crown of its own colour", "[world][grass]") {
    asset::model_registry models;

    std::set<uint8> crowns;
    for (uint8 color = 0; color < flower_colors; ++color) {
        std::vector<int32> petals;
        for (uint8 bunch = 0; bunch < flower_bunch_count; ++bunch) {
            const uint8 form = flower_form_of(color, bunch);
            INFO("colour " << static_cast<int32>(color) << ", bunch " << static_cast<int32>(bunch));
            REQUIRE(is_flower_form(form));
            REQUIRE(flower_color_of(form) == color);
            REQUIRE(flower_count_of(form) == flower_bunches[bunch]);

            const auto bed = grow_grass_tuft(models, form, voxels::green[5]);
            int32 height   = 0;
            int32 rooted   = 0;
            int32 foreign  = 0;
            uint8 brightest = 0;
            for (int32 y = 0; y < bed->height(); ++y) {
                for (int32 z = 0; z < bed->depth(); ++z) {
                    for (int32 x = 0; x < bed->width(); ++x) {
                        const voxel v = bed->get_voxel(x, y, z);
                        if (v.is_empty()) {
                            continue;
                        }
                        height = std::max(height, y + 1);
                        rooted += y == 0 ? 1 : 0;
                        if (!voxels::green.contains(v)) {
                            ++foreign;
                            brightest = std::max(brightest, v.value);
                        }
                    }
                }
            }

            REQUIRE(rooted == flower_count_of(form) * grass_stalk_width * grass_stalk_width);
            REQUIRE(height >= flower_min_height);
            REQUIRE(height <= flower_max_height);
            REQUIRE(foreign > 0);
            crowns.insert(brightest);
            petals.push_back(foreign);
        }
        REQUIRE(petals.back() > petals.front());
    }
    REQUIRE(crowns.size() == flower_colors);
    REQUIRE_FALSE(is_flower_form(grass_form_count));
    REQUIRE(is_flower_form(cover_form_count));
}
