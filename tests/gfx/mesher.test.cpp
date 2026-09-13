#include <catch2/catch_test_macros.hpp>

import std;

import vw.core;
import vw.asset;
import vw.ecs;
import vw.world;
import vw.gfx;

using namespace vw;

namespace {

// One unit face, so a greedy result can be compared against a per-voxel one:
// the two agree on which faces exist, never on how many quads they took to say
// it.
struct face_cell {
    int32 x = 0;
    int32 y = 0;
    int32 z = 0;
    uint8 normal = 0;
    uint16 slot  = 0;

    auto operator<=>(const face_cell&) const = default;
};

auto unpack_min(const gfx::quad& q) -> vec3i {
    return {
        static_cast<int32>(q.data0 & 0x7FU),
        static_cast<int32>((q.data0 >> 7) & 0x7FU),
        static_cast<int32>((q.data0 >> 14) & 0x7FU),
    };
}

// data1 carries the two tangent extents rather than the far corner; the third
// component is always one cell along the face axis. Same two tables the shaders
// and gfx::quad::pack use.
auto unpack_max(const gfx::quad& q) -> vec3i {
    constexpr int32 u_axis[6] = {2, 2, 0, 0, 0, 0};
    constexpr int32 v_axis[6] = {1, 1, 2, 2, 1, 1};

    const auto normal = static_cast<std::size_t>((q.data0 >> 21) & 0x7U);

    vec3i mx = unpack_min(q);
    mx[normal >> 1U] += 1;
    mx[u_axis[normal]] += static_cast<int32>(q.data1 & 0x7FU) + 1;
    mx[v_axis[normal]] += static_cast<int32>((q.data1 >> 7) & 0x7FU) + 1;
    return mx;
}

auto unpack_normal(const gfx::quad& q) -> uint8 {
    return static_cast<uint8>((q.data0 >> 21) & 0x7U);
}

// Слот палитры, а не идентификатор блока: в кваде едет плотный номер, который
// раздал реестр, и занимает он десять бит.
auto unpack_slot(const gfx::quad& q) -> uint16 {
    return static_cast<uint16>((q.data1 >> 14) & 0x3FFU);
}

// Two bits a corner, in winding order: 0 open, 3 shut in by two faces.
auto unpack_sky(const gfx::quad& q) -> std::array<uint8, 4> {
    return {
        static_cast<uint8>(q.data2 & 0xFU),
        static_cast<uint8>((q.data2 >> 4) & 0xFU),
        static_cast<uint8>((q.data2 >> 8) & 0xFU),
        static_cast<uint8>((q.data2 >> 12) & 0xFU),
    };
}

auto unpack_lamp(const gfx::quad& q) -> std::array<uint8, 4> {
    return {
        static_cast<uint8>((q.data2 >> 16) & 0xFU),
        static_cast<uint8>((q.data2 >> 20) & 0xFU),
        static_cast<uint8>((q.data2 >> 24) & 0xFU),
        static_cast<uint8>((q.data2 >> 28) & 0xFU),
    };
}

auto unpack_ao(const gfx::quad& q) -> std::array<uint8, 4> {
    const uint32 packed = (q.data0 >> 24) & 0xFFU;
    return {
        static_cast<uint8>(packed & 0x3U),
        static_cast<uint8>((packed >> 2) & 0x3U),
        static_cast<uint8>((packed >> 4) & 0x3U),
        static_cast<uint8>((packed >> 6) & 0x3U),
    };
}

auto unpack_convex(const gfx::quad& q) -> std::array<uint8, 4> {
    const uint32 packed = (q.data1 >> 24) & 0xFFU;
    return {
        static_cast<uint8>(packed & 0x3U),
        static_cast<uint8>((packed >> 2) & 0x3U),
        static_cast<uint8>((packed >> 4) & 0x3U),
        static_cast<uint8>((packed >> 6) & 0x3U),
    };
}

constexpr vec3i face_normal[6] = {
    {1, 0, 0}, {-1, 0, 0}, {0, 1, 0}, {0, -1, 0}, {0, 0, 1}, {0, 0, -1},
};

// Which end of the box each winding-order corner takes its components from.
// Copied from voxel.vert: the shader is what turns a rectangle into vertices,
// so this is the table that decides where a corner's occlusion lands on screen.
constexpr int32 face_verts[6][4][3] = {
    {{1, 0, 0}, {1, 0, 1}, {1, 1, 1}, {1, 1, 0}},
    {{0, 0, 0}, {0, 1, 0}, {0, 1, 1}, {0, 0, 1}},
    {{0, 1, 0}, {1, 1, 0}, {1, 1, 1}, {0, 1, 1}},
    {{0, 0, 0}, {0, 0, 1}, {1, 0, 1}, {1, 0, 0}},
    {{0, 0, 1}, {0, 1, 1}, {1, 1, 1}, {1, 0, 1}},
    {{1, 0, 0}, {1, 1, 0}, {0, 1, 0}, {0, 0, 0}},
};

// Worked out from the model and the face normal alone, never from the mesher's
// own idea of which way is u and which is v. That is the point of it: the rule
// below is the same one the mesher applies, but where it lands on the face is
// derived here from world coordinates, so a tangent table that is turned or
// mirrored for one face out of six shows up as a mismatch.
// The same geometry as expected_corner_ao, asking a different question: not
// how shut in the corner is, but how much sky the four cells around it get.
// Solid ones are left out of the average rather than counted as dark.
//
// The levels come from the flooded column rather than from the field on the
// model, so this checks the whole chain -- bake, boundary planes, mesher --
// against the thing all three are meant to reproduce.
auto expected_corner_light(
    const asset::model& mdl, const ecs::light_column& column, vec3i cell, int32 face,
    vec3i corner, asset::light_channel channel
) -> uint8 {
    constexpr int32 side = asset::light_field::side;

    const vec3i n     = face_normal[face];
    const vec3i front = cell + n;

    const int32 axis = (n.x != 0) ? 0 : ((n.y != 0) ? 1 : 2);
    const int32 a    = (axis + 1) % 3;
    const int32 b    = (axis + 2) % 3;

    const auto solid = [&mdl](vec3i p) {
        return p.x >= 0 && p.y >= 0 && p.z >= 0 && p.x < mdl.width() && p.y < mdl.height() &&
               p.z < mdl.depth() && !mdl.is_empty(p.x, p.y, p.z);
    };

    const auto level = [&column, channel](vec3i p) -> int32 {
        // A cell outside on two axes at once is at the very edge of the chunk,
        // and the field only keeps one plane a face -- so the mesher clamps the
        // other axes into it, first out of x, y, z wins. Pinned here rather
        // than glossed over: it is the one place the answer is approximate.
        const auto out = [](int32 v) { return v < 0 || v >= side; };

        if (out(p.x)) {
            p.y = std::clamp(p.y, 0, side - 1);
            p.z = std::clamp(p.z, 0, side - 1);
        } else if (out(p.y)) {
            p.z = std::clamp(p.z, 0, side - 1);
        }

        if (p.y < 0) {
            return 0;
        }
        if (p.y >= column.height()) {
            // Open sky above the column, and nothing else -- no lamp hangs
            // over the world, so the other channel reads dark up there.
            return channel == asset::light_channel::sky
                       ? int32{ecs::light_column::max_level}
                       : 0;
        }
        return column.level_at(p.x, p.y, p.z, channel);
    };

    const auto at = [&](int32 i, int32 j) {
        vec3i p = front;
        p[a]    = corner[a] + i;
        p[b]    = corner[b] + j;
        return p;
    };

    const int32 self_i  = front[a] - corner[a];
    const int32 self_j  = front[b] - corner[b];
    const int32 other_i = (self_i == 0) ? -1 : 0;
    const int32 other_j = (self_j == 0) ? -1 : 0;

    // The face's own front cell is air whenever the face is drawn, so the
    // average always has something in it.
    int32 sum   = level(at(self_i, self_j));
    int32 count = 1;

    for (const vec3i p : {at(other_i, self_j), at(self_i, other_j), at(other_i, other_j)}) {
        if (solid(p)) {
            continue;
        }
        sum += level(p);
        ++count;
    }

    return static_cast<uint8>(sum / count);
}

auto expected_corner_ao(
    const asset::model& mdl, vec3i cell, int32 face, vec3i corner
) -> uint8 {
    const vec3i n     = face_normal[face];
    const vec3i front = cell + n;

    const int32 axis = (n.x != 0) ? 0 : ((n.y != 0) ? 1 : 2);
    const int32 a    = (axis + 1) % 3;
    const int32 b    = (axis + 2) % 3;

    const auto solid = [&mdl](vec3i p) {
        if (p.x < 0 || p.y < 0 || p.z < 0) {
            return false;
        }
        if (p.x >= mdl.width() || p.y >= mdl.height() || p.z >= mdl.depth()) {
            return false;
        }
        return !mdl.is_empty(p.x, p.y, p.z);
    };

    // Cell (i, j) of the plane in front of the face, counted from the corner:
    // cell 0 is the one whose near edge starts at the corner, cell -1 the one
    // ending there.
    const auto at = [&](int32 i, int32 j) {
        vec3i p = front;
        p[a]    = corner[a] + i;
        p[b]    = corner[b] + j;
        return p;
    };

    // Where the face's own cell sits relative to the corner. The three cells
    // that matter are the two across an edge from it and the one diagonally
    // opposite; the fourth is the face's own, and it is empty whenever the face
    // is drawn at all.
    const int32 self_i  = front[a] - corner[a];
    const int32 self_j  = front[b] - corner[b];
    const int32 other_i = (self_i == 0) ? -1 : 0;
    const int32 other_j = (self_j == 0) ? -1 : 0;

    const bool edge_a   = solid(at(other_i, self_j));
    const bool edge_b   = solid(at(self_i, other_j));
    const bool diagonal = solid(at(other_i, other_j));

    if (edge_a && edge_b) {
        return 3;
    }
    return static_cast<uint8>(edge_a) + static_cast<uint8>(edge_b) +
           static_cast<uint8>(diagonal);
}

// The same three samples as expected_corner_ao, read in the layer the face's
// own cell stands in rather than the one in front of it, and counted for
// absence instead of presence. Zero on every face but the top one, which is the
// rule the mesher applies and the reason five faces out of six cost the greedy
// merge nothing.
auto expected_corner_convex(
    const asset::model& mdl, vec3i cell, int32 face, vec3i corner
) -> uint8 {
    if (face != 2) {
        return 0;
    }

    const vec3i n = face_normal[face];

    const int32 axis = (n.x != 0) ? 0 : ((n.y != 0) ? 1 : 2);
    const int32 a    = (axis + 1) % 3;
    const int32 b    = (axis + 2) % 3;

    // Outside the model is not open. With no boundary slice to ask, treating a
    // missing neighbour as absent would light a rim round the whole bounding
    // box -- which is the one place this rule differs from occlusion's.
    const auto open = [&mdl](vec3i p) {
        if (p.x < 0 || p.y < 0 || p.z < 0) {
            return false;
        }
        if (p.x >= mdl.width() || p.y >= mdl.height() || p.z >= mdl.depth()) {
            return false;
        }
        return mdl.is_empty(p.x, p.y, p.z);
    };

    const auto at = [&](int32 i, int32 j) {
        vec3i p = cell;
        p[a]    = corner[a] + i;
        p[b]    = corner[b] + j;
        return p;
    };

    const int32 self_i  = cell[a] - corner[a];
    const int32 self_j  = cell[b] - corner[b];
    const int32 other_i = (self_i == 0) ? -1 : 0;
    const int32 other_j = (self_j == 0) ? -1 : 0;

    const bool edge_a   = open(at(other_i, self_j));
    const bool edge_b   = open(at(self_i, other_j));
    const bool diagonal = open(at(other_i, other_j));

    if (edge_a && edge_b) {
        return 3;
    }
    return static_cast<uint8>(edge_a) + static_cast<uint8>(edge_b) +
           static_cast<uint8>(diagonal);
}

auto ao_levels(const gfx::mesh& m, uint8 normal) -> std::set<uint8> {
    std::set<uint8> levels;
    for (const auto& q : m.quads) {
        if (unpack_normal(q) != normal) {
            continue;
        }
        for (const uint8 value : unpack_ao(q)) {
            levels.insert(value);
        }
    }
    return levels;
}

// Every quad covers a rectangle of unit faces on one plane. Splitting it back
// into those units is what makes greedy and simple comparable.
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
        model_ = std::make_shared<asset::model>(identity_pool_, pages_, blocks::terrain::category, size, size, size);
        chunk_ = std::make_shared<asset::chunk_volume>(model_);
    }

    [[nodiscard]] auto get() const -> const std::shared_ptr<asset::model>& {
        return model_;
    }

    [[nodiscard]] auto chunk() const -> asset::chunk_volume& {
        return *chunk_;
    }

    [[nodiscard]] auto source() const -> gfx::mesh_source {
        return gfx::mesh_source{.voxels = *model_, .chunk = chunk_.get()};
    }

    [[nodiscard]] auto size() const -> int32 {
        return size_;
    }

    [[nodiscard]] auto greedy() -> gfx::mesh {
        gfx::mesh_generation_storage storage;
        return gfx::greedy_mesh_generator::generate_mesh_data(storage, source(), registry_);
    }

    [[nodiscard]] auto simple() const -> gfx::mesh {
        return gfx::simple_mesh_generator::generate_mesh_data(source(), registry_);
    }

