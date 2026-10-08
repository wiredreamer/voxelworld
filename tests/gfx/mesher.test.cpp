#include <catch2/catch_test_macros.hpp>

import std;

import vw.core;
import vw.asset;
import vw.gfx;

using namespace vw;

namespace {

struct face_cell {
    int32 x = 0;
    int32 y = 0;
    int32 z = 0;
    uint8 normal = 0;
    uint16 slot  = 0;

    auto operator<=>(const face_cell&) const = default;
};

constexpr std::array<int32, 6> voxel_vert_tangent_u_axis{2, 2, 0, 0, 0, 0};
constexpr std::array<int32, 6> voxel_vert_tangent_v_axis{1, 1, 2, 2, 1, 1};

auto unpack_min(const gfx::quad& q) -> vec3i {
    return {
        static_cast<int32>(q.data0 & 0x7FU),
        static_cast<int32>((q.data0 >> 7) & 0x7FU),
        static_cast<int32>((q.data0 >> 14) & 0x7FU),
    };
}

auto unpack_normal(const gfx::quad& q) -> uint8 {
    return static_cast<uint8>((q.data0 >> 21) & 0x7U);
}

auto unpack_max(const gfx::quad& q) -> vec3i {
    const uint8 normal    = unpack_normal(q);
    const int32 face_axis = normal / 2;
    const int32 u_extent  = static_cast<int32>(q.data1 & 0x7FU) + 1;
    const int32 v_extent  = static_cast<int32>((q.data1 >> 7) & 0x7FU) + 1;

    vec3i far_corner = unpack_min(q);
    far_corner[face_axis] += 1;
    far_corner[voxel_vert_tangent_u_axis[normal]] += u_extent;
    far_corner[voxel_vert_tangent_v_axis[normal]] += v_extent;
    return far_corner;
}

auto unpack_slot(const gfx::quad& q) -> uint16 {
    return static_cast<uint16>((q.data1 >> 14) & 0xFFU);
}

auto unpack_material(const gfx::quad& q) -> material {
    return material{static_cast<uint8>(q.data0 >> 24)};
}

auto unpack_sways(const gfx::quad& q) -> bool {
    return ((q.data1 >> 22) & 0x1U) != 0;
}

constexpr vec3i face_normal[6] = {
    {1, 0, 0}, {-1, 0, 0}, {0, 1, 0}, {0, -1, 0}, {0, 0, 1}, {0, 0, -1},
};

auto to_face_cells(const gfx::mesh& m) -> std::set<face_cell> {
    std::set<face_cell> cells;

    for (const auto& q : m.quads) {
        const auto lo = unpack_min(q);
        const auto hi = unpack_max(q);

        const auto normal = unpack_normal(q);
        const auto slot   = unpack_slot(q);

        const int32 sx = std::max(1, hi.x - lo.x);
        const int32 sy = std::max(1, hi.y - lo.y);
        const int32 sz = std::max(1, hi.z - lo.z);

        for (int32 dx = 0; dx < sx; ++dx) {
            for (int32 dy = 0; dy < sy; ++dy) {
                for (int32 dz = 0; dz < sz; ++dz) {
                    cells.insert(
                        face_cell{lo.x + dx, lo.y + dy, lo.z + dz, normal, slot}
                    );
                }
            }
        }
    }

    return cells;
}

auto unpack_extents(const gfx::quad& q) -> std::pair<int32, int32> {
    return {
        static_cast<int32>(q.data1 & 0x7FU) + 1,
        static_cast<int32>((q.data1 >> 7) & 0x7FU) + 1,
    };
}

auto quads_facing(const gfx::mesh& m, uint8 normal) -> std::vector<gfx::quad> {
    std::vector<gfx::quad> picked;
    for (const auto& q : m.quads) {
        if (unpack_normal(q) == normal) {
            picked.push_back(q);
        }
    }
    return picked;
}

auto quad_carries(const gfx::mesh& m, voxel v) -> bool {
    return std::ranges::all_of(m.quads, [v](const gfx::quad& q) {
        return ((q.data1 >> 14) & 0xFFu) == v.value;
    });
}

auto hash_mesh(const gfx::mesh& m) -> uint64 {
    uint64 hash = 1469598103934665603ULL;
    const auto mix = [&hash](uint32 value) {
        for (uint32 byte = 0; byte < 4; ++byte) {
            hash ^= (value >> (byte * 8)) & 0xFFU;
            hash *= 1099511628211ULL;
        }
    };

    for (const auto& q : m.quads) {
        mix(q.data0);
        mix(q.data1);
    }

    return hash;
}

class model_fixture {
public:
    explicit model_fixture(int32 size) : size_{size} {
        model_ = std::make_shared<asset::model>(identity_pool_, pages_, size, size, size);
        chunk_ = std::make_shared<asset::chunk_volume>(model_);
    }

    [[nodiscard]] auto get() const -> const std::shared_ptr<asset::model>& {
        return model_;
    }

    [[nodiscard]] auto chunk() const -> asset::chunk_volume& {
        return *chunk_;
    }

    [[nodiscard]] auto source() const -> gfx::mesh_source {
        return gfx::mesh_source{
            .voxels   = *model_,
            .boundary = chunk_->share_boundary().get()
        };
    }

    [[nodiscard]] auto size() const -> int32 {
        return size_;
    }

