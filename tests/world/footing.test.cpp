#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

import std;

import vw.core;
import vw.ecs;
import vw.world;

using namespace vw;
using namespace vw::ecs;
using Catch::Approx;

namespace {

constexpr float32 body_width  = 12.0F;
constexpr float32 body_height = 28.0F;

struct heights {
    std::function<int32(int32, int32)> top_voxel;

    [[nodiscard]] auto operator()(int32 vx, int32 vy, int32 vz) const -> bool {
        return vy < top_voxel(vx, vz);
    }
};

auto under(float32 x, float32 z) -> footprint {
    return {.x = x, .z = z, .half_x = body_width * 0.5F, .half_z = body_width * 0.5F};
}

auto step_along_x(int32 from_voxel, int32 rise) -> heights {
    return {[=](int32 vx, int32) { return vx >= from_voxel ? rise : 0; }};
}

}  // namespace

TEST_CASE("on flat ground the body stands at the ground", "[world][footing]") {
    const float32 voxel = GENERATE(8.0F, 16.0F);
    const heights flat{[](int32, int32) { return 3; }};

    const float32 x = GENERATE(0.0F, 3.3F, 7.9F, 20.5F);
    const auto seen = footing_under(flat, voxel, under(x, 5.1F), 3.0F * voxel, voxel);

    REQUIRE(seen.found);
    REQUIRE(seen.stand == Approx(3.0F * voxel));
}

TEST_CASE("a step counts from the moment the footprint touches it", "[world][footing]") {
    const float32 voxel = GENERATE(8.0F, 16.0F);
    const heights step  = step_along_x(4, 1);
    const float32 face  = 4.0F * voxel;
    const float32 half  = body_width * 0.5F;

    REQUIRE(footing_under(step, voxel, under(face - half - 0.5F, 0.0F), 0.0F, voxel).stand == Approx(0.0F));
    REQUIRE(footing_under(step, voxel, under(face - half + 0.1F, 0.0F), 0.0F, voxel).stand == Approx(voxel));
    REQUIRE(footing_under(step, voxel, under(face + half + 0.5F, 0.0F), voxel, voxel).stand == Approx(voxel));
}

TEST_CASE("the edge of a cliff holds until the whole body is past it", "[world][footing]") {
    const float32 voxel = GENERATE(8.0F, 16.0F);
    const heights cliff = step_along_x(4, -9);
    const float32 face  = 4.0F * voxel;
    const float32 half  = body_width * 0.5F;

    const auto on_the_edge = footing_under(cliff, voxel, under(face, 0.0F), 0.0F, voxel);
    REQUIRE(on_the_edge.found);
    REQUIRE(on_the_edge.stand == Approx(0.0F));

    const auto by_a_sliver = footing_under(cliff, voxel, under(face + half - 0.1F, 0.0F), 0.0F, voxel);
    REQUIRE(by_a_sliver.found);
    REQUIRE(by_a_sliver.stand == Approx(0.0F));

    const auto past = footing_under(cliff, voxel, under(face + half + 0.1F, 0.0F), 0.0F, voxel);
    REQUIRE_FALSE(past.found);
}

TEST_CASE("a wall is not footing, from beside it or from inside, and the body does not fit into it", "[world][footing]") {
    const float32 voxel = GENERATE(8.0F, 16.0F);
    const heights wall  = step_along_x(4, 2);
    const float32 face  = 4.0F * voxel;
    const float32 half  = body_width * 0.5F;

    const auto beside = under(face - half - 0.05F, 0.0F);
    const auto inside = under(face - half + 1.0F, 0.0F);
    const auto buried = under(face + half + 2.0F * voxel, 0.0F);

    REQUIRE(footing_under(wall, voxel, beside, 0.0F, voxel).stand == Approx(0.0F));
    REQUIRE(footing_under(wall, voxel, inside, 0.0F, voxel).stand == Approx(0.0F));
    REQUIRE_FALSE(footing_under(wall, voxel, buried, 0.0F, voxel).found);
    REQUIRE(body_fits(wall, voxel, beside, voxel, body_height));
    REQUIRE_FALSE(body_fits(wall, voxel, inside, voxel, body_height));

    const auto a_step = step_along_x(4, 1);
    REQUIRE(body_fits(a_step, voxel, inside, voxel, body_height));
}

TEST_CASE("a roof overhead is not footing", "[world][footing]") {
    const float32 voxel = 8.0F;
    const auto bridged  = [](int32, int32 vy, int32) { return vy < 0 || vy == 6; };

    const auto seen = footing_under(bridged, voxel, under(3.0F, 3.0F), 0.0F, voxel);

    REQUIRE(seen.found);
    REQUIRE(seen.stand == Approx(0.0F));
    REQUIRE(body_fits(bridged, voxel, under(3.0F, 3.0F), voxel, body_height));
    REQUIRE_FALSE(body_fits(bridged, voxel, under(3.0F, 3.0F), voxel, 6.0F * voxel + 1.0F));
}
