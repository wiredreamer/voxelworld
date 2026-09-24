#include <catch2/catch_test_macros.hpp>

import std;

import vw.core;
import vw.asset;
import vw.ecs;
import vw.world;
import vw.gfx;

using namespace vw;

namespace {

class pool_fixture {
public:
    pool_fixture()
        : jobs_{2}
        , pool_{registry_, jobs_}
        , model_{std::make_shared<asset::model>(
              identity_pool_, pages_, 64, 64, 64
          )} {
        for (int32 x = 0; x < 64; ++x) {
            for (int32 z = 0; z < 64; ++z) {
                const int32 height = 8 + ((x + z) % 3);
                for (int32 y = 0; y < height; ++y) {
                    model_->set_voxel(x, y, z, voxels::green[3]);
                }
            }
        }
    }

    [[nodiscard]] auto pool() -> gfx::mesh_pool& {
        return pool_;
    }

    [[nodiscard]] auto identity() const -> asset::model_identity {
        return model_->get_identity();
    }

    [[nodiscard]] auto voxels() -> asset::model& {
        return *model_;
    }

    auto request(int32 step) -> void {
        pool_.request_mesh(model_, nullptr, gfx::mesh_options{.lod_step = step});
    }

    auto settle() -> void {
        jobs_.drain(job_lane::mesh);
        pool_.process_completed(64);
    }

private:
    asset::model_identity_pool identity_pool_;
    asset::page_pool pages_;
    voxel_registry registry_;
    job_system jobs_;
    gfx::mesh_pool pool_;
    std::shared_ptr<asset::model> model_;
};

}  // namespace

TEST_CASE("the pool holds two levels of one model at once", "[mesh][lod]") {
    pool_fixture fixture;

    fixture.request(1);
    fixture.request(2);
    fixture.settle();

    const auto id = fixture.identity();

    REQUIRE(fixture.pool().has(id, 1));
    REQUIRE(fixture.pool().has(id, 2));

    const auto* fine   = fixture.pool().get(id, 1);
    const auto* coarse = fixture.pool().get(id, 2);

    REQUIRE(fine != nullptr);
    REQUIRE(coarse != nullptr);
    REQUIRE(fine->lod_step == 1);
    REQUIRE(coarse->lod_step == 2);
    REQUIRE(coarse->quads.size() < fine->quads.size());
    REQUIRE(fixture.pool().get_gen_stats().held == 2);
}

TEST_CASE("evicting one level leaves the other standing", "[mesh][lod]") {
    pool_fixture fixture;

    fixture.request(1);
    fixture.request(2);
    fixture.settle();

    const auto id = fixture.identity();
    fixture.pool().evict(id, 2);

    REQUIRE(fixture.pool().has(id, 1));
    REQUIRE_FALSE(fixture.pool().has(id, 2));
    REQUIRE(fixture.pool().get_gen_stats().held == 1);
}

TEST_CASE("editing the model retires every level it had", "[mesh][lod]") {
    pool_fixture fixture;

    fixture.request(1);
    fixture.request(2);
    fixture.settle();
    REQUIRE(fixture.pool().get_gen_stats().held == 2);

    const auto before = fixture.identity();
    fixture.voxels().set_voxel(0, 40, 0, voxels::gray[4]);
    const auto after = fixture.identity();
    REQUIRE(before.generation != after.generation);

    fixture.request(1);
    fixture.settle();

    REQUIRE(fixture.pool().has(after, 1));
    REQUIRE_FALSE(fixture.pool().has(after, 2));
    REQUIRE(fixture.pool().get_gen_stats().held == 1);
}

TEST_CASE("the pool keys by the level asked for, the mesh by the one it got", "[mesh][lod]") {
    asset::model_identity_pool identity_pool;
    asset::page_pool pages;
    voxel_registry registry;
    job_system jobs{2};
    gfx::mesh_pool pool{registry, jobs};

    auto small = std::make_shared<asset::model>(
        identity_pool, pages, 32, 32, 32
    );
    small->set_voxel(4, 4, 4, voxels::gray[4]);

    pool.request_mesh(small, nullptr, gfx::mesh_options{.lod_step = 4});
    jobs.drain(job_lane::mesh);
    pool.process_completed(64);

    const auto id = small->get_identity();

    REQUIRE(pool.has(id, 4));
    REQUIRE_FALSE(pool.has(id, 1));

    const auto* built = pool.get(id, 4);
    REQUIRE(built != nullptr);
    REQUIRE(built->lod_step == 1);
    REQUIRE(gfx::mesh_key_of(id, built->lod_step).level == 0);
    REQUIRE(gfx::effective_lod_step(*small, 4) == 1);
}
