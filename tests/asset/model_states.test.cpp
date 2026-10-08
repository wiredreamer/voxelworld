#include <catch2/catch_template_test_macros.hpp>
#include <catch2/catch_test_macros.hpp>

import std;

import vw.core;
import vw.asset;

using namespace vw;

namespace {

constexpr int32 side = 64;

template <typename Value>
auto marked(uint8 raw) -> Value {
    return std::bit_cast<Value>(raw);
}

}  // namespace

TEMPLATE_TEST_CASE("a layer holds nothing until a value is set and folds back when it leaves",
                   "[model][layer]", material, voxel_state) {
    asset::voxel_layer<TestType> layer{8};

    REQUIRE(layer.blank());
    REQUIRE(layer.get(3, 100) == TestType{});
    REQUIRE(layer.whole_page(3) == TestType{});

    layer.set(3, 100, TestType{});
    REQUIRE(layer.blank());

    layer.set(3, 100, marked<TestType>(7));
    REQUIRE_FALSE(layer.blank());
    REQUIRE(layer.get(3, 100) == marked<TestType>(7));
    REQUIRE(layer.get(3, 101) == TestType{});
    REQUIRE(layer.get(4, 100) == TestType{});
    REQUIRE_FALSE(layer.whole_page(3).has_value());
    REQUIRE(layer.whole_page(4) == TestType{});

    layer.set(3, 100, TestType{});
    layer.fold();
    REQUIRE(layer.blank());
}

TEMPLATE_TEST_CASE("a page of one value is a single number", "[model][layer]", material,
                   voxel_state) {
    asset::voxel_layer<TestType> layer{8};

    layer.fill_page(5, marked<TestType>(9));
    REQUIRE(layer.whole_page(5) == marked<TestType>(9));
    REQUIRE(layer.get(5, 0) == marked<TestType>(9));
    REQUIRE(layer.get(5, 511) == marked<TestType>(9));

    layer.set(5, 17, marked<TestType>(2));
    REQUIRE_FALSE(layer.whole_page(5).has_value());
    REQUIRE(layer.get(5, 17) == marked<TestType>(2));
    REQUIRE(layer.get(5, 18) == marked<TestType>(9));

    layer.set(5, 17, marked<TestType>(9));
    layer.fold();
    REQUIRE(layer.whole_page(5) == marked<TestType>(9));

    layer.fill(TestType{});
    REQUIRE(layer.blank());
}

TEST_CASE("the shown code is the low six bits of a state", "[voxels][state]") {
    REQUIRE(shown_code(voxel_state{}) == 0);
    REQUIRE(shown_code(voxel_state{5}) == 5);
    REQUIRE(shown_code(voxel_state{63}) == 63);
    REQUIRE(shown_code(voxel_state{64 + 5}) == 5);
}

TEST_CASE("a state sticks to an occupied voxel and never to air", "[model][state]") {
    asset::model_identity_pool ids;
    asset::page_pool pages;

    asset::model m{ids, pages, side, side, side};
    m.set_voxel(3, 4, 5, voxels::gray[8]);

    const auto before = m.get_identity();
    m.set_state(3, 4, 5, voxel_state{9});
    REQUIRE(m.get_state(3, 4, 5) == voxel_state{9});
    REQUIRE(m.get_identity() != before);

    const auto settled = m.get_identity();
    m.set_state(10, 10, 10, voxel_state{9});
    REQUIRE(m.get_state(10, 10, 10) == voxel_state{});
    REQUIRE(m.get_identity() == settled);
    REQUIRE_FALSE(m.get_page_state(0, 0, 0).has_value());
    REQUIRE(m.get_page_state(7, 7, 7) == voxel_state{});
}

TEST_CASE("clearing or replacing a voxel drops its state, rewriting it as it was keeps it", "[model][state]") {
    asset::model_identity_pool ids;
    asset::page_pool pages;

    asset::model m{ids, pages, side, side, side};
    m.set_voxel(3, 4, 5, voxels::gray[8]);
    m.set_voxel(4, 4, 5, voxels::gray[8]);
    m.set_voxel(5, 4, 5, voxels::gray[8]);
    m.set_voxel(6, 4, 5, voxels::gray[8], materials::wood);
    for (int32 x = 3; x <= 6; ++x) {
        m.set_state(x, 4, 5, voxel_state{9});
    }

    m.set_voxel(3, 4, 5, voxels::air);
    m.set_voxel(4, 4, 5, voxels::gray[9]);
    m.set_voxel(5, 4, 5, voxels::gray[8]);
    m.set_voxel(6, 4, 5, voxels::gray[8], materials::glow);

    REQUIRE(m.get_state(3, 4, 5) == voxel_state{});
    REQUIRE(m.get_state(4, 4, 5) == voxel_state{});
    REQUIRE(m.get_state(5, 4, 5) == voxel_state{9});
    REQUIRE(m.get_state(6, 4, 5) == voxel_state{});
}

TEST_CASE("a clone carries states, a fill drops them", "[model][state]") {
    asset::model_identity_pool ids;
    asset::page_pool pages;

    asset::model source{ids, pages, side, side, side};
    source.set_voxel(3, 4, 5, voxels::gray[8]);
    source.set_state(3, 4, 5, voxel_state{9});

    asset::model copy{ids, pages, side, side, side};
    copy.clone_pages_from(source);
    REQUIRE(copy.get_state(3, 4, 5) == voxel_state{9});

    source.set_state(3, 4, 5, voxel_state{2});
    REQUIRE(copy.get_state(3, 4, 5) == voxel_state{9});

    copy.fill(voxels::gray[8]);
    REQUIRE(copy.get_state(3, 4, 5) == voxel_state{});
    REQUIRE(copy.get_page_state(0, 0, 0) == voxel_state{});
}

TEST_CASE("a page whose states all match folds on compaction", "[model][state]") {
    asset::model_identity_pool ids;
    asset::page_pool pages;

    asset::model m{ids, pages, side, side, side};
    for (int32 z = 8; z < 16; ++z) {
        for (int32 y = 0; y < 8; ++y) {
            for (int32 x = 16; x < 24; ++x) {
                m.set_voxel(x, y, z, voxels::gray[8]);
                m.set_state(x, y, z, voxel_state{4});
            }
        }
    }
    REQUIRE_FALSE(m.get_page_state(2, 0, 1).has_value());

    static_cast<void>(m.compact_pages());
    REQUIRE(m.get_page_state(2, 0, 1) == voxel_state{4});
    REQUIRE(m.get_state(20, 3, 12) == voxel_state{4});
}