private:
    int32 size_;
    asset::model_identity_pool identity_pool_;
    asset::page_pool pages_;
    block_registry registry_;
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
        fixture.get()->set_voxel(4, 5, 6, voxel{blocks::terrain::clay[1]});

        const auto greedy = fixture.greedy();
        REQUIRE(greedy.quads.size() == 6);
        REQUIRE(to_face_cells(greedy) == to_face_cells(fixture.simple()));
    }

    SECTION("voxel in the corner") {
        model_fixture fixture{16};
        fixture.get()->set_voxel(0, 0, 0, voxel{blocks::terrain::stone[1]});
        REQUIRE(to_face_cells(fixture.greedy()) == to_face_cells(fixture.simple()));
    }

    SECTION("solid block merges into six quads") {
        model_fixture fixture{16};
        fixture.get()->fill(voxel{blocks::terrain::grass[0]});

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
                        fixture.get()->set_voxel(x, y, z, voxel{blocks::terrain::ice[1]});
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
                    const auto block = (x < 8) ? blocks::terrain::dirt[2] : blocks::terrain::stone_deep[1];
                    fixture.get()->set_voxel(x, y, z, voxel{block});
                }
            }
        }
        REQUIRE(to_face_cells(fixture.greedy()) == to_face_cells(fixture.simple()));
    }

    // A 64-cube is the only size that takes the bit-occupancy path, so the
    // cases below are the ones that actually exercise it.
    SECTION("full-size chunk, terrain-like") {
        model_fixture fixture{64};
        uint32 state = 777;
        for (int32 x = 0; x < fixture.size(); ++x) {
            for (int32 z = 0; z < fixture.size(); ++z) {
                state = (state * 1664525U) + 1013904223U;
                const int32 height = 8 + static_cast<int32>((state >> 26) % 24);
                for (int32 y = 0; y < height; ++y) {
                    fixture.get()->set_voxel(x, y, z, voxel{blocks::terrain::sand[0]});
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
                            x, y, z, voxel{block_id{blocks::terrain::category, static_cast<uint8>(state % 40)}}
                        );
                    }
                }
            }
        }
        REQUIRE(to_face_cells(fixture.greedy()) == to_face_cells(fixture.simple()));
    }

    SECTION("full-size chunk, solid") {
        model_fixture fixture{64};
        fixture.get()->fill(voxel{blocks::terrain::stone[2]});

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
                        fixture.get()->set_voxel(x, y, z, voxel{block_id{blocks::terrain::category, static_cast<uint8>(state % 40)}});
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
    block_registry registry;

    // 64 so the bit path is the one under test: the +-X faces gather the
    // neighbour plane bit by bit, which the other four directions never do.
    constexpr int32 size = 64;
    auto left  = std::make_shared<asset::model>(identity_pool, pages, blocks::terrain::category, size, size, size);
    auto right = std::make_shared<asset::model>(identity_pool, pages, blocks::terrain::category, size, size, size);

    left->fill(voxel{blocks::terrain::stone[0]});
    right->fill(voxel{blocks::terrain::stone[0]});

    asset::chunk_volume left_chunk{left};

    const auto count_faces = [&registry](const asset::chunk_volume& c, uint8 normal) {
        gfx::mesh_generation_storage storage;
        const auto mesh = gfx::greedy_mesh_generator::generate_mesh_data(
            storage, gfx::mesh_source{.voxels = c.voxels(), .chunk = &c}, registry);

        std::size_t count = 0;
        for (const auto& cell : to_face_cells(mesh)) {
            if (cell.normal == normal) {
                ++count;
            }
        }
        return count;
    };

    // Face 0 is +X: without a neighbour the whole side is drawn, with one it
    // disappears, and the other five sides are untouched either way.
    REQUIRE(count_faces(left_chunk, 0) == size * size);
    const auto before_minus_x = count_faces(left_chunk, 1);

    left_chunk.set_boundary_slice(0, *right);

    REQUIRE(left_chunk.has_boundary_slice(0));
    REQUIRE(count_faces(left_chunk, 0) == 0);
    REQUIRE(count_faces(left_chunk, 1) == before_minus_x);
}