    [[nodiscard]] auto greedy(gfx::mesh_options opts = {}) -> gfx::mesh {
        gfx::mesh_generation_storage storage;
        return gfx::greedy_mesh_generator::generate_mesh_data(storage, source(), opts);
    }

    [[nodiscard]] auto simple() const -> gfx::mesh {
        return gfx::simple_mesh_generator::generate_mesh_data(source());
    }

private:
    int32 size_;
    asset::model_identity_pool identity_pool_;
    asset::page_pool pages_;
    std::shared_ptr<asset::model> model_;
    std::shared_ptr<asset::chunk_volume> chunk_;
};

}  // namespace

TEST_CASE("greedy meshing agrees with per-voxel meshing", "[mesh]") {
    SECTION("empty model") {
        model_fixture fixture{16};
        REQUIRE(fixture.greedy().quads.empty());
    }

    SECTION("single voxel") {
        model_fixture fixture{16};
        fixture.get()->set_voxel(4, 5, 6, voxels::amber[6]);

        const auto greedy = fixture.greedy();
        REQUIRE(greedy.quads.size() == 6);
        REQUIRE(to_face_cells(greedy) == to_face_cells(fixture.simple()));
    }

    SECTION("voxel in the corner") {
        model_fixture fixture{16};
        fixture.get()->set_voxel(0, 0, 0, voxels::gray[10]);
        REQUIRE(to_face_cells(fixture.greedy()) == to_face_cells(fixture.simple()));
    }

    SECTION("solid block merges into six quads") {
        model_fixture fixture{16};
        fixture.get()->fill(voxels::green[4]);

        const auto greedy = fixture.greedy();
        REQUIRE(greedy.quads.size() == 6);
        REQUIRE(to_face_cells(greedy) == to_face_cells(fixture.simple()));
    }

    SECTION("checkerboard defeats merging but not correctness") {
        model_fixture fixture{16};
        for (int32 x = 0; x < fixture.size(); ++x) {
            for (int32 y = 0; y < fixture.size(); ++y) {
                for (int32 z = 0; z < fixture.size(); ++z) {
                    if (((x + y + z) % 2) == 0) {
                        fixture.get()->set_voxel(x, y, z, voxels::blue[8]);
                    }
                }
            }
        }
        REQUIRE(to_face_cells(fixture.greedy()) == to_face_cells(fixture.simple()));
    }

    SECTION("two materials never merge across the seam") {
        model_fixture fixture{16};
        for (int32 x = 0; x < fixture.size(); ++x) {
            for (int32 y = 0; y < 4; ++y) {
                for (int32 z = 0; z < fixture.size(); ++z) {
                    const voxel id = (x < 8) ? voxels::brown[4] : voxels::gray[4];
                    fixture.get()->set_voxel(x, y, z, id);
                }
            }
        }
        REQUIRE(to_face_cells(fixture.greedy()) == to_face_cells(fixture.simple()));
    }

    SECTION("full-size chunk, terrain-like") {
        model_fixture fixture{64};
        uint32 state = 777;
        for (int32 x = 0; x < fixture.size(); ++x) {
            for (int32 z = 0; z < fixture.size(); ++z) {
                state = (state * 1664525U) + 1013904223U;
                const int32 height = 8 + static_cast<int32>((state >> 26) % 24);
                for (int32 y = 0; y < height; ++y) {
                    fixture.get()->set_voxel(x, y, z, voxels::brown[6]);
                }
            }
        }
        REQUIRE(to_face_cells(fixture.greedy()) == to_face_cells(fixture.simple()));
    }

    SECTION("full-size chunk, sparse noise") {
        model_fixture fixture{64};
        uint32 state = 31337;
        for (int32 x = 0; x < fixture.size(); ++x) {
            for (int32 y = 0; y < fixture.size(); ++y) {
                for (int32 z = 0; z < fixture.size(); ++z) {
                    state = (state * 1664525U) + 1013904223U;
                    if ((state >> 29) == 0) {
                        fixture.get()->set_voxel(
                            x, y, z, voxel{static_cast<uint8>(state % 40)}
                        );
                    }
                }
            }
        }
        REQUIRE(to_face_cells(fixture.greedy()) == to_face_cells(fixture.simple()));
    }

    SECTION("full-size chunk, solid") {
        model_fixture fixture{64};
        fixture.get()->fill(voxels::gray[12]);

        const auto greedy = fixture.greedy();
        REQUIRE(greedy.quads.size() == 6);
        REQUIRE(to_face_cells(greedy) == to_face_cells(fixture.simple()));
    }

    SECTION("pseudo-random fill") {
        model_fixture fixture{32};
        uint32 state = 12345;
        for (int32 x = 0; x < fixture.size(); ++x) {
            for (int32 y = 0; y < fixture.size(); ++y) {
                for (int32 z = 0; z < fixture.size(); ++z) {
                    state = (state * 1664525U) + 1013904223U;
                    if ((state >> 28) < 6) {
                        fixture.get()->set_voxel(x, y, z, voxel{static_cast<uint8>(state % 40)});
                    }
                }
            }
        }
        REQUIRE(to_face_cells(fixture.greedy()) == to_face_cells(fixture.simple()));
    }
}

