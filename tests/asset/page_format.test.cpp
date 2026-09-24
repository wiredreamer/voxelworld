#include <catch2/catch_test_macros.hpp>

import std;

import vw.core;
import vw.asset;

using namespace vw;

namespace {

constexpr int32 side = asset::model::page_size * 2;

auto scatter(int32 x, int32 y, int32 z) -> bool {
    return (((x * 73) ^ (y * 151) ^ (z * 31)) % 5) != 0;
}

}  // namespace

TEST_CASE("compaction packs one material and air into the binary class", "[asset][pages]") {
    asset::model_identity_pool ids;
    asset::page_pool pages;

    asset::model m{ids, pages, side, side, side};
    const auto stone = voxels::gray[4];

    asset::model_writer writer{m};
    for (int32 x = 0; x < side; ++x) {
        for (int32 y = 0; y < side; ++y) {
            for (int32 z = 0; z < side; ++z) {
                if (scatter(x, y, z)) {
                    writer.set(x, y, z, stone);
                }
            }
        }
    }

    REQUIRE(m.get_page_mode(0, 0, 0) == asset::page_mode::dense);
    REQUIRE(pages.dense_count() == 8);

    REQUIRE(writer.compact_pages() == 8);

    for (int32 px = 0; px < m.pages_x(); ++px) {
        for (int32 py = 0; py < m.pages_y(); ++py) {
            for (int32 pz = 0; pz < m.pages_z(); ++pz) {
                REQUIRE(m.get_page_mode(px, py, pz) == asset::page_mode::binary);
            }
        }
    }

    REQUIRE(pages.binary_count() == 8);
    REQUIRE(pages.dense_count() == 0);

    for (int32 x = 0; x < side; ++x) {
        for (int32 y = 0; y < side; ++y) {
            for (int32 z = 0; z < side; ++z) {
                const auto want = scatter(x, y, z) ? stone : voxels::air;
                REQUIRE(m.get_voxel(x, y, z) == want);
                REQUIRE(m.is_empty(x, y, z) == want.is_empty());
            }
        }
    }
}

TEST_CASE("a second material spills the page into the dense class", "[asset][pages]") {
    asset::model_identity_pool ids;
    asset::page_pool pages;

    asset::model m{ids, pages, side, side, side};
    const auto stone = voxels::gray[4];
    const auto dirt  = voxels::brown[0];

    asset::model_writer writer{m};
    for (int32 x = 0; x < side; ++x) {
        for (int32 y = 0; y < side; ++y) {
            for (int32 z = 0; z < side; ++z) {
                if (scatter(x, y, z)) {
                    writer.set(x, y, z, stone);
                }
            }
        }
    }

    REQUIRE(writer.compact_pages() == 8);
    REQUIRE(m.get_page_mode(0, 0, 0) == asset::page_mode::binary);

    writer.set(1, 2, 3, dirt);

    REQUIRE(m.get_page_mode(0, 0, 0) == asset::page_mode::dense);
    REQUIRE(m.get_page_mode(1, 1, 1) == asset::page_mode::binary);
    REQUIRE(pages.dense_count() == 1);
    REQUIRE(pages.binary_count() == 7);

    for (int32 x = 0; x < side; ++x) {
        for (int32 y = 0; y < side; ++y) {
            for (int32 z = 0; z < side; ++z) {
                const auto want = (x == 1 && y == 2 && z == 3)
                    ? dirt
                    : (scatter(x, y, z) ? stone : voxels::air);
                REQUIRE(m.get_voxel(x, y, z) == want);
            }
        }
    }
}

TEST_CASE("digging a solid page leaves it binary, not dense", "[asset][pages]") {
    asset::model_identity_pool ids;
    asset::page_pool pages;

    asset::model m{ids, pages, side, side, side};
    m.fill(voxels::gray[4]);

    REQUIRE(m.get_page_mode(0, 0, 0) == asset::page_mode::uniform);

    m.set_voxel(2, 2, 2, voxels::air);

    REQUIRE(m.get_page_mode(0, 0, 0) == asset::page_mode::binary);
    REQUIRE(pages.dense_count() == 0);
    REQUIRE(m.get_voxel(2, 2, 2) == voxels::air);
    REQUIRE(m.get_voxel(2, 2, 3) == voxels::gray[4]);
    REQUIRE(m.get_voxel(3, 2, 2) == voxels::gray[4]);
}

TEST_CASE("compaction moves a dense page down to its tightest class", "[asset][pages]") {
    asset::model_identity_pool ids;
    asset::page_pool pages;

    asset::model m{ids, pages, side, side, side};
    const auto stone = voxels::gray[4];
    const auto dirt  = voxels::brown[0];

    asset::model_writer writer{m};
    writer.set(0, 0, 0, stone);
    writer.set(1, 0, 0, dirt);

    REQUIRE(m.get_page_mode(0, 0, 0) == asset::page_mode::dense);
    REQUIRE(pages.dense_count() == 1);

    writer.set(1, 0, 0, stone);

    REQUIRE(writer.compact_pages() == 1);
    REQUIRE(m.get_page_mode(0, 0, 0) == asset::page_mode::binary);
    REQUIRE(pages.dense_count() == 0);
    REQUIRE(pages.binary_count() == 1);

    REQUIRE(m.get_voxel(0, 0, 0) == stone);
    REQUIRE(m.get_voxel(1, 0, 0) == stone);
    REQUIRE(m.get_voxel(2, 0, 0) == voxels::air);
}