// The sampler grades occlusion from nothing to shut in, and the packing has to
// carry that grading through. It did not: the last step compared the value
// against zero, so a corner brushed by one diagonal block came out identical to
// the inside of a right angle, and the shading read as an outline rather than
// as depth. A digest would not have caught it -- it was stable and wrong.
TEST_CASE("ambient occlusion keeps all four levels", "[mesh]") {
    model_fixture fixture{16};

    for (int32 x = 0; x < 8; ++x) {
        for (int32 z = 0; z < 8; ++z) {
            fixture.get()->set_voxel(x, 4, z, voxel{blocks::terrain::stone[1]});
        }
    }

    SECTION("open floor is not occluded anywhere") {
        REQUIRE(ao_levels(fixture.simple(), 2) == std::set<uint8>{0});
    }

    SECTION("three blocks are enough to produce every level") {
        // Against the floor cell at (1, 4, 2) these are, in turn, a diagonal on
        // its own, an edge, and a second edge at right angles to the first --
        // which is one corner of each kind, plus the untouched ones.
        fixture.get()->set_voxel(2, 5, 1, voxel{blocks::terrain::clay[1]});
        fixture.get()->set_voxel(2, 5, 2, voxel{blocks::terrain::clay[1]});
        fixture.get()->set_voxel(1, 5, 3, voxel{blocks::terrain::clay[1]});

        REQUIRE(ao_levels(fixture.simple(), 2) == std::set<uint8>{0, 1, 2, 3});
        REQUIRE(ao_levels(fixture.greedy(), 2) == std::set<uint8>{0, 1, 2, 3});
    }
}

