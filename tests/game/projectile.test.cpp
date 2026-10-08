#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

import std;

import vw.core;
import vw.asset;
import vw.ecs;
import vw.world;
import vw.game;

using namespace vw;

namespace {

constexpr float32 tick_seconds = 1.0F / 120.0F;

struct range {
    ecs::world world;
    asset::vox_parser_plain parser;
    asset::model_library library{
        world.resource<asset::model_registry>(), world.voxel_types(), VW_ASSET_DIR
    };
    asset::asset_storage assets{parser, library};
    std::shared_ptr<asset::model> arrow;
    std::vector<game::projectile_hit> seen;

    range() {
        game::install_systems(world, assets);
        arrow = world.resource<asset::model_registry>().create_unnamed(1, 1, 8);
        arrow->fill(voxels::brown[5]);
        arrow->set_pivot({0.5F, 0.5F, 4.0F});
    }

    [[nodiscard]] auto shots() -> game::projectile_system& {
        return world.system<game::projectile_system>();
    }

    [[nodiscard]] auto block(const vec3f& at, const vec3i& size, ecs::spatial_layer_mask layer)
        -> ecs::entity {
        auto model = world.resource<asset::model_registry>().create_unnamed(size.x, size.y, size.z);
        model->fill(voxels::gray[9]);
        model->set_pivot({
            static_cast<float32>(size.x) * 0.5F,
            static_cast<float32>(size.y) * 0.5F,
            static_cast<float32>(size.z) * 0.5F,
        });

        const auto ent = world.create()
            .with<ecs::hierarchy_component>()
            .with<ecs::transform_component>()
            .with<ecs::spatial_component>()
            .with<ecs::model_component>()
            .get_entity();
        world.system<ecs::model_system>().modify(ent).set_model(model);
        world.system<ecs::spatial_system>().modify(ent).set_layer(layer);
        world.system<ecs::transform_system>().modify(ent).set_position(at);
        return ent;
    }

    auto tick() -> void {
        world.update(tick_seconds);
        for (const auto& hit : shots().hits()) {
            seen.push_back(hit);
        }
        world.clear_changed();
    }

    auto run_for(float32 seconds) -> void {
        for (float32 elapsed = 0.0F; elapsed < seconds; elapsed += tick_seconds) {
            tick();
        }
    }

    [[nodiscard]] auto loose(const vec3f& from, const vec3f& velocity, ecs::entity owner = {})
        -> ecs::entity {
        return shots().launch({.model = arrow, .position = from, .velocity = velocity, .owner = owner});
    }

    [[nodiscard]] auto at(ecs::entity ent) const -> vec3f {
        return world.get<ecs::transform_component>(ent).get_position();
    }

    [[nodiscard]] auto alive(ecs::entity ent) -> bool {
        return world.registry().alive(ent);
    }
};

}  // namespace

TEST_CASE("an arrow flies in an arc: steady ahead, faster and faster down", "[game][projectile]") {
    range r;
    r.shots().tuning().gravity_scale = 0.5F;
    const float32 pull = r.world.system<ecs::physics_system>().get_gravity() * 0.5F;
    REQUIRE(pull < 0.0F);

    const auto shot = r.loose({0.0F, 100.0F, 0.0F}, {300.0F, 0.0F, 0.0F});
    r.run_for(0.5F);
    const vec3f half_way = r.at(shot);
    r.run_for(0.5F);
    const vec3f landed = r.at(shot);

    REQUIRE(half_way.x == Catch::Approx(150.0F).epsilon(0.03));
    REQUIRE(landed.x == Catch::Approx(300.0F).epsilon(0.03));
    REQUIRE(100.0F - half_way.y == Catch::Approx(-pull * 0.125F).epsilon(0.08));
    REQUIRE(100.0F - landed.y == Catch::Approx(-pull * 0.5F).epsilon(0.08));

    const auto& flying = r.world.get<game::projectile_component>(shot);
    REQUIRE_FALSE(flying.is_stuck());
    REQUIRE(flying.get_velocity().y == Catch::Approx(pull).epsilon(0.03));
    REQUIRE(r.shots().get_flying_count() == 1);
}

