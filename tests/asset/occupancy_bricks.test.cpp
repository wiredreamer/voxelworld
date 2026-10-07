#include <catch2/catch_test_macros.hpp>

import std;

import vw.core;
import vw.asset;

using namespace vw;

namespace {

constexpr int32 side = asset::chunk_occupancy::side;

auto scattered() -> asset::chunk_occupancy {
    asset::chunk_occupancy solid;

    std::mt19937 rng{11};
    std::uniform_int_distribution<int32> place{0, side - 1};
    for (int32 i = 0; i < 3000; ++i) {
        solid.set_row(place(rng), place(rng), uint64{1} << place(rng));
    }
    for (int32 y = 0; y < 9; ++y) {
        for (int32 z = 0; z < side; ++z) {
            solid.set_row(y, z, ~uint64{0});
        }
    }
    return solid;
}

auto any_in(const asset::chunk_occupancy& solid, vec3i cell, int32 level) -> bool {
    const int32 span = 1 << level;
    for (int32 z = 0; z < span; ++z) {
        for (int32 y = 0; y < span; ++y) {
            for (int32 x = 0; x < span; ++x) {
                if (solid.test((cell.x * span) + x, (cell.y * span) + y, (cell.z * span) + z)) {
                    return true;
                }
            }
        }
    }
    return false;
}

}  // namespace

TEST_CASE("every brick bit says whether its cell holds anything solid", "[occupancy]") {
    const auto solid = std::make_unique<asset::chunk_occupancy>(scattered());

    for (int32 level = 0; level < spatial::occupancy_clipmap_layout::level_count; ++level) {
        std::vector<uint8> bricks(asset::occupancy_brick_count(level));
        asset::pack_occupancy_bricks(*solid, level, bricks);

        const int32 cells  = side >> level;
        const int32 across = cells / 2;
        REQUIRE(across == spatial::occupancy_clipmap_layout::bricks_per_chunk(level));

        for (int32 z = 0; z < cells; ++z) {
            for (int32 y = 0; y < cells; ++y) {
                for (int32 x = 0; x < cells; ++x) {
                    const vec3i cell{x, y, z};
                    const auto brick = static_cast<std::size_t>(
                        (x / 2) + (across * ((y / 2) + (across * (z / 2))))
                    );
                    const bool packed =
                        ((bricks[brick] >> spatial::occupancy_brick_bit(cell)) & 1U) != 0;

                    REQUIRE(packed == any_in(*solid, cell, level));
                }
            }
        }
    }
}

TEST_CASE("rows built page by page match the ones the mesher reads", "[occupancy]") {
    asset::model_identity_pool ids;
    asset::page_pool pages;
    asset::model scattered_model{ids, pages, side, side, side};

    std::mt19937 rng{3};
    std::uniform_int_distribution<int32> place{0, side - 1};
    for (int32 i = 0; i < 4000; ++i) {
        scattered_model.set_voxel(place(rng), place(rng), place(rng), voxels::gray[8]);
    }
    for (int32 z = 0; z < side; ++z) {
        for (int32 x = 0; x < side; ++x) {
            for (int32 y = 0; y < 12; ++y) {
                scattered_model.set_voxel(x, y, z, voxels::brown[4]);
            }
        }
    }
    static_cast<void>(scattered_model.compact_pages());

    const auto meshed = std::make_unique<asset::chunk_occupancy>();
    const auto paged  = std::make_unique<asset::chunk_occupancy>();
    paged->rows.fill(~uint64{0});

    REQUIRE(scattered_model.build_occupancy(*meshed));
    REQUIRE(scattered_model.build_rows_page_by_page(*paged));

    CHECK(meshed->rows == paged->rows);
}

