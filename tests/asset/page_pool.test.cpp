#include <catch2/catch_test_macros.hpp>

import std;

import vw.core;
import vw.asset;

using namespace vw;

TEST_CASE("fresh slots are distinct and counted", "[asset][pages]") {
    asset::page_pool pool;

    REQUIRE(pool.allocated_count() == 0);
    REQUIRE(pool.free_count() == 0);

    std::set<uint32> seen;
    for (int32 i = 0; i < 64; ++i) {
        REQUIRE(seen.insert(pool.alloc_dense()).second);
    }

    REQUIRE(pool.allocated_count() == 64);
    REQUIRE(pool.free_count() == 0);
}

TEST_CASE("a freed slot comes back and its bytes are still ours", "[asset][pages]") {
    asset::page_pool pool;

    const uint32 first = pool.alloc_dense();
    pool.get_dense(first).fill(voxel{7});

    pool.free_dense(first);
    REQUIRE(pool.allocated_count() == 0);
    REQUIRE(pool.free_count() == 1);

    const uint32 again = pool.alloc_dense();
    REQUIRE(again == first);
    REQUIRE(pool.free_count() == 0);
    REQUIRE(pool.get_dense(again)[0] == voxel{7});
}

TEST_CASE("slot addresses survive growth past a block", "[asset][pages]") {
    asset::page_pool pool;

    const uint32 early = pool.alloc_dense();
    auto* held         = &pool.get_dense(early);
    pool.get_dense(early).fill(voxel{3});

    for (uint32 i = 0; i < asset::page_pool::block_size + 16; ++i) {
        static_cast<void>(pool.alloc_dense());
    }

    REQUIRE(&pool.get_dense(early) == held);
    REQUIRE(pool.get_dense(early)[0] == voxel{3});
}

TEST_CASE("a batch hands out distinct slots and takes them all back", "[asset][pages]") {
    asset::page_pool pool;

    const auto batch = pool.alloc_dense_batch(300);
    REQUIRE(batch.size() == 300);

    const std::set<uint32> distinct{batch.begin(), batch.end()};
    REQUIRE(distinct.size() == batch.size());
    REQUIRE(pool.allocated_count() == 300);

    pool.free_dense_batch(batch);
    REQUIRE(pool.allocated_count() == 0);
    REQUIRE(pool.free_count() == 300);
}

TEST_CASE("a batch spends the free list before growing", "[asset][pages]") {
    asset::page_pool pool;

    const auto first = pool.alloc_dense_batch(10);
    pool.free_dense_batch(first);

    const auto second = pool.alloc_dense_batch(10);

    const std::set<uint32> returned{first.begin(), first.end()};
    for (const uint32 index : second) {
        REQUIRE(returned.contains(index));
    }

    REQUIRE(pool.allocated_count() == 10);
    REQUIRE(pool.free_count() == 0);
}

TEST_CASE("a batch larger than the free list mixes reuse with growth", "[asset][pages]") {
    asset::page_pool pool;

    const auto first = pool.alloc_dense_batch(4);
    pool.free_dense_batch(first);

    const auto second = pool.alloc_dense_batch(10);
    REQUIRE(second.size() == 10);

    const std::set<uint32> distinct{second.begin(), second.end()};
    REQUIRE(distinct.size() == second.size());

    const std::set<uint32> returned{first.begin(), first.end()};
    const auto reused = std::ranges::count_if(second, [&returned](uint32 index) -> bool {
        return returned.contains(index);
    });

    REQUIRE(reused == 4);
    REQUIRE(pool.free_count() == 0);
    REQUIRE(pool.allocated_count() == 10);
}

TEST_CASE("the two size classes share an index space but not memory", "[asset][pages]") {
    asset::page_pool pool;

    const uint32 dense  = pool.alloc_dense();
    const uint32 binary = pool.alloc_binary();

    REQUIRE(dense == binary);

    pool.get_dense(dense).fill(voxel{9});
    pool.get_binary(binary).fill(uint8{0xAB});

    REQUIRE(pool.get_dense(dense)[0] == voxel{9});
    REQUIRE(pool.get_binary(binary)[0] == uint8{0xAB});

    REQUIRE(pool.dense_count() == 1);
    REQUIRE(pool.binary_count() == 1);
    REQUIRE(pool.allocated_count() == 2);
}

TEST_CASE("a binary slot costs an eighth of a dense one", "[asset][pages]") {
    REQUIRE(asset::page_pool::binary_store::slot_bytes * 8 ==
            asset::page_pool::dense_store::slot_bytes);

    asset::page_pool pool;

    static_cast<void>(pool.alloc_binary());
    REQUIRE(pool.bytes_reserved() == asset::page_pool::binary_store::slot_bytes);

    static_cast<void>(pool.alloc_dense());
    REQUIRE(pool.bytes_reserved() == asset::page_pool::binary_store::slot_bytes +
                                         asset::page_pool::dense_store::slot_bytes);
}

TEST_CASE("the binary store fits the slot index the page entry can hold", "[asset][pages]") {
    REQUIRE(asset::page_pool::binary_store::capacity <= asset::page_entry::binary_limit);

    const auto entry =
        asset::page_entry::make_binary(voxel{42}, asset::page_pool::binary_store::capacity - 1);

    REQUIRE(entry.mode() == asset::page_mode::binary);
    REQUIRE(entry.fill_voxel() == voxel{42});
    REQUIRE(entry.binary_slot() == asset::page_pool::binary_store::capacity - 1);
}
