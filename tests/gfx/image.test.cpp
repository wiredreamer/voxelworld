#include <catch2/catch_test_macros.hpp>

import std;

import vw.core;
import vw.gfx;

using namespace vw;

namespace {

auto flat_image(uint32 width, uint32 height, std::array<uint8, 4> texel) -> gfx::image_rgba {
    gfx::image_rgba image{.width = width, .height = height, .pixels = {}};
    image.pixels.reserve(static_cast<std::size_t>(width) * height * 4);
    for (uint32 index = 0; index < width * height; ++index) {
        image.pixels.insert(image.pixels.end(), texel.begin(), texel.end());
    }
    return image;
}

auto big_endian_at(const std::vector<uint8>& bytes, std::size_t at) -> uint32 {
    return (static_cast<uint32>(bytes[at]) << 24) | (static_cast<uint32>(bytes[at + 1]) << 16) |
        (static_cast<uint32>(bytes[at + 2]) << 8) | static_cast<uint32>(bytes[at + 3]);
}

auto texel_at(const gfx::image_rgba& image, uint32 x, uint32 y) -> std::array<uint8, 4> {
    const std::size_t at = (static_cast<std::size_t>(y) * image.width + x) * 4;
    return {image.pixels[at], image.pixels[at + 1], image.pixels[at + 2], image.pixels[at + 3]};
}

}  // namespace

TEST_CASE("a png starts with its signature and names its size", "[image]") {
    const auto encoded = gfx::encode_png(flat_image(5, 3, {10, 20, 30, 255}));

    REQUIRE(encoded.has_value());
    REQUIRE(encoded->size() > 33);

    const std::array<uint8, 8> signature{0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A};
    CHECK(std::ranges::equal(std::span{*encoded}.first(8), signature));

    const std::string_view header_tag{reinterpret_cast<const char*>(encoded->data()) + 12, 4};
    CHECK(header_tag == "IHDR");
    CHECK(big_endian_at(*encoded, 16) == 5);
    CHECK(big_endian_at(*encoded, 20) == 3);

    const std::string_view end_tag{
        reinterpret_cast<const char*>(encoded->data()) + encoded->size() - 8, 4
    };
    CHECK(end_tag == "IEND");
}

TEST_CASE("a flat image compresses far below its raw size", "[image]") {
    const auto image   = flat_image(256, 256, {200, 100, 50, 255});
    const auto encoded = gfx::encode_png(image);

    REQUIRE(encoded.has_value());
    CHECK(encoded->size() < image.pixels.size() / 20);
}

TEST_CASE("an image without pixels is not encoded", "[image]") {
    CHECK(gfx::encode_png(gfx::image_rgba{}).error() == gfx::png_error::empty_image);

    gfx::image_rgba no_rows{.width = 4, .height = 0, .pixels = {}};
    CHECK(gfx::encode_png(no_rows).error() == gfx::png_error::empty_image);
}

TEST_CASE("an image whose pixels do not match its size is not encoded", "[image]") {
    auto image = flat_image(4, 4, {1, 2, 3, 255});
    image.pixels.pop_back();

    CHECK(gfx::encode_png(image).error() == gfx::png_error::size_mismatch);
}

TEST_CASE("shrinking keeps an image that already fits", "[image]") {
    const auto image = flat_image(40, 30, {9, 8, 7, 255});

    const auto same = gfx::shrunk_to_fit(image, 40);
    CHECK(same.width == 40);
    CHECK(same.height == 30);
    CHECK(same.pixels == image.pixels);

    const auto larger_limit = gfx::shrunk_to_fit(image, 4000);
    CHECK(larger_limit.width == 40);
    CHECK(larger_limit.height == 30);
}

TEST_CASE("shrinking fits the longest side and keeps the proportions", "[image]") {
    const auto wide = gfx::shrunk_to_fit(flat_image(1800, 1200, {1, 2, 3, 255}), 900);
    CHECK(wide.width == 900);
    CHECK(wide.height == 600);
    CHECK(wide.is_consistent());

    const auto tall = gfx::shrunk_to_fit(flat_image(300, 1000, {1, 2, 3, 255}), 100);
    CHECK(tall.width == 30);
    CHECK(tall.height == 100);

    const auto sliver = gfx::shrunk_to_fit(flat_image(1000, 2, {1, 2, 3, 255}), 10);
    CHECK(sliver.width == 10);
    CHECK(sliver.height == 1);
}