TEST_CASE("bit rows of a model of any size say which voxels are there", "[occupancy]") {
    asset::model_identity_pool ids;
    asset::page_pool pages;

    for (const vec3i size : {vec3i{5, 9, 3}, vec3i{32, 16, 8}, vec3i{37, 21, 70}, vec3i{128, 4, 4}}) {
        asset::model figure{ids, pages, size.x, size.y, size.z};

        std::mt19937 rng{static_cast<uint32>(size.x * 31 + size.z)};
        std::bernoulli_distribution filled{0.35};
        for (int32 z = 0; z < size.z; ++z) {
            for (int32 y = 0; y < size.y; ++y) {
                for (int32 x = 0; x < size.x; ++x) {
                    if (filled(rng) || y == 0) {
                        figure.set_voxel(x, y, z, voxels::gray[8]);
                    }
                }
            }
        }
        static_cast<void>(figure.compact_pages());

        const uint32 row_words = asset::bit_row_words(size.x);
        std::vector<uint32> rows(
            static_cast<std::size_t>(row_words) * static_cast<std::size_t>(size.y * size.z),
            ~uint32{0}
        );
        figure.build_bit_rows(rows);

        for (int32 z = 0; z < size.z; ++z) {
            for (int32 y = 0; y < size.y; ++y) {
                const auto row = static_cast<std::size_t>(y + (size.y * z)) * row_words;
                for (int32 x = 0; x < static_cast<int32>(row_words) * 32; ++x) {
                    const bool bit =
                        ((rows[row + static_cast<std::size_t>(x >> 5)] >> (x & 31)) & 1U) != 0;
                    const bool there = x < size.x && !figure.is_empty(x, y, z);

                    REQUIRE(bit == there);
                }
            }
        }
    }
}

TEST_CASE("cost of reading and packing one chunk of hills", "[.occupancy_cost]") {
    asset::model_identity_pool ids;
    asset::page_pool pages;
    asset::model hills{ids, pages, side, side, side};

    for (int32 z = 0; z < side; ++z) {
        for (int32 x = 0; x < side; ++x) {
            const float32 wave = std::sin(static_cast<float32>(x) * 0.13F) +
                                 std::cos(static_cast<float32>(z) * 0.09F);
            const int32 top = 24 + static_cast<int32>(wave * 9.0F);
            for (int32 y = 0; y <= top; ++y) {
                hills.set_voxel(x, y, z, y == top ? voxels::green[4] : voxels::brown[4]);
            }
        }
    }
    static_cast<void>(hills.compact_pages());

    const auto solid = std::make_unique<asset::chunk_occupancy>();
    std::vector<uint8> bricks(asset::occupancy_brick_count(0));

    constexpr int32 rounds = 2000;
    uint64 sink            = 0;

    const auto timed = [&](std::string_view what, auto&& work) {
        const auto started = std::chrono::steady_clock::now();
        for (int32 round = 0; round < rounds; ++round) {
            work();
        }
        const std::chrono::duration<float64, std::micro> spent =
            std::chrono::steady_clock::now() - started;
        std::println("{:<28} {:8.2f} us", what, spent.count() / rounds);
    };

    timed("build_occupancy", [&] {
        static_cast<void>(hills.build_occupancy(*solid));
        sink += solid->rows[100];
    });
    timed("build_x_rows", [&] {
        static_cast<void>(hills.build_x_rows(*solid));
        sink += solid->rows[100];
    });
    timed("build_rows_page_by_page", [&] {
        static_cast<void>(hills.build_rows_page_by_page(*solid));
        sink += solid->rows[100];
    });
    for (int32 level = 0; level < 3; ++level) {
        const std::span<uint8> out{bricks.data(), asset::occupancy_brick_count(level)};
        timed(std::format("pack level {}", level), [&] {
            asset::pack_occupancy_bricks(*solid, level, out);
            sink += out[7];
        });
    }

    CHECK(sink != 0);
}

TEST_CASE("packing refuses a span that is not one chunk of bricks", "[occupancy]") {
    const auto solid = std::make_unique<asset::chunk_occupancy>();
    std::vector<uint8> bricks(100);

    REQUIRE_THROWS_AS(asset::pack_occupancy_bricks(*solid, 0, bricks), std::invalid_argument);
}