// A trench that turns a corner, checked for the one thing that reads as a bug
// rather than as shading: a point lighter than everything around it.
//
// A bend is where an occlusion term is most likely to produce one. A wider
// kernel did, at every turn, because its far diagonals were the only samples in
// range there and had been given no weight. Three samples cannot make that
// mistake -- a corner is dark exactly when something touches it -- but the
// invariant is worth keeping whatever the kernel becomes.
TEST_CASE("a bent trench has no point lighter than its surroundings", "[mesh]") {
    model_fixture fixture{32};

    for (int32 x = 0; x < 32; ++x) {
        for (int32 z = 0; z < 32; ++z) {
            for (int32 y = 0; y < 10; ++y) {
                fixture.get()->set_voxel(x, y, z, voxel{blocks::terrain::stone[1]});
            }
        }
    }

    const auto in_corridor = [](int32 x, int32 z) {
        const bool leg_a = x >= 10 && x < 13 && z >= 8 && z < 18;
        const bool leg_b = x >= 10 && x < 20 && z >= 15 && z < 18;
        return leg_a || leg_b;
    };

    for (int32 x = 0; x < 32; ++x) {
        for (int32 z = 0; z < 32; ++z) {
            if (!in_corridor(x, z)) {
                continue;
            }
            for (int32 y = 6; y < 10; ++y) {
                fixture.get()->set_voxel(x, y, z, voxel{});
            }
        }
    }

    std::map<std::pair<int32, int32>, uint8> lattice;
    for (const auto& q : fixture.simple().quads) {
        if (unpack_normal(q) != 2) {
            continue;
        }
        const auto lo = unpack_min(q);
        const auto hi = unpack_max(q);
        if (lo.y != 5) {
            continue;
        }
        const auto ao = unpack_ao(q);
        for (int32 slot = 0; slot < 4; ++slot) {
            const int32 cx = face_verts[2][slot][0] != 0 ? hi.x : lo.x;
            const int32 cz = face_verts[2][slot][2] != 0 ? hi.z : lo.z;
            lattice[{cx, cz}] = ao[slot];
        }
    }

    REQUIRE(lattice.size() > 60);

    for (const auto& [at, value] : lattice) {
        static constexpr std::array<std::pair<int32, int32>, 4> steps{
            std::pair{1, 0}, std::pair{-1, 0}, std::pair{0, 1}, std::pair{0, -1}
        };

        bool lighter_than_all = true;
        int32 neighbours      = 0;

        for (const auto& [dx, dz] : steps) {
            const auto it = lattice.find({at.first + dx, at.second + dz});
            if (it == lattice.end()) {
                continue;
            }
            ++neighbours;
            if (it->second <= value) {
                lighter_than_all = false;
            }
        }

        if (neighbours == 4 && lighter_than_all) {
            INFO("lattice " << at.first << "," << at.second << " reads " << int32{value}
                            << " against darker neighbours on all four sides");
            FAIL();
        }
    }
}