TEST_CASE("boundary faces close the seam between chunks", "[mesh]") {
    asset::model_identity_pool identity_pool;
    asset::page_pool pages;

    constexpr int32 size = 64;
    auto left  = std::make_shared<asset::model>(identity_pool, pages, size, size, size);
    auto right = std::make_shared<asset::model>(identity_pool, pages, size, size, size);

    left->fill(voxels::gray[8]);
    right->fill(voxels::gray[8]);

    asset::chunk_volume left_chunk{left};

    const auto count_faces = [](const asset::chunk_volume& c, uint8 normal) {
        gfx::mesh_generation_storage storage;
        const auto mesh = gfx::greedy_mesh_generator::generate_mesh_data(
            storage,
            gfx::mesh_source{
                .voxels   = c.voxels(),
                .boundary = c.share_boundary().get()
            });

        std::size_t count = 0;
        for (const auto& cell : to_face_cells(mesh)) {
            if (cell.normal == normal) {
                ++count;
            }
        }
        return count;
    };

    REQUIRE(count_faces(left_chunk, 0) == size * size);
    const auto before_minus_x = count_faces(left_chunk, 1);

    left_chunk.set_boundary_slice(face_direction::pos_x, *right);

    REQUIRE(left_chunk.has_boundary_slice(face_direction::pos_x));
    REQUIRE(count_faces(left_chunk, 0) == 0);
    REQUIRE(count_faces(left_chunk, 1) == before_minus_x);
}

TEST_CASE("a floor stays one quad right up to the wall standing on it", "[mesh]") {
    model_fixture fixture{16};

    constexpr int32 side  = 16;
    constexpr int32 floor = 4;
    constexpr int32 wall  = 8;

    for (int32 x = 0; x < side; ++x) {
        for (int32 z = 0; z < side; ++z) {
            fixture.get()->set_voxel(x, floor, z, voxels::gray[10]);
            for (int32 y = floor + 1; x >= wall && y < floor + 4; ++y) {
                fixture.get()->set_voxel(x, y, z, voxels::gray[10]);
            }
        }
    }

    const auto up = static_cast<uint8>(std::to_underlying(face_direction::pos_y));

    std::size_t floor_quads = 0;
    for (const auto& q : fixture.greedy().quads) {
        if (unpack_normal(q) != up || unpack_min(q).y != floor) {
            continue;
        }
        ++floor_quads;
        REQUIRE(unpack_min(q) == vec3i{0, floor, 0});
        REQUIRE(unpack_extents(q) == std::pair<int32, int32>{wall, side});
        REQUIRE((q.data0 >> 24) == 0);
        REQUIRE((q.data1 >> 24) == 0);
    }

    REQUIRE(floor_quads == 1);
}

TEST_CASE("greedy meshing output is stable", "[mesh]") {
    model_fixture fixture{32};

    uint32 state = 999;
    for (int32 x = 0; x < fixture.size(); ++x) {
        for (int32 z = 0; z < fixture.size(); ++z) {
            state = (state * 1664525U) + 1013904223U;
            const int32 height = 4 + static_cast<int32>((state >> 27) % 8);
            for (int32 y = 0; y < height; ++y) {
                fixture.get()->set_voxel(x, y, z, voxels::green[6]);
            }
        }
    }

    const auto mesh = fixture.greedy();
    REQUIRE(mesh.quads.size() > 0);

    const auto digest = hash_mesh(mesh);
    INFO("mesh digest: " << digest << ", quads: " << mesh.quads.size());
    REQUIRE(mesh.quads.size() == 2632);
    REQUIRE(quad_carries(mesh, voxels::green[6]));
    REQUIRE(digest == 2061791383763684293ULL);
}

TEST_CASE("full-size greedy meshing output is stable", "[mesh]") {
    model_fixture fixture{64};

    uint32 state = 20250815;
    for (int32 x = 0; x < fixture.size(); ++x) {
        for (int32 z = 0; z < fixture.size(); ++z) {
            state = (state * 1664525U) + 1013904223U;
            const int32 height = 6 + static_cast<int32>((state >> 26) % 20);
            for (int32 y = 0; y < height; ++y) {
                fixture.get()->set_voxel(x, y, z, voxels::green[6]);
            }
        }
    }

    const auto mesh = fixture.greedy();
    const auto digest = hash_mesh(mesh);
    INFO("full-size digest: " << digest << ", quads: " << mesh.quads.size());
    REQUIRE(mesh.quads.size() == 11499);
    REQUIRE(quad_carries(mesh, voxels::green[6]));
    REQUIRE(digest == 12132135778226986332ULL);
}


TEST_CASE("a coarse step inflates a lone voxel to a whole cell", "[mesh][lod]") {
    model_fixture fixture{64};
    fixture.get()->set_voxel(10, 10, 10, voxels::gray[8]);

    const auto fine = fixture.greedy();
    REQUIRE(fine.quads.size() == 6);
    for (const auto& q : fine.quads) {
        REQUIRE(unpack_min(q) == vec3i{10, 10, 10});
        REQUIRE(unpack_extents(q) == std::pair<int32, int32>{1, 1});
    }

    const auto coarse = fixture.greedy({.lod_step = 2});
    REQUIRE(coarse.quads.size() == 6);
    for (const auto& q : coarse.quads) {
        const auto normal = unpack_normal(q);
        const int32 axis  = normal / 2;

        vec3i want{10, 10, 10};
        if ((normal % 2) == 0) {
            want[axis] = 11;
        }

        REQUIRE(unpack_min(q) == want);
        REQUIRE(unpack_extents(q) == std::pair<int32, int32>{2, 2});
        REQUIRE(unpack_slot(q) == unpack_slot(fine.quads.front()));
    }
}

