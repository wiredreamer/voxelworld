#include <catch2/catch_test_macros.hpp>


import vw.core;

using namespace vw;

TEST_CASE("voxel default constructor", "[voxel]") {
    voxel v;
    REQUIRE(v.is_empty());
    REQUIRE(v.id == blocks::air);
}

TEST_CASE("voxel id constructor", "[voxel]") {
    voxel v{blocks::terrain::dirt[0]};
    REQUIRE_FALSE(v.is_empty());
    REQUIRE(v.id == blocks::terrain::dirt[0]);
}

// Emptiness is the raw value being zero, and the page table, the occupancy walk
// and the light flood all lean on that. Only category zero index zero is air; an
// index of zero in any other category is an ordinary block.
TEST_CASE("voxel is_empty", "[voxel]") {
    REQUIRE(voxel{}.is_empty());
    REQUIRE(voxel{block_id{}}.is_empty());
    REQUIRE(blocks::air.value == 0);

    REQUIRE_FALSE(voxel{blocks::terrain::grass[0]}.is_empty());
    REQUIRE_FALSE(voxel{blocks::palette::amber[3]}.is_empty());
    REQUIRE_FALSE(voxel{block_id::from_raw(0xFFFF)}.is_empty());
}

TEST_CASE("voxel empty_voxel constant", "[voxel]") {
    static_assert(empty_voxel.is_empty());
    REQUIRE(empty_voxel.is_empty());
    REQUIRE(empty_voxel.id == blocks::air);
}

TEST_CASE("voxel equality", "[voxel]") {
    REQUIRE(voxel{blocks::terrain::grass[0]} == voxel{blocks::terrain::grass[0]});
    REQUIRE_FALSE(voxel{blocks::terrain::grass[0]} == voxel{blocks::terrain::grass[1]});
}

// A voxel value carries a full identity, but a page stores only the index
// within its model's set -- one byte, which is what model::build_x_rows folds
// eight at a time out of a machine word.
TEST_CASE("a page entry is one byte, an identity two", "[voxel]") {
    static_assert(sizeof(block_index) == 1);
    static_assert(sizeof(block_id) == 2);
}

// Zero is empty in every set, and the catalog numbers from one so that a zero
// byte can only ever mean air.
TEST_CASE("index zero is empty in any set", "[voxel]") {
    REQUIRE(block_index{}.is_empty());
    REQUIRE(block_index{0}.is_empty());
    REQUIRE_FALSE(block_index{1}.is_empty());

    REQUIRE(blocks::terrain::grass[0].index() >= 1);
    REQUIRE(blocks::palette::blue[0].index() >= 1);
}