// How far a wall reaches, written down as a number rather than left to be
// rediscovered from a screenshot. One cell, and then nothing: the column beside
// the wall is shaded, the next one is open ground. That is why the middle of a
// trench three voxels wide reads exactly as bright as a field, and it is the
// known price of three samples -- a two-cell kernel does see across a trench
// three voxels wide, and charges ten percent of the quad count for it.
TEST_CASE("occlusion reaches exactly one cell from a wall", "[mesh]") {
    model_fixture fixture{16};

    for (int32 x = 0; x < 16; ++x) {
        for (int32 z = 0; z < 16; ++z) {
            fixture.get()->set_voxel(x, 4, z, voxel{blocks::terrain::stone[1]});
        }
    }

    // One voxel tall, running the length of the floor: everything the up-faces
    // sample lies in the plane just above them.
    for (int32 z = 0; z < 16; ++z) {
        fixture.get()->set_voxel(4, 5, z, voxel{blocks::terrain::clay[1]});
    }

    // Every up-face corner, gathered by how far along x its lattice point sits.
    // Away from the ends in z, where the wall stops and the picture changes.
    std::map<int32, std::set<uint8>> by_column;
    for (const auto& q : fixture.simple().quads) {
        const auto lo = unpack_min(q);
        const auto hi = unpack_max(q);
        const auto ao = unpack_ao(q);

        // The floor's own up-faces. The wall has one too, a level higher, and
        // nothing stands over it -- counting it in reads as the wall failing to
        // occlude itself.
        if (unpack_normal(q) != 2 || lo.y != 4) {
            continue;
        }

        for (int32 slot = 0; slot < 4; ++slot) {
            const int32 cx = face_verts[2][slot][0] != 0 ? hi.x : lo.x;
            const int32 cz = face_verts[2][slot][2] != 0 ? hi.z : lo.z;
            if (cz > 4 && cz < 12) {
                by_column[cx].insert(ao[slot]);
            }
        }
    }

    REQUIRE(by_column[5] == std::set<uint8>{2});
    REQUIRE(by_column[6] == std::set<uint8>{0});
    REQUIRE(by_column[7] == std::set<uint8>{0});
}

// Every corner of every rectangle, against occlusion worked out from the model
// directly. This is the check that a corner's value lands on the corner it was
// computed for: the mesher indexes corners by its own two tangents, the shader
// by winding order, and a table between them that is turned or mirrored for one
// face out of six produces shading on the wrong side of that face -- which
// reads as a bright seam where two faces meet and both went light.
TEST_CASE("packed occlusion matches the model at every corner", "[mesh]") {
    model_fixture fixture{16};

    // Away from the model's own walls, so nothing is decided by the absence of
    // a boundary slice, and dense enough to put every corner case somewhere.
    uint32 state = 7771;
    for (int32 x = 2; x < 14; ++x) {
        for (int32 y = 2; y < 14; ++y) {
            for (int32 z = 2; z < 14; ++z) {
                state = (state * 1664525U) + 1013904223U;
                if (((state >> 28) & 7U) < 4U) {
                    fixture.get()->set_voxel(x, y, z, voxel{blocks::terrain::stone[1]});
                }
            }
        }
    }

    const auto check = [&fixture](const gfx::mesh& m, std::string_view what) {
        std::size_t checked = 0;

        for (const auto& q : m.quads) {
            const int32 face = unpack_normal(q);
            const auto lo    = unpack_min(q);
            const auto hi    = unpack_max(q);
            const auto ao    = unpack_ao(q);

            for (int32 slot = 0; slot < 4; ++slot) {
                vec3i corner{};
                vec3i cell{};
                for (std::size_t i = 0; i < 3; ++i) {
                    const bool high = face_verts[face][slot][i] != 0;
                    corner[i]       = high ? hi[i] : lo[i];
                    cell[i]         = high ? hi[i] - 1 : lo[i];
                }

                const uint8 want = expected_corner_ao(*fixture.get(), cell, face, corner);
                if (ao[slot] != want) {
                    INFO(
                        what << ": face " << face << " cell " << cell.x << "," << cell.y << ","
                             << cell.z << " slot " << slot << " packed " << int32{ao[slot]}
                             << " expected " << int32{want}
                    );
                    FAIL();
                }
                ++checked;
            }
        }

        return checked;
    };

    REQUIRE(check(fixture.simple(), "simple") > 1000);
    REQUIRE(check(fixture.greedy(), "greedy") > 1000);
}