TEST_CASE("a coarse cell wears the material of its top voxel", "[mesh][lod]") {
    model_fixture fixture{64};

    for (int32 x = 10; x < 12; ++x) {
        for (int32 y = 10; y < 12; ++y) {
            for (int32 z = 10; z < 12; ++z) {
                fixture.get()->set_voxel(x, y, z, voxels::gray[8]);
            }
        }
    }
    for (int32 x = 10; x < 12; ++x) {
        for (int32 z = 10; z < 12; ++z) {
            fixture.get()->set_voxel(x, 11, z, voxels::green[6]);
        }
    }

    const auto up   = static_cast<uint8>(std::to_underlying(face_direction::pos_y));
    const auto fine = quads_facing(fixture.greedy(), up);
    REQUIRE(fine.size() == 1);

    const auto grass_slot = unpack_slot(fine.front());
    for (const auto& q : fine) {
        REQUIRE(unpack_slot(q) == grass_slot);
    }

    const auto coarse = quads_facing(fixture.greedy({.lod_step = 2}), up);
    REQUIRE(coarse.size() == 1);
    REQUIRE(unpack_slot(coarse.front()) == grass_slot);
}

TEST_CASE("a coarse step flattens detail the fine mesh keeps", "[mesh][lod]") {
    model_fixture fixture{64};

    for (int32 x = 0; x < 64; ++x) {
        for (int32 z = 0; z < 64; ++z) {
            const int32 height = 8 + ((x + z) % 2);
            for (int32 y = 0; y < height; ++y) {
                fixture.get()->set_voxel(x, y, z, voxels::green[6]);
            }
        }
    }

    const auto up = std::to_underlying(face_direction::pos_y);

    const auto fine = quads_facing(fixture.greedy(), static_cast<uint8>(up));
    REQUIRE(fine.size() > 1000);

    const auto coarse = quads_facing(fixture.greedy({.lod_step = 2}), static_cast<uint8>(up));
    REQUIRE(coarse.size() == 1);
    REQUIRE(unpack_min(coarse.front()) == vec3i{0, 9, 0});
    REQUIRE(unpack_extents(coarse.front()) == std::pair<int32, int32>{64, 64});
}

TEST_CASE("a coarse step reads the neighbour at its own resolution", "[mesh][lod]") {
    asset::model_identity_pool identity_pool;
    asset::page_pool pages;

    constexpr int32 size = 64;
    constexpr int32 half = size / 2;

    auto left  = std::make_shared<asset::model>(
        identity_pool, pages, size, size, size);
    auto right = std::make_shared<asset::model>(
        identity_pool, pages, size, size, size);

    left->fill(voxels::gray[8]);
    for (int32 x = 0; x < size; ++x) {
        for (int32 y = 0; y < size; ++y) {
            for (int32 z = 0; z < half; ++z) {
                right->set_voxel(x, y, z, voxels::gray[8]);
            }
        }
    }

    asset::chunk_volume left_chunk{left};
    left_chunk.set_boundary_slice(face_direction::pos_x, *right);
    REQUIRE(left_chunk.has_boundary_slice(face_direction::pos_x));

    gfx::mesh_generation_storage storage;
    const auto mesh = gfx::greedy_mesh_generator::generate_mesh_data(
        storage,
        gfx::mesh_source{
            .voxels   = left_chunk.voxels(),
            .boundary = left_chunk.share_boundary().get()
        },
        {.lod_step = 2});

    const auto outward = static_cast<uint8>(std::to_underlying(face_direction::pos_x));

    std::set<int32> open_z;
    for (const auto& cell : to_face_cells(mesh)) {
        if (cell.normal == outward) {
            open_z.insert(cell.z);
        }
    }

    REQUIRE(open_z.size() == half);
    REQUIRE(*open_z.begin() == half);
    REQUIRE(*open_z.rbegin() == size - 1);
}

TEST_CASE("a coarse roof stays one quad across the chunk seam", "[mesh][lod]") {
    asset::model_identity_pool identity_pool;
    asset::page_pool pages;

    constexpr int32 size    = 64;
    constexpr int32 surface = 30;

    const auto terrain = [&] {
        auto built = std::make_shared<asset::model>(identity_pool, pages, size, size, size);
        for (int32 x = 0; x < size; ++x) {
            for (int32 z = 0; z < size; ++z) {
                for (int32 y = 0; y <= surface; ++y) {
                    built->set_voxel(x, y, z, voxels::gray[8]);
                }
            }
        }
        return built;
    };

    const auto middle = terrain();
    const auto around = terrain();

    asset::chunk_volume chunk{middle};
    for (const face_direction face :
         {face_direction::pos_x, face_direction::neg_x, face_direction::pos_z,
          face_direction::neg_z}) {
        chunk.set_boundary_slice(face, *around);
    }

    gfx::mesh_generation_storage storage;
    const auto mesh = gfx::greedy_mesh_generator::generate_mesh_data(
        storage,
        gfx::mesh_source{
            .voxels   = chunk.voxels(),
            .boundary = chunk.share_boundary().get()
        },
        {.lod_step = 4}
    );

    const auto up = static_cast<uint8>(std::to_underlying(face_direction::pos_y));

    std::size_t roof_quads = 0;
    for (const auto& q : mesh.quads) {
        if (unpack_normal(q) != up) {
            continue;
        }
        ++roof_quads;
        REQUIRE(unpack_extents(q) == std::pair<int32, int32>{size, size});
    }

    REQUIRE(roof_quads == 1);
}