TEST_CASE("an arrow sticks in the first voxel on its way and stays there", "[game][projectile]") {
    range r;
    r.shots().tuning().gravity_scale = 0.0F;
    static_cast<void>(r.block({200.0F, 0.0F, 0.0F}, {8, 64, 64}, ecs::spatial_layer::terrain));
    r.tick();

    const auto shot = r.loose({0.0F, 0.0F, 0.0F}, {600.0F, 0.0F, 0.0F});
    r.run_for(1.0F);

    const auto& stuck = r.world.get<game::projectile_component>(shot);
    REQUIRE(stuck.is_stuck());
    REQUIRE_FALSE(stuck.get_stuck_in().is_valid());
    REQUIRE(r.at(shot).x > 185.0F);
    REQUIRE(r.at(shot).x < 197.0F);

    REQUIRE(r.seen.size() == 1);
    REQUIRE(r.seen.front().projectile == shot);
    REQUIRE_FALSE(r.seen.front().target.is_valid());
    REQUIRE(r.seen.front().velocity.x == Catch::Approx(600.0F));
    REQUIRE(r.shots().get_entity_hit_count() == 0);
    REQUIRE(r.shots().get_stuck_count() == 1);

    const vec3f rested = r.at(shot);
    r.run_for(0.5F);
    REQUIRE(r.at(shot) == rested);
}

TEST_CASE("an arrow in a body is reported with its owner and travels with the body", "[game][projectile]") {
    range r;
    r.shots().tuning().gravity_scale = 0.0F;
    const auto archer = r.world.create().with<ecs::hierarchy_component>().get_entity();
    const auto body   = r.block({0.0F, 0.0F, 150.0F}, {16, 32, 16}, ecs::spatial_layer::character);
    r.tick();

    const auto shot = r.loose({0.0F, 0.0F, 0.0F}, {0.0F, 0.0F, 500.0F}, archer);
    r.run_for(0.6F);

    const auto& stuck = r.world.get<game::projectile_component>(shot);
    REQUIRE(stuck.is_stuck());
    REQUIRE(stuck.get_stuck_in() == body);
    REQUIRE(r.seen.size() == 1);
    REQUIRE(r.seen.front().target == body);
    REQUIRE(r.seen.front().owner == archer);
    REQUIRE(r.shots().get_entity_hit_count() == 1);

    const vec3f before = r.at(shot);
    r.world.system<ecs::transform_system>().modify(body).set_position({70.0F, 5.0F, 150.0F});
    r.tick();
    REQUIRE(r.at(shot).x == Catch::Approx(before.x + 70.0F));
    REQUIRE(r.at(shot).y == Catch::Approx(before.y + 5.0F));
    REQUIRE(r.at(shot).z == Catch::Approx(before.z));

    r.world.destroy(body);
    r.tick();
    r.tick();
    REQUIRE_FALSE(r.alive(shot));
}

TEST_CASE("an arrow passes through whoever loosed it, held things included", "[game][projectile]") {
    range r;
    r.shots().tuning().gravity_scale = 0.0F;
    const auto archer = r.block({0.0F, 0.0F, 0.0F}, {16, 32, 16}, ecs::spatial_layer::character);
    const auto held   = r.block({0.0F, 0.0F, 20.0F}, {4, 24, 4}, ecs::spatial_layer::character);
    r.world.system<ecs::hierarchy_system>().modify(held).set_parent(archer);
    const auto wall = r.block({0.0F, 0.0F, 200.0F}, {64, 64, 8}, ecs::spatial_layer::terrain);
    r.tick();

    const auto shot = r.loose({0.0F, 0.0F, 0.0F}, {0.0F, 0.0F, 400.0F}, archer);
    r.run_for(1.0F);

    REQUIRE(r.world.get<game::projectile_component>(shot).is_stuck());
    REQUIRE(r.seen.size() == 1);
    REQUIRE(r.seen.front().part == wall);
    REQUIRE(r.at(shot).z > 150.0F);
}