// Sky light all the way through: flood a column, bake it onto the model, mesh
// it, and check every corner of every quad against a plain recomputation. Both
// mesh generators are checked, which also pins the fast path -- greedy reads
// the corners out of occupancy bit rows, simple asks the voxels one at a time,
// and the two have to agree.
TEST_CASE("packed sky light matches the field at every corner", "[mesh]") {
    model_fixture fixture{64};
    auto& mdl = *fixture.get();

    // Rock with a shaft down into it and a tunnel off the bottom of the shaft.
    // The tunnel is what makes the test say anything: along it the light falls
    // a level a voxel, so most corners have four different numbers around them.
    asset::model_writer writer{mdl};

    for (int32 y = 0; y < 40; ++y) {
        for (int32 z = 0; z < 64; ++z) {
            for (int32 x = 0; x < 64; ++x) {
                writer.set(x, y, z, voxel{blocks::terrain::stone[1]});
            }
        }
    }
    for (int32 y = 10; y < 40; ++y) {
        for (int32 z = 30; z < 34; ++z) {
            for (int32 x = 30; x < 34; ++x) {
                writer.set(x, y, z, voxel{});
            }
        }
    }
    for (int32 y = 10; y < 14; ++y) {
        for (int32 z = 30; z < 34; ++z) {
            for (int32 x = 8; x < 34; ++x) {
                writer.set(x, y, z, voxel{});
            }
        }
    }

    // A pillar up to the ceiling of the chunk. Its top face reads the plane one
    // voxel outside, which here is open sky at 15 -- and reading it wrong is
    // what turned every outward face of every chunk black. Without something
    // touching the ceiling the test cannot tell.
    for (int32 y = 40; y < 64; ++y) {
        for (int32 z = 50; z < 54; ++z) {
            for (int32 x = 50; x < 54; ++x) {
                writer.set(x, y, z, voxel{blocks::terrain::stone[1]});
            }
        }
    }

    asset::chunk_occupancy occupancy;
    REQUIRE(mdl.build_occupancy(occupancy));

    const asset::chunk_occupancy* stack[1] = {&occupancy};
    const ecs::light_column column{
        std::span<const asset::chunk_occupancy* const>{stack, 1}
    };
    fixture.chunk().set_sky_light(column.bake(0, asset::light_channel::sky));

    const auto check = [&mdl, &column](const gfx::mesh& m, std::string_view what) {
        std::size_t checked = 0;
        std::set<uint8> levels;
        std::size_t lit_ceiling = 0;

        for (const auto& q : m.quads) {
            const int32 face = unpack_normal(q);
            const auto lo    = unpack_min(q);
            const auto hi    = unpack_max(q);
            const auto sky   = unpack_sky(q);

            for (int32 slot = 0; slot < 4; ++slot) {
                vec3i corner{};
                vec3i cell{};
                for (std::size_t i = 0; i < 3; ++i) {
                    const bool high = face_verts[face][slot][i] != 0;
                    corner[i]       = high ? hi[i] : lo[i];
                    cell[i]         = high ? hi[i] - 1 : lo[i];
                }

                const uint8 want = expected_corner_light(
                    mdl, column, cell, face, corner, asset::light_channel::sky
                );
                if (sky[slot] != want) {
                    INFO(
                        what << ": face " << face << " cell " << cell.x << "," << cell.y << ","
                             << cell.z << " slot " << slot << " packed " << int32{sky[slot]}
                             << " expected " << int32{want}
                    );
                    FAIL();
                }

                // Face 2 is +Y, and a top face on the ceiling row is the one
                // that has to reach outside the chunk to find its light.
                if (face == 2 && hi.y == 64 && sky[slot] == 15) {
                    ++lit_ceiling;
                }

                levels.insert(sky[slot]);
                ++checked;
            }
        }

        // Not a vacuous run: a field of one value would pass every comparison
        // above and mean nothing, and neither would one where the whole shell
        // came out dark.
        INFO(
            what << ": " << levels.size() << " distinct levels, " << lit_ceiling
                 << " lit ceiling corners"
        );
        REQUIRE(levels.size() > 4);
        REQUIRE(lit_ceiling > 0);

        return checked;
    };

    REQUIRE(check(fixture.greedy(), "greedy") > 1000);
    REQUIRE(check(fixture.simple(), "simple") > 1000);
}