TEST_CASE("shrinking averages the pixels it merges", "[image]") {
    gfx::image_rgba image{.width = 4, .height = 2, .pixels = {}};
    const std::array<std::array<uint8, 4>, 8> texels{{
        {0, 0, 0, 255},
        {100, 0, 0, 255},
        {10, 20, 30, 255},
        {10, 20, 30, 255},
        {200, 0, 0, 255},
        {100, 0, 0, 255},
        {10, 20, 30, 255},
        {10, 20, 30, 255},
    }};
    for (const auto& texel : texels) {
        image.pixels.insert(image.pixels.end(), texel.begin(), texel.end());
    }

    const auto small = gfx::shrunk_to_fit(image, 2);

    REQUIRE(small.width == 2);
    REQUIRE(small.height == 1);
    CHECK(texel_at(small, 0, 0) == std::array<uint8, 4>{100, 0, 0, 255});
    CHECK(texel_at(small, 1, 0) == std::array<uint8, 4>{10, 20, 30, 255});
}

TEST_CASE("a shrunk flat image stays flat", "[image]") {
    const auto small = gfx::shrunk_to_fit(flat_image(333, 217, {40, 80, 120, 255}), 64);

    REQUIRE(small.is_consistent());
    for (uint32 y = 0; y < small.height; ++y) {
        for (uint32 x = 0; x < small.width; ++x) {
            REQUIRE(texel_at(small, x, y) == std::array<uint8, 4>{40, 80, 120, 255});
        }
    }
}

TEST_CASE("tiles are laid out in rows, each in its own cell", "[image]") {
    const std::array tiles{
        flat_image(2, 3, {255, 0, 0, 255}),
        flat_image(2, 3, {0, 255, 0, 255}),
        flat_image(2, 3, {0, 0, 255, 255}),
    };

    const gfx::image_rgba sheet = gfx::tiled(tiles, 2);

    REQUIRE(sheet.width == 4);
    REQUIRE(sheet.height == 6);
    REQUIRE(sheet.is_consistent());

    CHECK(texel_at(sheet, 0, 0) == std::array<uint8, 4>{255, 0, 0, 255});
    CHECK(texel_at(sheet, 1, 2) == std::array<uint8, 4>{255, 0, 0, 255});
    CHECK(texel_at(sheet, 2, 0) == std::array<uint8, 4>{0, 255, 0, 255});
    CHECK(texel_at(sheet, 3, 2) == std::array<uint8, 4>{0, 255, 0, 255});
    CHECK(texel_at(sheet, 0, 3) == std::array<uint8, 4>{0, 0, 255, 255});
    CHECK(texel_at(sheet, 1, 5) == std::array<uint8, 4>{0, 0, 255, 255});
    CHECK(texel_at(sheet, 2, 3) == std::array<uint8, 4>{0, 0, 0, 0});
}

TEST_CASE("fewer tiles than columns make one short row", "[image]") {
    const std::array tiles{flat_image(3, 2, {9, 9, 9, 255}), flat_image(3, 2, {7, 7, 7, 255})};

    const gfx::image_rgba sheet = gfx::tiled(tiles, 6);

    CHECK(sheet.width == 6);
    CHECK(sheet.height == 2);
    CHECK(texel_at(sheet, 5, 1) == std::array<uint8, 4>{7, 7, 7, 255});
}

TEST_CASE("a smaller tile sits in the corner of a cell sized for the largest", "[image]") {
    const std::array tiles{flat_image(4, 4, {1, 1, 1, 255}), flat_image(2, 1, {200, 200, 200, 255})};

    const gfx::image_rgba sheet = gfx::tiled(tiles, 2);

    REQUIRE(sheet.width == 8);
    REQUIRE(sheet.height == 4);
    CHECK(texel_at(sheet, 4, 0) == std::array<uint8, 4>{200, 200, 200, 255});
    CHECK(texel_at(sheet, 5, 0) == std::array<uint8, 4>{200, 200, 200, 255});
    CHECK(texel_at(sheet, 6, 0) == std::array<uint8, 4>{0, 0, 0, 0});
    CHECK(texel_at(sheet, 4, 1) == std::array<uint8, 4>{0, 0, 0, 0});
}

TEST_CASE("nothing to tile gives an empty image", "[image]") {
    CHECK(gfx::tiled(std::span<const gfx::image_rgba>{}, 3).empty());

    const std::array tiles{flat_image(2, 2, {1, 2, 3, 255})};
    CHECK(gfx::tiled(tiles, 0).empty());

    gfx::image_rgba broken{.width = 4, .height = 4, .pixels = {1, 2, 3}};
    const std::array with_broken{flat_image(2, 2, {1, 2, 3, 255}), broken};
    CHECK(gfx::tiled(with_broken, 2).empty());
}
