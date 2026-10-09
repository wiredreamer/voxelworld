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

TEST_CASE("on flat ground the body rides and stands at the ground", "[world][footing]") {
    const float32 voxel = GENERATE(8.0F, 16.0F);
    const heights flat{[](int32, int32) { return 3; }};

    const float32 x = GENERATE(0.0F, 3.3F, 7.9F, 20.5F);
    const auto seen = footing_under(flat, voxel, under(x, 5.1F), 3.0F * voxel, voxel);

    REQUIRE(seen.found);
    REQUIRE(seen.ride == Approx(3.0F * voxel));
    REQUIRE(seen.stand == Approx(3.0F * voxel));
}

TEST_CASE("a step up is a ramp as long as the body is wide", "[world][footing]") {
    const float32 voxel = GENERATE(8.0F, 16.0F);
    const heights step  = step_along_x(4, 1);
    const float32 face  = 4.0F * voxel;
    const float32 half  = body_width * 0.5F;

    const auto before = footing_under(step, voxel, under(face - half - 0.5F, 0.0F), 0.0F, voxel);
    REQUIRE(before.ride == Approx(0.0F));
    REQUIRE(before.stand == Approx(0.0F));

    float32 feet = 0.0F;
    for (const float32 share : {0.25F, 0.5F, 0.75F}) {
        const float32 x = face - half + (body_width * share);
        const auto seen = footing_under(step, voxel, under(x, 0.0F), feet, voxel);
        REQUIRE(seen.ride == Approx(voxel * share).margin(1.0e-3F));
        REQUIRE(seen.stand == Approx(voxel));
        feet = seen.ride;
    }

    const auto after = footing_under(step, voxel, under(face + half + 0.5F, 0.0F), voxel, voxel);
    REQUIRE(after.ride == Approx(voxel));
}

TEST_CASE("the ramp can be squeezed into the first share of the body", "[world][footing]") {
    const float32 voxel = 16.0F;
    const float32 full  = 0.4F;
    const float32 half  = body_width * 0.5F;
    const float32 face  = 4.0F * voxel;

    const heights up = step_along_x(4, 1);
    const auto at    = [&](const heights& land, float32 share, float32 feet) {
        return footing_under(
            land, voxel, under(face - half + (body_width * share), 0.0F), feet, voxel, full
        );
    };

    REQUIRE(at(up, 0.0F, 0.0F).ride == Approx(0.0F));
    REQUIRE(at(up, 0.2F, 0.0F).ride == Approx(0.5F * voxel).margin(1.0e-3F));
    REQUIRE(at(up, 0.4F, 0.5F * voxel).ride == Approx(voxel).margin(1.0e-3F));
    REQUIRE(at(up, 0.7F, voxel).ride == Approx(voxel));

    const heights down = step_along_x(4, -1);
    REQUIRE(at(down, 0.3F, 0.0F).ride == Approx(0.0F));
    REQUIRE(at(down, 0.6F, 0.0F).ride == Approx(0.0F).margin(1.0e-3F));
    REQUIRE(at(down, 0.8F, 0.0F).ride == Approx(-0.5F * voxel).margin(1.0e-3F));
    REQUIRE(at(down, 1.0F, 0.0F).ride == Approx(-voxel).margin(1.0e-3F));
}

TEST_CASE("a step down is the same ramp, taken without leaving the ground", "[world][footing]") {
    const float32 voxel = GENERATE(8.0F, 16.0F);
    const heights step  = step_along_x(4, -1);
    const float32 face  = 4.0F * voxel;
    const float32 half  = body_width * 0.5F;

    for (const float32 share : {0.25F, 0.5F, 0.75F}) {
        const float32 x = face - half + (body_width * share);
        const auto seen = footing_under(step, voxel, under(x, 0.0F), 0.0F, voxel);
        REQUIRE(seen.ride == Approx(-voxel * share).margin(1.0e-3F));
        REQUIRE(seen.stand == Approx(0.0F));
    }
}

TEST_CASE("the edge of a cliff holds until the whole body is past it", "[world][footing]") {
    const float32 voxel = GENERATE(8.0F, 16.0F);
    const heights cliff = step_along_x(4, -9);
    const float32 face  = 4.0F * voxel;
    const float32 half  = body_width * 0.5F;

    const auto on_the_edge = footing_under(cliff, voxel, under(face, 0.0F), 0.0F, voxel);
    REQUIRE(on_the_edge.found);
    REQUIRE(on_the_edge.ride == Approx(-0.5F * voxel).margin(1.0e-3F));
    REQUIRE(on_the_edge.stand == Approx(0.0F));

    const auto by_a_sliver =
        footing_under(cliff, voxel, under(face + half - 0.1F, 0.0F), -0.9F * voxel, voxel);
    REQUIRE(by_a_sliver.found);
    REQUIRE(by_a_sliver.ride > -voxel);
    REQUIRE(by_a_sliver.stand == Approx(0.0F));

    const auto past = footing_under(cliff, voxel, under(face + half + 0.1F, 0.0F), -voxel, voxel);
    REQUIRE_FALSE(past.found);
}