TEST_CASE("compaction retires a binary page that lost its air", "[asset][pages]") {
    asset::model_identity_pool ids;
    asset::page_pool pages;

    asset::model m{ids, pages, side, side, side};
    m.fill(voxels::gray[4]);
    m.set_voxel(2, 2, 2, voxels::air);

    REQUIRE(m.get_page_mode(0, 0, 0) == asset::page_mode::binary);

    m.set_voxel(2, 2, 2, voxels::gray[4]);

    REQUIRE(m.compact_pages() == 1);
    REQUIRE(m.get_page_mode(0, 0, 0) == asset::page_mode::uniform);
    REQUIRE(pages.binary_count() == 0);
    REQUIRE(m.scan_fill() == asset::model_fill::solid);
}

TEST_CASE("a model hands every slot of both classes back", "[asset][pages]") {
    asset::model_identity_pool ids;
    asset::page_pool pages;

    {
        asset::model m{ids, pages, side, side, side};
        m.fill(voxels::gray[4]);
        m.set_voxel(9, 9, 9, voxels::air);

        asset::model_writer writer{m};
        writer.set(0, 0, 0, voxels::brown[0]);

        REQUIRE(pages.dense_count() == 1);
        REQUIRE(pages.binary_count() == 1);
    }

    REQUIRE(pages.allocated_count() == 0);
}

TEST_CASE("a clone carries both page classes without sharing slots", "[asset][pages]") {
    asset::model_identity_pool ids;
    asset::page_pool pages;

    asset::model source{ids, pages, side, side, side};
    source.fill(voxels::gray[4]);
    source.set_voxel(9, 9, 9, voxels::air);
    {
        asset::model_writer writer{source};
        writer.set(0, 0, 0, voxels::brown[0]);
    }

    REQUIRE(source.get_page_mode(0, 0, 0) == asset::page_mode::dense);
    REQUIRE(source.get_page_mode(1, 1, 1) == asset::page_mode::binary);

    asset::model copy{ids, pages, side, side, side};
    copy.clone_pages_from(source);

    REQUIRE(pages.dense_count() == 2);
    REQUIRE(pages.binary_count() == 2);

    copy.set_voxel(9, 9, 8, voxels::air);

    REQUIRE(source.get_voxel(9, 9, 8) == voxels::gray[4]);
    REQUIRE(copy.get_voxel(9, 9, 8) == voxels::air);
    REQUIRE(copy.get_voxel(9, 9, 9) == voxels::air);
    REQUIRE(copy.get_voxel(0, 0, 0) == voxels::brown[0]);
    REQUIRE(copy.get_voxel(1, 0, 0) == voxels::gray[4]);
}

TEST_CASE("compaction packs three materials into the palette class", "[asset][pages]") {
    asset::model_identity_pool ids;
    asset::page_pool pages;

    asset::model m{ids, pages, side, side, side};
    const std::array shades{voxels::gray[4], voxels::brown[0], voxels::green[2]};

    {
        asset::model_writer writer{m};
        for (int32 x = 0; x < side; ++x) {
            for (int32 y = 0; y < side; ++y) {
                for (int32 z = 0; z < side; ++z) {
                    if (scatter(x, y, z)) {
                        writer.set(x, y, z, shades[static_cast<std::size_t>((x + y + z) % 3)]);
                    }
                }
            }
        }
    }

    REQUIRE(pages.dense_count() == 8);

    REQUIRE(m.compact_pages() == 8);

    REQUIRE(pages.palette_count() == 8);
    REQUIRE(pages.dense_count() == 0);

    for (int32 x = 0; x < side; ++x) {
        for (int32 y = 0; y < side; ++y) {
            for (int32 z = 0; z < side; ++z) {
                const auto want = scatter(x, y, z)
                                      ? shades[static_cast<std::size_t>((x + y + z) % 3)]
                                      : voxels::air;
                REQUIRE(m.get_voxel(x, y, z) == want);
                REQUIRE(m.is_empty(x, y, z) == want.is_empty());
            }
        }
    }
}

TEST_CASE("a sixteenth material is the last the palette class holds", "[asset][pages]") {
    asset::model_identity_pool ids;
    asset::page_pool pages;

    const auto fill_with = [&](int32 distinct) -> asset::page_mode {
        asset::model m{ids, pages, side, side, side};
        {
            asset::model_writer writer{m};
            for (int32 x = 0; x < asset::model::page_size; ++x) {
                for (int32 y = 0; y < asset::model::page_size; ++y) {
                    for (int32 z = 0; z < asset::model::page_size; ++z) {
                        const int32 local = x + (y * 8) + (z * 64);
                        writer.set(
                            x, y, z, voxel{static_cast<uint8>(1 + (local % distinct))}
                        );
                    }
                }
            }
        }
        const auto packed = m.compact_pages();
        REQUIRE(packed <= 1);
        return m.get_page_mode(0, 0, 0);
    };

    REQUIRE(fill_with(15) == asset::page_mode::palette);
    REQUIRE(fill_with(16) == asset::page_mode::dense);
}