TEST_CASE("every coarse step costs fewer quads than the one before", "[mesh][lod]") {
    model_fixture fixture{64};

    uint32 state = 20250815;
    for (int32 x = 0; x < fixture.size(); ++x) {
        for (int32 z = 0; z < fixture.size(); ++z) {
            state = (state * 1664525U) + 1013904223U;
            const int32 height = 6 + static_cast<int32>((state >> 26) % 20);
            for (int32 y = 0; y < height; ++y) {
                fixture.get()->set_voxel(x, y, z, voxels::green[6]);
            }
        }
    }

    std::vector<std::size_t> counts;
    for (const int32 step : {1, 2, 4, 8}) {
        counts.push_back(fixture.greedy({.lod_step = step}).quads.size());
    }

    INFO(
        "quads by step: " << counts[0] << ", " << counts[1] << ", " << counts[2] << ", "
                          << counts[3]
    );

    REQUIRE(counts[0] == 11499);
    for (std::size_t i = 1; i < counts.size(); ++i) {
        REQUIRE(counts[i] * 2 < counts[i - 1]);
    }
}

TEST_CASE("a mesh reports the step it was actually built with", "[mesh][lod]") {
    SECTION("a chunk-sized model takes the step it was asked for") {
        model_fixture fixture{64};
        fixture.get()->set_voxel(10, 10, 10, voxels::gray[8]);

        for (const int32 step : {1, 2, 4, 8}) {
            const auto built = fixture.greedy({.lod_step = step});
            REQUIRE(built.lod_step == step);
            REQUIRE(gfx::mesh_key_of({.index = 7, .generation = 1}, built.lod_step).level ==
                    static_cast<uint32>(gfx::lod_level_of(step)));
        }
    }

    SECTION("a model the occupancy cannot describe stays at full detail") {
        model_fixture fixture{32};
        fixture.get()->set_voxel(10, 10, 10, voxels::gray[8]);

        const auto built = fixture.greedy({.lod_step = 4});
        REQUIRE(built.lod_step == 1);
        REQUIRE(built.quads.size() == fixture.greedy().quads.size());
    }
}

TEST_CASE("the mesh key separates levels and models", "[mesh][lod]") {
    const asset::model_identity a{.index = 3, .generation = 1};
    const asset::model_identity b{.index = 4, .generation = 1};

    REQUIRE(gfx::mesh_key_of(a, 1) == gfx::mesh_key_of(a, 1));
    REQUIRE_FALSE(gfx::mesh_key_of(a, 1) == gfx::mesh_key_of(a, 2));
    REQUIRE_FALSE(gfx::mesh_key_of(a, 1) == gfx::mesh_key_of(b, 1));

    std::unordered_map<gfx::mesh_key, int32> seen;
    for (const int32 step : {1, 2, 4, 8}) {
        seen[gfx::mesh_key_of(a, step)] = step;
        seen[gfx::mesh_key_of(b, step)] = step;
    }
    REQUIRE(seen.size() == 8);
}

TEST_CASE("a coarse chunk hides nothing a neighbour only half covers", "[mesh][lod]") {
    asset::model_identity_pool identity_pool;
    asset::page_pool pages;

    constexpr int32 size = 64;

    auto left  = std::make_shared<asset::model>(
        identity_pool, pages, size, size, size);
    auto right = std::make_shared<asset::model>(
        identity_pool, pages, size, size, size);

    left->fill(voxels::gray[8]);
    for (int32 x = 0; x < size; ++x) {
        for (int32 y = 0; y < size; y += 2) {
            for (int32 z = 0; z < size; ++z) {
                right->set_voxel(x, y, z, voxels::gray[8]);
            }
        }
    }

    asset::chunk_volume left_chunk{left};
    left_chunk.set_boundary_slice(face_direction::pos_x, *right);

    const auto outward = static_cast<uint8>(std::to_underlying(face_direction::pos_x));

    const auto outward_cells = [&](int32 step) -> std::size_t {
        gfx::mesh_generation_storage storage;
        const auto built = gfx::greedy_mesh_generator::generate_mesh_data(
            storage,
            gfx::mesh_source{
                .voxels   = left_chunk.voxels(),
                .boundary = left_chunk.share_boundary().get()
            },
            {.lod_step = step});

        std::size_t count = 0;
        for (const auto& cell : to_face_cells(built)) {
            if (cell.normal == outward) {
                ++count;
            }
        }
        return count;
    };

    REQUIRE(outward_cells(1) == static_cast<std::size_t>(size) * size / 2);
    REQUIRE(outward_cells(2) == static_cast<std::size_t>(size) * size);
}