TEST_CASE("arrows do not live forever: neither in flight nor stuck", "[game][projectile]") {
    range r;
    r.shots().tuning().gravity_scale  = 0.0F;
    r.shots().tuning().flight_seconds = 0.3F;
    r.shots().tuning().stuck_seconds  = 0.4F;
    static_cast<void>(r.block({0.0F, 0.0F, 100.0F}, {64, 64, 8}, ecs::spatial_layer::terrain));
    r.tick();

    const auto lost  = r.loose({0.0F, 0.0F, 0.0F}, {0.0F, 300.0F, 0.0F});
    const auto stuck = r.loose({0.0F, 0.0F, 0.0F}, {0.0F, 0.0F, 500.0F});

    r.run_for(0.25F);
    REQUIRE(r.alive(lost));
    REQUIRE(r.world.get<game::projectile_component>(stuck).is_stuck());

    r.run_for(0.1F);
    REQUIRE_FALSE(r.alive(lost));
    REQUIRE(r.alive(stuck));

    r.run_for(0.4F);
    REQUIRE_FALSE(r.alive(stuck));
    REQUIRE(r.shots().get_stuck_count() == 0);
}

TEST_CASE("only so many arrows stay stuck; the oldest go first", "[game][projectile]") {
    range r;
    r.shots().tuning().gravity_scale = 0.0F;
    r.shots().tuning().stuck_limit   = 3;
    static_cast<void>(r.block({0.0F, 0.0F, 100.0F}, {64, 64, 8}, ecs::spatial_layer::terrain));
    r.tick();

    std::vector<ecs::entity> loosed;
    for (int32 index = 0; index < 5; ++index) {
        loosed.push_back(
            r.loose({static_cast<float32>(index) * 6.0F - 12.0F, 0.0F, 0.0F}, {0.0F, 0.0F, 500.0F})
        );
        r.run_for(0.3F);
    }

    REQUIRE(r.shots().get_stuck_count() == 3);
    REQUIRE(r.shots().get_launched_count() == 5);
    REQUIRE_FALSE(r.alive(loosed[0]));
    REQUIRE_FALSE(r.alive(loosed[1]));
    REQUIRE(r.alive(loosed[2]));
    REQUIRE(r.alive(loosed[4]));
}

TEST_CASE("a stuck arrow has its head inside what it hit, however it is held", "[game][projectile]") {
    for (const float32 held_at : {0.5F, 4.0F}) {
        range r;
        r.arrow->set_pivot({0.5F, 0.5F, held_at});
        r.shots().tuning().gravity_scale = 0.0F;
        const float32 sink   = r.shots().tuning().sink_units;
        const float32 length = 8.0F - held_at;
        REQUIRE(sink >= 3.0F);

        static_cast<void>(r.block({0.0F, -100.0F, 0.0F}, {128, 16, 128}, ecs::spatial_layer::terrain));
        const auto body = r.block({0.0F, 0.0F, 150.0F}, {16, 32, 16}, ecs::spatial_layer::character);
        r.tick();

        const float32 slant = std::sqrt(0.5F);
        const auto slanted  = r.loose({-50.0F, 0.0F, 0.0F}, {300.0F, -300.0F, 0.0F});
        const auto straight = r.loose({0.0F, 0.0F, 0.0F}, {0.0F, 0.0F, 500.0F});
        r.run_for(1.0F);

        REQUIRE(r.world.get<game::projectile_component>(slanted).is_stuck());
        const vec3f slanted_tip = r.at(slanted) + vec3f{slant, -slant, 0.0F} * length;
        REQUIRE(slanted_tip.y == Catch::Approx(-92.0F - sink * slant).margin(0.1));
        REQUIRE(slanted_tip.x == Catch::Approx(-50.0F + 92.0F + sink * slant).margin(0.1));

        REQUIRE(r.world.get<game::projectile_component>(straight).get_stuck_in() == body);
        REQUIRE(r.at(straight).z + length == Catch::Approx(142.0F + sink).margin(0.1));
    }
}
