#include <catch2/catch_test_macros.hpp>

import std;

import vw.core;
import vw.asset;
import vw.world;

using namespace vw;

namespace {

constexpr int32 side = asset::chunk_occupancy::side;
constexpr auto sky   = asset::light_channel::sky;
constexpr auto block = asset::light_channel::block;

// A real column of models, because block light is seeded out of block ids and
// occupancy bits cannot carry one. Nothing stands around it, so the four sides
// come out sealed and nothing above it, so the sky channel is wide open -- both
// of which the tests below lean on.
class column_fixture {
public:
    explicit column_fixture(int32 chunks) {
        for (int32 i = 0; i < chunks; ++i) {
            models_.push_back(std::make_unique<asset::model>(ids_, pages_, blocks::terrain::category, side, side, side));
        }
    }

    auto set(int32 x, int32 y, int32 z, block_id id) -> void {
        models_[static_cast<std::size_t>(y / side)]->set_voxel(x, y % side, z, voxel{id});
    }

    auto fill(vec3i from, vec3i to, block_id id) -> void {
        for (int32 y = from.y; y <= to.y; ++y) {
            for (int32 z = from.z; z <= to.z; ++z) {
                for (int32 x = from.x; x <= to.x; ++x) {
                    set(x, y, z, id);
                }
            }
        }
    }

    [[nodiscard]] auto light() -> asset::light_column {
        occupancy_.resize(models_.size());
        occ_.clear();
        emitters_.clear();

        for (std::size_t i = 0; i < models_.size(); ++i) {
            REQUIRE(models_[i]->build_x_rows(occupancy_[i]));
            occ_.push_back(&occupancy_[i]);
            emitters_.push_back(models_[i].get());
        }

        asset::light_column::neighbourhood around{};
        around[4] = asset::light_column::column_slice{.occupancy = occ_, .models = emitters_};

        return asset::light_column{
            around, asset::build_emission_table(block_registry{}), {}
        };
    }

private:
    asset::model_identity_pool ids_;
    asset::page_pool pages_;
    std::vector<std::unique_ptr<asset::model>> models_;
    std::vector<asset::chunk_occupancy> occupancy_;
    std::vector<const asset::chunk_occupancy*> occ_;
    std::vector<const asset::model*> emitters_;
};

}  // namespace

TEST_CASE("a lamp lights its own voxel and falls one level a step", "[block_light]") {
    column_fixture fixture{1};
    fixture.set(32, 32, 32, blocks::terrain::glowstone);

    const asset::light_column light = fixture.light();

    // The lamp block is solid, so its own level is never sampled by the mesher.
    // It is still written, because the spread starts from it.
    REQUIRE(light.level_at(32, 32, 32, block) == 14);

    REQUIRE(light.level_at(33, 32, 32, block) == 13);
    REQUIRE(light.level_at(34, 32, 32, block) == 12);
    REQUIRE(light.level_at(32, 33, 32, block) == 13);
    REQUIRE(light.level_at(32, 32, 33, block) == 13);

    // Fourteen steps and it is gone -- the same falloff sky light has, only
    // starting from what the block says instead of from fifteen.
    REQUIRE(light.level_at(45, 32, 32, block) == 1);
    REQUIRE(light.level_at(46, 32, 32, block) == 0);
}

TEST_CASE("lava carries one voxel further than a lamp", "[block_light]") {
    column_fixture fixture{1};
    fixture.set(10, 32, 32, blocks::terrain::lava);

    const asset::light_column light = fixture.light();

    REQUIRE(light.level_at(10, 32, 32, block) == 15);
    REQUIRE(light.level_at(11, 32, 32, block) == 14);
    REQUIRE(light.level_at(24, 32, 32, block) == 1);
    REQUIRE(light.level_at(25, 32, 32, block) == 0);
}

TEST_CASE("a wall stops block light", "[block_light]") {
    column_fixture fixture{1};
    fixture.set(32, 32, 32, blocks::terrain::glowstone);
    fixture.fill(vec3i{34, 0, 0}, vec3i{34, side - 1, side - 1}, blocks::terrain::stone[0]);

    const asset::light_column light = fixture.light();

    REQUIRE(light.level_at(33, 32, 32, block) == 13);
    REQUIRE(light.level_at(35, 32, 32, block) == 0);
    REQUIRE(light.level_at(63, 32, 32, block) == 0);
}