TEST_CASE("the coarse side closes the step its own inflation made", "[mesh][lod]") {
    asset::model_identity_pool identity_pool;
    asset::page_pool pages;

    constexpr int32 size = 64;
    constexpr int32 top  = 40;

    auto coarse    = std::make_shared<asset::model>(
        identity_pool, pages, size, size, size);
    auto neighbour = std::make_shared<asset::model>(
        identity_pool, pages, size, size, size);

    for (auto& ground : {coarse, neighbour}) {
        for (int32 x = 0; x < size; ++x) {
            for (int32 y = 0; y <= top; ++y) {
                for (int32 z = 0; z < size; ++z) {
                    ground->set_voxel(x, y, z, voxels::green[6]);
                }
            }
        }
    }

    asset::chunk_volume chunk{coarse};
    chunk.set_boundary_slice(face_direction::pos_x, *neighbour);

    gfx::mesh_generation_storage storage;
    const auto built = gfx::greedy_mesh_generator::generate_mesh_data(
        storage,
        gfx::mesh_source{
            .voxels   = chunk.voxels(),
            .boundary = chunk.share_boundary().get()
        },
        {.lod_step = 2});

    const auto outward = static_cast<uint8>(std::to_underlying(face_direction::pos_x));

    std::set<int32> wall_y;
    for (const auto& cell : to_face_cells(built)) {
        if (cell.normal == outward) {
            wall_y.insert(cell.y);
        }
    }

    REQUIRE(wall_y.contains(top + 1));
    REQUIRE_FALSE(wall_y.contains(top - 1));
}

namespace {

auto coarse_solid(const asset::model& m, int32 step, vec3i cell) -> bool {
    for (int32 dx = 0; dx < step; ++dx) {
        for (int32 dy = 0; dy < step; ++dy) {
            for (int32 dz = 0; dz < step; ++dz) {
                if (!m.is_empty(
                        (cell.x * step) + dx, (cell.y * step) + dy, (cell.z * step) + dz
                    )) {
                    return true;
                }
            }
        }
    }
    return false;
}

auto expected_coarse_faces(const asset::model& m, int32 step) -> std::set<face_cell> {
    const int32 cells = m.width() / step;

    std::set<face_cell> want;
    for (int32 x = 0; x < cells; ++x) {
        for (int32 y = 0; y < cells; ++y) {
            for (int32 z = 0; z < cells; ++z) {
                const vec3i cell{x, y, z};
                if (!coarse_solid(m, step, cell)) {
                    continue;
                }

                for (uint8 dir = 0; dir < 6; ++dir) {
                    const vec3i at = cell + face_normal[dir];
                    const bool outside = at.x < 0 || at.y < 0 || at.z < 0 || at.x >= cells ||
                        at.y >= cells || at.z >= cells;
                    if (outside || !coarse_solid(m, step, at)) {
                        want.insert(face_cell{x, y, z, dir, 0});
                    }
                }
            }
        }
    }
    return want;
}

auto emitted_coarse_faces(const gfx::mesh& m, int32 step) -> std::set<face_cell> {
    std::set<face_cell> got;
    for (const auto& cell : to_face_cells(m)) {
        got.insert(face_cell{cell.x / step, cell.y / step, cell.z / step, cell.normal, 0});
    }
    return got;
}

}  // namespace

TEST_CASE("a coarse mesh has a face wherever the coarse grid needs one", "[mesh][lod]") {
    model_fixture fixture{64};

    uint32 state = 20250815;
    for (int32 x = 0; x < 64; ++x) {
        for (int32 z = 0; z < 64; ++z) {
            state = (state * 1664525U) + 1013904223U;
            const int32 height = 6 + static_cast<int32>((state >> 26) % 20);
            for (int32 y = 0; y < height; ++y) {
                fixture.get()->set_voxel(x, y, z, voxels::green[6]);
            }
        }
    }

    for (const int32 step : {1, 2, 4, 8}) {
        const auto built = fixture.greedy({.lod_step = step});
        const auto want  = expected_coarse_faces(*fixture.get(), step);
        const auto got   = emitted_coarse_faces(built, step);

        std::vector<face_cell> missing;
        std::ranges::set_difference(want, got, std::back_inserter(missing));

        INFO("step " << step << ": want " << want.size() << ", got " << got.size()
                     << ", missing " << missing.size());
        REQUIRE(missing.empty());
    }
}