// The other channel, the same way through. A lamp underground rather than a
// shaft to the surface, because block light has to be checked where sky light
// is flatly zero -- if the two ever bled into one another this is where it
// would show.
TEST_CASE("packed block light matches the field at every corner", "[mesh]") {
    model_fixture fixture{64};
    auto& mdl = *fixture.get();

    asset::model_writer writer{mdl};

    for (int32 y = 0; y < 64; ++y) {
        for (int32 z = 0; z < 64; ++z) {
            for (int32 x = 0; x < 64; ++x) {
                writer.set(x, y, z, voxel{blocks::terrain::stone[1]});
            }
        }
    }

    // A room with a corridor off it. The corridor is what makes the test say
    // anything: the light falls a level a voxel along it, so most corners have
    // four different numbers around them.
    for (int32 y = 20; y < 28; ++y) {
        for (int32 z = 20; z < 28; ++z) {
            for (int32 x = 20; x < 28; ++x) {
                writer.set(x, y, z, voxel{});
            }
        }
    }
    for (int32 y = 20; y < 24; ++y) {
        for (int32 z = 23; z < 25; ++z) {
            for (int32 x = 28; x < 50; ++x) {
                writer.set(x, y, z, voxel{});
            }
        }
    }

    writer.set(24, 20, 24, voxel{blocks::terrain::glowstone});
    writer.set(21, 26, 21, voxel{blocks::terrain::lava});

    asset::chunk_occupancy occupancy;
    REQUIRE(mdl.build_occupancy(occupancy));

    const asset::chunk_occupancy* stack[1] = {&occupancy};
    const asset::model* models[1]          = {&mdl};

    ecs::light_column::neighbourhood around{};
    around[4] = ecs::light_column::column_slice{
        .occupancy = std::span<const asset::chunk_occupancy* const>{stack, 1},
        .models    = std::span<const asset::model* const>{models, 1},
    };

    const ecs::light_column column{
        around, asset::build_emission_table(block_registry{}), {}
    };

    fixture.chunk().set_sky_light(column.bake(0, asset::light_channel::sky));
    fixture.chunk().set_block_light(column.bake(0, asset::light_channel::block));

    const auto check = [&mdl, &column](const gfx::mesh& m, std::string_view what) {
        std::size_t checked = 0;
        std::set<uint8> levels;

        for (const auto& q : m.quads) {
            const int32 face = unpack_normal(q);
            const auto lo    = unpack_min(q);
            const auto hi    = unpack_max(q);
            const auto lamp  = unpack_lamp(q);
            const auto sky   = unpack_sky(q);

            for (int32 slot = 0; slot < 4; ++slot) {
                vec3i corner{};
                vec3i cell{};
                for (std::size_t i = 0; i < 3; ++i) {
                    const bool high = face_verts[face][slot][i] != 0;
                    corner[i]       = high ? hi[i] : lo[i];
                    cell[i]         = high ? hi[i] - 1 : lo[i];
                }

                const uint8 want = expected_corner_light(
                    mdl, column, cell, face, corner, asset::light_channel::block
                );

                if (lamp[slot] != want) {
                    INFO(
                        what << ": face " << face << " cell " << cell.x << "," << cell.y << ","
                             << cell.z << " slot " << slot << " packed " << int32{lamp[slot]}
                             << " expected " << int32{want}
                    );
                    FAIL();
                }

                // The chunk is sealed rock, so nothing in it sees the sky. Any
                // sky level at all here would mean the two halves of data2 had
                // run into each other.
                if (sky[slot] != 0) {
                    INFO(what << ": sky leaked into a sealed chunk, " << int32{sky[slot]});
                    FAIL();
                }

                levels.insert(lamp[slot]);
                ++checked;
            }
        }

        // Not a vacuous run: a field of one value would pass every comparison
        // above and mean nothing.
        INFO(what << ": " << levels.size() << " distinct levels");
        REQUIRE(levels.size() > 4);
        REQUIRE(levels.contains(0));

        return checked;
    };

    REQUIRE(check(fixture.greedy(), "greedy") > 500);
    REQUIRE(check(fixture.simple(), "simple") > 500);
}

// Occlusion samples reach one cell past the face, so a face on the edge of a
// chunk asks its neighbour. If it did not, every chunk boundary would draw
// itself as a line of unoccluded ground across an otherwise shaded corner.
TEST_CASE("ambient occlusion reads across the chunk seam", "[mesh]") {
    // Full chunk size on both sides: a boundary slice is only taken between
    // models of exactly that side, and a smaller one is dropped in silence.
    model_fixture left{64};
    model_fixture right{64};

    for (int32 x = 0; x < 64; ++x) {
        for (int32 z = 0; z < 64; ++z) {
            left.get()->set_voxel(x, 4, z, voxel{blocks::terrain::stone[1]});
        }
    }

    // Just over the seam and one above the floor: from the last floor cell of
    // `left` this is the neighbour along +x, in the plane its up-face samples.
    right.get()->set_voxel(0, 5, 8, voxel{blocks::terrain::clay[1]});

    // The up-face of the last floor cell. Found rather than assumed: a lookup
    // that silently misses would return four zeroes, which is exactly the value
    // the unwired case is supposed to have.
    const auto seam_corners = [](const gfx::mesh& m) -> std::optional<std::array<uint8, 4>> {
        for (const auto& q : m.quads) {
            const auto lo = unpack_min(q);
            const auto hi = unpack_max(q);
            if (unpack_normal(q) != 2) {
                continue;
            }
            if (lo.x <= 63 && hi.x > 63 && lo.y == 4 && lo.z <= 8 && hi.z > 8) {
                return unpack_ao(q);
            }
        }
        return std::nullopt;
    };

    static constexpr std::array<uint8, 4> open{0, 0, 0, 0};

    const auto before = seam_corners(left.simple());
    REQUIRE(before.has_value());
    REQUIRE(*before == open);

    left.chunk().set_boundary_slice(0, *right.get());

    const auto after = seam_corners(left.simple());
    REQUIRE(after.has_value());
    REQUIRE(*after != open);

    const auto after_greedy = seam_corners(left.greedy());
    REQUIRE(after_greedy.has_value());
    REQUIRE(*after_greedy != open);
}