TEST_CASE("a world with no emitters has no block light at all", "[block_light]") {
    column_fixture fixture{1};
    fixture.fill(vec3i{0, 0, 0}, vec3i{side - 1, 20, side - 1}, blocks::terrain::stone[0]);

    const asset::light_column light = fixture.light();

    for (int32 y = 0; y < side; y += 7) {
        for (int32 z = 0; z < side; z += 11) {
            for (int32 x = 0; x < side; x += 13) {
                REQUIRE(light.level_at(x, y, z, block) == 0);
            }
        }
    }

    const asset::light_field field = light.bake(0, block);
    REQUIRE(field.is_uniform());
    REQUIRE(field.uniform_level() == 0);
}

// The seeding skips the inside of a uniform page, which is what keeps a lava
// lake costing its surface. The surface itself must survive that.
TEST_CASE("a solid page of lava lights all the way round itself", "[block_light]") {
    column_fixture fixture{1};
    fixture.fill(vec3i{8, 8, 8}, vec3i{15, 15, 15}, blocks::terrain::lava);

    const asset::light_column light = fixture.light();

    REQUIRE(light.level_at(12, 12, 12, block) == 15);
    REQUIRE(light.level_at(15, 12, 12, block) == 15);

    REQUIRE(light.level_at(16, 12, 12, block) == 14);
    REQUIRE(light.level_at(7, 12, 12, block) == 14);
    REQUIRE(light.level_at(12, 16, 12, block) == 14);
    REQUIRE(light.level_at(12, 7, 12, block) == 14);
    REQUIRE(light.level_at(12, 12, 16, block) == 14);
    REQUIRE(light.level_at(12, 12, 7, block) == 14);

    REQUIRE(light.level_at(17, 12, 12, block) == 13);
}

// Two nibbles of one byte. A write to either must leave the other where it was,
// and the whole point of keeping them apart is that the day cannot reach the
// lamp -- if they ever merged, this is the test that would say so.
TEST_CASE("the two channels do not touch each other", "[block_light]") {
    column_fixture fixture{1};
    fixture.set(32, 32, 32, blocks::terrain::glowstone);

    const asset::light_column light = fixture.light();

    REQUIRE(light.level_at(0, 0, 0, sky) == 15);
    REQUIRE(light.level_at(0, 0, 0, block) == 0);

    REQUIRE(light.level_at(33, 32, 32, sky) == 15);
    REQUIRE(light.level_at(33, 32, 32, block) == 13);
}

TEST_CASE("a baked block field reads back what was flooded", "[block_light]") {
    column_fixture fixture{1};
    fixture.set(20, 30, 40, blocks::terrain::glowstone);
    fixture.set(50, 10, 12, blocks::terrain::lava);

    const asset::light_column light = fixture.light();

    const asset::light_field baked_block = light.bake(0, block);
    const asset::light_field baked_sky   = light.bake(0, sky);

    REQUIRE_FALSE(baked_block.is_uniform());

    for (int32 y = 0; y < side; ++y) {
        for (int32 z = 0; z < side; ++z) {
            for (int32 x = 0; x < side; ++x) {
                if (baked_block.level_at(x, y, z) != light.level_at(x, y, z, block)) {
                    FAIL("block channel differs at " << x << "," << y << "," << z);
                }
                if (baked_sky.level_at(x, y, z) != light.level_at(x, y, z, sky)) {
                    FAIL("sky channel differs at " << x << "," << y << "," << z);
                }
            }
        }
    }
}

// The mesh is a function of the light as much as of the voxels: the levels are
// baked into the quad corners. mesh_pool keys on model_identity and drops a
// request for one it already holds, so light that arrives without a new
// identity never reaches the screen -- the chunk keeps the mesh it was given
// moments earlier, built against the light this call is replacing.
//
// The symptom is a one-edit lag, and it is not obvious from the picture what
// went wrong: place a lamp and nothing lights up, place anything at all beside
// it and the first lamp comes on, because that second edit bumped the
// generation for its own reasons.
TEST_CASE("setting either light invalidates the mesh built from it", "[block_light]") {
    asset::model_identity_pool ids;
    asset::page_pool pages;

    auto voxels = std::make_shared<asset::model>(ids, pages, blocks::terrain::category, side, side, side);
    asset::chunk_volume chunk{voxels};

    const asset::model_identity fresh = voxels->get_identity();

    chunk.set_sky_light(asset::light_field{});
    const asset::model_identity after_sky = voxels->get_identity();
    REQUIRE(after_sky != fresh);

    chunk.set_block_light(asset::light_field{});
    REQUIRE(voxels->get_identity() != after_sky);
}