TEST_CASE("a write reopens a palette page into the dense class", "[asset][pages]") {
    asset::model_identity_pool ids;
    asset::page_pool pages;

    asset::model m{ids, pages, side, side, side};
    {
        asset::model_writer writer{m};
        writer.set(0, 0, 0, voxels::gray[4]);
        writer.set(1, 0, 0, voxels::brown[0]);
        writer.set(2, 0, 0, voxels::green[2]);
    }

    REQUIRE(m.compact_pages() == 1);
    REQUIRE(m.get_page_mode(0, 0, 0) == asset::page_mode::palette);

    m.set_voxel(3, 0, 0, voxels::blue[1]);

    REQUIRE(m.get_page_mode(0, 0, 0) == asset::page_mode::dense);
    REQUIRE(pages.palette_count() == 0);

    REQUIRE(m.get_voxel(0, 0, 0) == voxels::gray[4]);
    REQUIRE(m.get_voxel(1, 0, 0) == voxels::brown[0]);
    REQUIRE(m.get_voxel(2, 0, 0) == voxels::green[2]);
    REQUIRE(m.get_voxel(3, 0, 0) == voxels::blue[1]);
    REQUIRE(m.get_voxel(4, 0, 0) == voxels::air);
}

TEST_CASE("a palette page reports the same rows as a dense one", "[asset][pages]") {
    constexpr int32 chunk = asset::chunk_occupancy::side;

    asset::model_identity_pool ids;
    asset::page_pool pages;

    asset::model packed{ids, pages, chunk, chunk, chunk};
    asset::model loose{ids, pages, chunk, chunk, chunk};

    const std::array shades{voxels::gray[4], voxels::brown[0], voxels::green[2]};

    {
        asset::model_writer to_packed{packed};
        asset::model_writer to_loose{loose};
        for (int32 x = 0; x < chunk; ++x) {
            for (int32 y = 0; y < chunk; ++y) {
                for (int32 z = 0; z < chunk; ++z) {
                    if (!scatter(x, y, z)) {
                        continue;
                    }
                    const auto v = shades[static_cast<std::size_t>((x + y + z) % 3)];
                    to_packed.set(x, y, z, v);
                    to_loose.set(x, y, z, v);
                }
            }
        }
    }

    REQUIRE(packed.compact_pages() > 0);
    REQUIRE(pages.palette_count() > 0);
    REQUIRE(packed.get_page_mode(0, 0, 0) == asset::page_mode::palette);
    REQUIRE(loose.get_page_mode(0, 0, 0) == asset::page_mode::dense);

    asset::chunk_occupancy from_packed;
    asset::chunk_occupancy from_loose;
    REQUIRE(packed.build_occupancy(from_packed));
    REQUIRE(loose.build_occupancy(from_loose));

    for (int32 y = 0; y < chunk; ++y) {
        for (int32 z = 0; z < chunk; ++z) {
            REQUIRE(from_packed.row(y, z) == from_loose.row(y, z));
        }
    }
}

TEST_CASE("a model hands its palette slots back", "[asset][pages]") {
    asset::model_identity_pool ids;
    asset::page_pool pages;

    {
        asset::model m{ids, pages, side, side, side};
        {
            asset::model_writer writer{m};
            writer.set(0, 0, 0, voxels::gray[4]);
            writer.set(1, 0, 0, voxels::brown[0]);
            writer.set(2, 0, 0, voxels::green[2]);
        }
        REQUIRE(m.compact_pages() == 1);
        REQUIRE(pages.palette_count() == 1);
    }

    REQUIRE(pages.allocated_count() == 0);
}

TEST_CASE("a clone carries a palette page without sharing its slot", "[asset][pages]") {
    asset::model_identity_pool ids;
    asset::page_pool pages;

    asset::model source{ids, pages, side, side, side};
    {
        asset::model_writer writer{source};
        writer.set(0, 0, 0, voxels::gray[4]);
        writer.set(1, 0, 0, voxels::brown[0]);
        writer.set(2, 0, 0, voxels::green[2]);
    }
    REQUIRE(source.compact_pages() == 1);
    REQUIRE(source.get_page_mode(0, 0, 0) == asset::page_mode::palette);

    asset::model copy{ids, pages, side, side, side};
    copy.clone_pages_from(source);

    REQUIRE(pages.palette_count() == 2);

    copy.set_voxel(1, 0, 0, voxels::blue[1]);

    REQUIRE(source.get_voxel(1, 0, 0) == voxels::brown[0]);
    REQUIRE(copy.get_voxel(1, 0, 0) == voxels::blue[1]);
    REQUIRE(copy.get_voxel(0, 0, 0) == voxels::gray[4]);
    REQUIRE(copy.get_voxel(2, 0, 0) == voxels::green[2]);
}