// Guards the shape of the output across refactors that are meant to preserve
// it: the value only changes when the mesher deliberately changes.
TEST_CASE("greedy meshing output is stable", "[mesh]") {
    model_fixture fixture{32};

    uint32 state = 999;
    for (int32 x = 0; x < fixture.size(); ++x) {
        for (int32 z = 0; z < fixture.size(); ++z) {
            state = (state * 1664525U) + 1013904223U;
            const int32 height = 4 + static_cast<int32>((state >> 27) % 8);
            for (int32 y = 0; y < height; ++y) {
                fixture.get()->set_voxel(x, y, z, voxel{blocks::terrain::grass[1]});
            }
        }
    }

    const auto mesh = fixture.greedy();
    REQUIRE(mesh.quads.size() > 0);

    const auto digest = hash_mesh(mesh);
    INFO("mesh digest: " << digest << ", quads: " << mesh.quads.size());
    REQUIRE(mesh.quads.size() == 5490);
    REQUIRE(digest == 3212416632622423288ULL);
}

// The 32-cube above never reaches the bit path, so ambient occlusion out of
// occupancy bits needs its own anchor: this digest covers the packed corner
// values, which the greedy-against-simple comparison does not look at.
TEST_CASE("full-size greedy meshing output is stable", "[mesh]") {
    model_fixture fixture{64};

    uint32 state = 20250815;
    for (int32 x = 0; x < fixture.size(); ++x) {
        for (int32 z = 0; z < fixture.size(); ++z) {
            state = (state * 1664525U) + 1013904223U;
            const int32 height = 6 + static_cast<int32>((state >> 26) % 20);
            for (int32 y = 0; y < height; ++y) {
                fixture.get()->set_voxel(x, y, z, voxel{blocks::terrain::grass[1]});
            }
        }
    }

    const auto mesh = fixture.greedy();
    const auto digest = hash_mesh(mesh);
    INFO("full-size digest: " << digest << ", quads: " << mesh.quads.size());
    REQUIRE(mesh.quads.size() == 29276);
    REQUIRE(digest == 5005360969942537598ULL);
}


// Convexity all the way through: the same shape of check as occlusion, over
// both mesh generators, which pins the fast path against the slow one -- greedy
// reads the corners out of occupancy bit rows, simple asks the voxels one at a
// time. It also pins the winding reorder: convexity has to travel the same
// shuffle out of sampler order that occlusion and sky light do, or the shader
// bilinearly blends one rectangle for two of them and another for the third.
TEST_CASE("packed convexity matches the model at every corner", "[mesh]") {
    model_fixture fixture{16};

    // Away from the model's own walls, so nothing is decided by the absence of
    // a boundary slice, and a height field rather than noise: convexity lives
    // on the steps of a surface, and a cloud of loose voxels has few.
    uint32 state = 4127;
    for (int32 x = 2; x < 14; ++x) {
        for (int32 z = 2; z < 14; ++z) {
            state = (state * 1664525U) + 1013904223U;
            const int32 height = 3 + static_cast<int32>((state >> 28) % 6);
            for (int32 y = 2; y < 2 + height; ++y) {
                fixture.get()->set_voxel(x, y, z, voxel{blocks::terrain::stone[1]});
            }
        }
    }

    const auto check = [&fixture](const gfx::mesh& m, std::string_view what) {
        std::size_t spikes = 0;

        for (const auto& q : m.quads) {
            const int32 face  = unpack_normal(q);
            const auto lo     = unpack_min(q);
            const auto hi     = unpack_max(q);
            const auto convex = unpack_convex(q);

            for (int32 slot = 0; slot < 4; ++slot) {
                vec3i corner{};
                vec3i cell{};
                for (std::size_t i = 0; i < 3; ++i) {
                    const bool high = face_verts[face][slot][i] != 0;
                    corner[i]       = high ? hi[i] : lo[i];
                    cell[i]         = high ? hi[i] - 1 : lo[i];
                }

                const uint8 want = expected_corner_convex(*fixture.get(), cell, face, corner);
                if (convex[slot] != want) {
                    INFO(
                        what << ": face " << face << " cell " << cell.x << "," << cell.y << ","
                             << cell.z << " slot " << slot << " packed " << int32{convex[slot]}
                             << " expected " << int32{want}
                    );
                    FAIL();
                }
                if (want == 3) {
                    ++spikes;
                }
            }
        }

        return spikes;
    };

    // A count, not a smoke test: a surface of random steps has outside corners
    // all over it, and a check that only ever compared zero against zero would
    // pass with the whole term deleted.
    REQUIRE(check(fixture.simple(), "simple") > 50);
    REQUIRE(check(fixture.greedy(), "greedy") > 50);
}

// Convexity is the one sampler whose out-of-range default is not occlusion's.
// A model with no boundary slices has to come out flush all round its own
// bounding box, not ringed in light.
TEST_CASE("a model without neighbours has no rim", "[mesh]") {
    model_fixture fixture{16};

    for (int32 x = 0; x < 16; ++x) {
        for (int32 z = 0; z < 16; ++z) {
            for (int32 y = 0; y < 8; ++y) {
                fixture.get()->set_voxel(x, y, z, voxel{blocks::terrain::stone[1]});
            }
        }
    }

    std::size_t tops = 0;
    for (const auto& q : fixture.greedy().quads) {
        if (unpack_normal(q) != 2) {
            continue;
        }
        ++tops;
        for (const uint8 value : unpack_convex(q)) {
            REQUIRE(value == 0);
        }
    }

    REQUIRE(tops > 0);
}
