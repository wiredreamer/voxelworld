#include <catch2/catch_test_macros.hpp>


import vw.core;

using namespace vw;

TEST_CASE("color default constructor", "[color]") {
    color c;
    REQUIRE(c.value == 0);
    REQUIRE(c.is_empty());
}

TEST_CASE("color uint32 constructor", "[color]") {
    color c{0xFF0000FFU};
    REQUIRE(c.r() == 255);
    REQUIRE(c.g() == 0);
    REQUIRE(c.b() == 0);
    REQUIRE(c.a() == 255);
}

TEST_CASE("color component constructor", "[color]") {
    SECTION("with explicit alpha") {
        color c{255, 128, 64, 200};
        REQUIRE(c.r() == 255);
        REQUIRE(c.g() == 128);
        REQUIRE(c.b() == 64);
        REQUIRE(c.a() == 200);
    }

    SECTION("default alpha is 255") {
        color c{100, 150, 200};
        REQUIRE(c.r() == 100);
        REQUIRE(c.g() == 150);
        REQUIRE(c.b() == 200);
        REQUIRE(c.a() == 255);
    }
}

TEST_CASE("color RGBA packing order", "[color]") {
    color c{0xAABBCCDDU};
    REQUIRE(c.r() == 0xAA);
    REQUIRE(c.g() == 0xBB);
    REQUIRE(c.b() == 0xCC);
    REQUIRE(c.a() == 0xDD);
}

TEST_CASE("color is_empty", "[color]") {
    REQUIRE(color{}.is_empty());
    REQUIRE(color{0x00000000U}.is_empty());
    REQUIRE(colors::empty.is_empty());
    REQUIRE_FALSE(colors::black.is_empty());
    REQUIRE_FALSE(colors::white.is_empty());
}

TEST_CASE("color comparison", "[color]") {
    REQUIRE(color{0xFF0000FFU} == color{0xFF0000FFU});
    REQUIRE(color{0xFF0000FFU} != color{0x00FF00FFU});
    REQUIRE(colors::red_6 == colors::red_6);
    REQUIRE(colors::red_6 != colors::blue_6);
}

TEST_CASE("color palette constants", "[color]") {
    REQUIRE(colors::white.value == 0xFFFFFFFFU);
    REQUIRE(colors::black.value == 0x000000FFU);

    REQUIRE(colors::all.size() == 87U);
    REQUIRE(colors::all.front() == colors::blue_0);
    REQUIRE(colors::all.back() == colors::black);
    REQUIRE(colors::all[43] == colors::amber_10);

    const auto luma = [](const color& c) -> uint32 {
        return static_cast<uint32>(c.r()) + static_cast<uint32>(c.g()) + static_cast<uint32>(c.b());
    };
    REQUIRE(luma(colors::blue_0) < luma(colors::blue_6));
    REQUIRE(luma(colors::blue_6) < luma(colors::blue_10));
    REQUIRE(luma(colors::gray_0) < luma(colors::gray_18));
    REQUIRE(luma(colors::amber_0) < luma(colors::amber_10));
}

TEST_CASE("color round-trip packing", "[color]") {
    for (uint8 r = 0; r < 5; ++r) {
        for (uint8 g = 0; g < 5; ++g) {
            for (uint8 b = 0; b < 5; ++b) {
                color c{static_cast<uint8>(r * 63), static_cast<uint8>(g * 63),
                         static_cast<uint8>(b * 63), 255};
                REQUIRE(c.r() == r * 63);
                REQUIRE(c.g() == g * 63);
                REQUIRE(c.b() == b * 63);
                REQUIRE(c.a() == 255);
            }
        }
    }
}