TEST_CASE("the plane between two levels is closed from one side or the other", "[mesh][lod]") {
    asset::model_identity_pool identity_pool;
    asset::page_pool pages;

    constexpr int32 size = 64;

    auto left  = std::make_shared<asset::model>(
        identity_pool, pages, size, size, size);
    auto right = std::make_shared<asset::model>(
        identity_pool, pages, size, size, size);

    uint32 state = 991;
    const auto carve = [&](const std::shared_ptr<asset::model>& m) {
        for (int32 x = 0; x < size; ++x) {
            for (int32 z = 0; z < size; ++z) {
                state = (state * 1664525U) + 1013904223U;
                const int32 height = 10 + static_cast<int32>((state >> 26) % 24);
                for (int32 y = 0; y < height; ++y) {
                    m->set_voxel(x, y, z, voxels::green[6]);
                }
            }
        }
    };
    carve(left);
    carve(right);

    asset::chunk_volume left_chunk{left};
    asset::chunk_volume right_chunk{right};
    left_chunk.set_boundary_slice(face_direction::pos_x, *right);
    right_chunk.set_boundary_slice(face_direction::neg_x, *left);

    const auto build = [](asset::chunk_volume& c, int32 step) -> gfx::mesh {
        gfx::mesh_generation_storage storage;
        return gfx::greedy_mesh_generator::generate_mesh_data(
            storage,
            gfx::mesh_source{
                .voxels   = c.voxels(),
                .boundary = c.share_boundary().get()
            },
            {.lod_step = step});
    };

    for (const auto [left_step, right_step] :
         std::vector<std::pair<int32, int32>>{{1, 2}, {2, 1}, {2, 4}, {4, 8}, {1, 8}}) {
        const auto left_faces  = to_face_cells(build(left_chunk, left_step));
        const auto right_faces = to_face_cells(build(right_chunk, right_step));

        const auto outward = static_cast<uint8>(std::to_underlying(face_direction::pos_x));
        const auto inward  = static_cast<uint8>(std::to_underlying(face_direction::neg_x));

        std::set<std::pair<int32, int32>> closed;
        for (const auto& c : left_faces) {
            if (c.normal == outward && c.x == size - 1) {
                closed.insert({c.y, c.z});
            }
        }
        for (const auto& c : right_faces) {
            if (c.normal == inward && c.x == 0) {
                closed.insert({c.y, c.z});
            }
        }

        std::size_t gaps = 0;
        for (int32 y = 0; y < size; ++y) {
            for (int32 z = 0; z < size; ++z) {
                const bool a = coarse_solid(*left, left_step, {(size - 1) / left_step,
                                                              y / left_step, z / left_step});
                const bool b = coarse_solid(*right, right_step, {0, y / right_step,
                                                                 z / right_step});
                if (a != b && !closed.contains({y, z})) {
                    ++gaps;
                }
            }
        }

        INFO("steps " << left_step << " and " << right_step << ": " << gaps << " open cells");
        REQUIRE(gaps == 0);
    }
}

TEST_CASE("a coarse face lands on the cell boundary the shader draws it at", "[mesh][lod]") {
    model_fixture fixture{64};

    uint32 state = 4242;
    for (int32 x = 0; x < 64; ++x) {
        for (int32 z = 0; z < 64; ++z) {
            state = (state * 1664525U) + 1013904223U;
            const int32 height = 8 + static_cast<int32>((state >> 26) % 24);
            for (int32 y = 0; y < height; ++y) {
                fixture.get()->set_voxel(x, y, z, voxels::green[6]);
            }
        }
    }

    for (const int32 step : {1, 2, 4, 8}) {
        const auto built = fixture.greedy({.lod_step = step});

        for (const auto& q : built.quads) {
            const auto normal    = unpack_normal(q);
            const int32 axis     = normal / 2;
            const bool positive  = (normal % 2) == 0;
            const int32 min_at   = unpack_min(q)[axis];

            const int32 drawn_at = min_at + (positive ? 1 : 0);
            const int32 cell     = positive ? (drawn_at / step) - 1 : drawn_at / step;
            const int32 want     = positive ? (cell + 1) * step : cell * step;

            INFO("step " << step << " normal " << static_cast<int32>(normal) << " min "
                         << min_at << " drawn at " << drawn_at << " want " << want);
            REQUIRE(drawn_at == want);
            REQUIRE(drawn_at % step == 0);
        }
    }
}

namespace {

auto has_face(const gfx::mesh& m, vec3i cell, int32 face) -> bool {
    for (const auto& q : m.quads) {
        if (unpack_normal(q) != face) {
            continue;
        }
        const vec3i lo = unpack_min(q);
        const vec3i hi = unpack_max(q);
        if (cell.x >= lo.x && cell.x < hi.x && cell.y >= lo.y && cell.y < hi.y && cell.z >= lo.z &&
            cell.z < hi.z) {
            return true;
        }
    }
    return false;
}

auto grow_test_tree(asset::model& m) -> void {
    for (int32 x = 0; x < m.width(); ++x) {
        for (int32 z = 0; z < m.depth(); ++z) {
            m.set_voxel(x, 2, z, voxels::gray[10]);
        }
    }
    for (int32 y = 3; y < 12; ++y) {
        m.set_voxel(20, y, 20, voxels::bark[2]);
    }
    uint32 state = 77;
    for (int32 x = 14; x <= 26; ++x) {
        for (int32 y = 9; y <= 17; ++y) {
            for (int32 z = 14; z <= 26; ++z) {
                state           = (state * 1664525U) + 1013904223U;
                const int32 dx  = x - 20;
                const int32 dy  = y - 13;
                const int32 dz  = z - 20;
                const bool keep = (dx * dx) + (dy * dy) + (dz * dz) <= 30 && ((state >> 28) % 5) != 0;
                if (keep && m.is_empty(x, y, z)) {
                    m.set_voxel(x, y, z, voxels::leaves[(state >> 20) % voxels::leaves.count]);
                }
            }
        }
    }
}

}  // namespace