TEST_CASE("a wall is not footing and the body does not fit into it", "[world][footing]") {
    const float32 voxel = GENERATE(8.0F, 16.0F);
    const heights wall  = step_along_x(4, 2);
    const float32 face  = 4.0F * voxel;
    const float32 half  = body_width * 0.5F;

    const auto beside = under(face - half - 0.05F, 0.0F);
    const auto inside = under(face - half + 1.0F, 0.0F);

    REQUIRE(footing_under(wall, voxel, beside, 0.0F, voxel).ride == Approx(0.0F));
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
    REQUIRE(seen.ride == Approx(0.0F));
    REQUIRE(body_fits(bridged, voxel, under(3.0F, 3.0F), voxel, body_height));
    REQUIRE_FALSE(body_fits(bridged, voxel, under(3.0F, 3.0F), voxel, 6.0F * voxel + 1.0F));
}

namespace {

struct walk_report {
    int32 moved        = 0;
    int32 falls        = 0;
    int32 lifted       = 0;
    float32 worst_rise = 0.0F;
    float32 worst_sink = 0.0F;
};

// см. docs/world.md#опора-под-телом
auto wander(float32 voxel, uint32 seed, int32 roughness, float32 share) -> walk_report {
    constexpr float32 stride = 0.25F;
    constexpr int32 strides  = 40000;

    std::mt19937 dice{seed};
    std::unordered_map<uint64, int32> bumps;
    const heights land{[&](int32 vx, int32 vz) {
        const float32 wave = std::sin(static_cast<float32>(vx) * 0.55F) +
                             std::cos(static_cast<float32>(vz) * 0.45F) +
                             std::sin(static_cast<float32>(vx + vz) * 0.3F);
        const auto key = (static_cast<uint64>(static_cast<uint32>(vx)) << 32U) |
                         static_cast<uint64>(static_cast<uint32>(vz));
        auto found = bumps.find(key);
        if (found == bumps.end()) {
            constexpr std::array<int32, 6> extra{0, 0, 0, 1, -1, -4};
            const int32 bump = roughness > 1 ? extra[dice() % extra.size()] : 0;
            found = bumps.emplace(key, bump).first;
        }
        return static_cast<int32>(std::lround(wave * static_cast<float32>(roughness))) + found->second;
    }};

    const float32 step  = voxel;
    const float32 bound = 2.0F * step * (2.0F / body_width) * stride / share;
    const auto settle   = [&](float32 x, float32 z, float32 from) {
        float32 feet = from;
        for (int32 guard = 0; guard < 400000; ++guard) {
            const auto seen = footing_under(land, voxel, under(x, z), feet, step, share);
            if (seen.found && feet <= seen.ride + 1.0e-4F) {
                return seen.ride;
            }
            feet -= 0.05F;
        }
        FAIL("the wanderer fell out of the world");
        return feet;
    };

    std::mt19937 turns{(seed * 7U) + 1U};
    std::uniform_real_distribution<float32> any_way{0.0F, 2.0F * std::numbers::pi_v<float32>};

    walk_report report;
    float32 x       = voxel * 0.5F;
    float32 z       = voxel * 0.5F;
    float32 feet    = settle(x, z, 400.0F);
    float32 heading = 0.0F;
    int32 refused   = 0;

    for (int32 at = 0; at < strides; ++at) {
        if (at % 200 == 0) {
            heading = any_way(turns);
        }
        const float32 next_x = x + (std::cos(heading) * stride);
        const float32 next_z = z + (std::sin(heading) * stride);
        const auto seen      = footing_under(land, voxel, under(next_x, next_z), feet, step, share);
        const bool dropped   = !seen.found || seen.ride < feet - bound - 1.0e-3F;
        const float32 stood  = dropped ? feet : seen.ride;

        if (!body_fits(land, voxel, under(next_x, next_z), stood + step, stood + body_height)) {
            if (++refused % 5 == 0) {
                heading = any_way(turns);
            }
            continue;
        }

        x = next_x;
        z = next_z;
        if (dropped) {
            ++report.falls;
            feet = settle(x, z, feet);
            continue;
        }

        report.worst_rise = std::max(report.worst_rise, seen.ride - feet);
        report.worst_sink = std::min(report.worst_sink, seen.ride - feet);
        report.lifted += seen.ride - feet > bound + 1.0e-3F ? 1 : 0;
        feet = seen.ride;
        ++report.moved;
    }
    return report;
}

}  // namespace

TEST_CASE("wandering over bumps the footing never jumps up and sinks no faster than a ramp", "[world][footing]") {
    const float32 voxel   = GENERATE(8.0F, 16.0F);
    const uint32 seed     = GENERATE(1U, 2U, 3U);
    const int32 roughness = GENERATE(1, 2);
    INFO("voxel " << voxel << " seed " << seed << " roughness " << roughness);

    const float32 share    = GENERATE(1.0F, 0.4F);
    const walk_report seen = wander(voxel, seed, roughness, share);
    const float32 bound    = 2.0F * voxel * (2.0F / body_width) * 0.25F / share;

    REQUIRE(seen.moved > 10000);
    REQUIRE(seen.lifted == 0);
    REQUIRE(seen.worst_rise <= bound + 1.0e-3F);
    REQUIRE(seen.worst_sink >= -bound - 1.0e-3F);
}