TEST_CASE("a quad carries the material of its voxel and an empty state code", "[mesh][material]") {
    const material_table& materials = default_material_table();

    model_fixture fixture{64};
    fixture.get()->set_voxel(3, 3, 3, voxels::gray[10]);
    fixture.get()->set_voxel(9, 3, 3, voxels::glow_blue);
    fixture.get()->set_voxel(15, 3, 3, voxels::leaves[2]);

    REQUIRE(materials.of(voxels::gray[10]) == material{});
    REQUIRE(materials.get(materials.of(voxels::glow_blue)).glow > 0);
    REQUIRE(materials.get(materials.of(voxels::leaves[2])).sways);
    REQUIRE(materials.of(voxels::glow_blue) != materials.of(voxels::leaves[2]));

    const auto check = [&materials](const gfx::mesh& m) -> void {
        REQUIRE(m.quads.size() == 18);
        for (const auto& q : m.quads) {
            const voxel v = voxel{static_cast<uint8>(unpack_slot(q))};
            REQUIRE(unpack_material(q) == materials.of(v));
            REQUIRE((q.data1 >> 23) == 0);
        }
    };

    check(fixture.simple());
    check(fixture.greedy());
}

TEST_CASE("a block of one leaf tone merges up to the wind lattice and no further", "[mesh][sway]") {
    model_fixture fixture{64};
    for (int32 x = 4; x < 20; ++x) {
        for (int32 y = 8; y < 24; ++y) {
            for (int32 z = 16; z < 32; ++z) {
                fixture.get()->set_voxel(x, y, z, voxels::leaves[2]);
            }
        }
    }

    const auto mesh = fixture.greedy();
    REQUIRE(mesh.quads.size() == 32);
    for (const auto& q : mesh.quads) {
        const vec3i lo = unpack_min(q);
        const vec3i hi = unpack_max(q);
        for (std::size_t axis = 0; axis < 3; ++axis) {
            REQUIRE(lo[axis] / 8 == (hi[axis] - 1) / 8);
        }
    }
}

TEST_CASE("every leaf quad sways, stays inside one lattice cell and carries nothing above the flag", "[mesh][sway]") {
    model_fixture fixture{64};
    grow_test_tree(*fixture.get());

    const auto check = [](const gfx::mesh& m) -> int32 {
        int32 swaying = 0;
        for (const auto& q : m.quads) {
            const voxel v = voxel{static_cast<uint8>(unpack_slot(q))};
            REQUIRE(unpack_sways(q) == voxels::leaves.contains(v));
            REQUIRE((q.data1 >> 23) == 0);
            if (unpack_sways(q)) {
                const vec3i lo = unpack_min(q);
                const vec3i hi = unpack_max(q);
                for (std::size_t axis = 0; axis < 3; ++axis) {
                    REQUIRE(lo[axis] / 8 == (hi[axis] - 1) / 8);
                }
            }
            swaying += unpack_sways(q) ? 1 : 0;
        }
        return swaying;
    };

    REQUIRE(check(fixture.simple()) > 100);
    REQUIRE(check(fixture.greedy()) > 50);
}

TEST_CASE("a leaf and the wood it touches both keep the face between them", "[mesh][sway]") {
    model_fixture fixture{64};
    fixture.get()->set_voxel(5, 5, 5, voxels::leaves[3]);
    fixture.get()->set_voxel(4, 5, 5, voxels::leaves[1]);
    fixture.get()->set_voxel(6, 5, 5, voxels::bark[2]);
    fixture.get()->set_voxel(7, 5, 5, voxels::bark[2]);
    fixture.get()->set_voxel(5, 4, 5, voxels::gray[10]);

    const auto check = [](const gfx::mesh& m) -> void {
        REQUIRE(has_face(m, {5, 5, 5}, 0));
        REQUIRE(has_face(m, {6, 5, 5}, 1));
        REQUIRE(has_face(m, {5, 5, 5}, 3));
        REQUIRE(has_face(m, {5, 4, 5}, 2));

        REQUIRE_FALSE(has_face(m, {4, 5, 5}, 0));
        REQUIRE_FALSE(has_face(m, {5, 5, 5}, 1));
        REQUIRE_FALSE(has_face(m, {6, 5, 5}, 0));
        REQUIRE_FALSE(has_face(m, {7, 5, 5}, 1));
    };

    check(fixture.simple());
    check(fixture.greedy());
}

TEST_CASE("a leaf on the chunk seam keeps its face against wood and drops it against leaves", "[mesh][sway]") {
    for (const bool wood : {true, false}) {
        INFO((wood ? "wood" : "leaf") << " across the seam");
        model_fixture left{64};
        model_fixture right{64};
        left.get()->set_voxel(63, 10, 10, voxels::leaves[3]);
        right.get()->set_voxel(0, 10, 10, wood ? voxels::bark[2] : voxels::leaves[3]);
        left.chunk().set_boundary_slice(face_direction::pos_x, *right.get());
        right.chunk().set_boundary_slice(face_direction::neg_x, *left.get());

        REQUIRE(has_face(left.greedy(), {63, 10, 10}, 0) == wood);
        REQUIRE(has_face(right.greedy(), {0, 10, 10}, 1) == wood);
    }
}

TEST_CASE("a coarse mesh neither sways nor opens faces between leaves and wood", "[mesh][sway][lod]") {
    model_fixture fixture{64};
    grow_test_tree(*fixture.get());

    const auto mesh = fixture.greedy({.lod_step = 2});
    REQUIRE_FALSE(mesh.quads.empty());
    for (const auto& q : mesh.quads) {
        REQUIRE_FALSE(unpack_sways(q));
    }
}
