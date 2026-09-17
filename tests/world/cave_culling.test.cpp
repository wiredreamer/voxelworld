#include <catch2/catch_test_macros.hpp>

import std;

import vw.core;
import vw.asset;
import vw.ecs;
import vw.world;

using namespace vw;
using namespace vw::ecs;

namespace {

constexpr int32 chunk_side = asset::chunk_occupancy::side;

class occupancy_world {
public:
    occupancy_world(int32 columns, perlin_terrain_generator& gen) {
        for (int32 cx = 0; cx < columns; ++cx) {
            for (int32 cz = 0; cz < columns; ++cz) {
                ecs::gen_column column{cx, cz};
                terrain_context ctx{
                    .cx           = cx,
                    .cz           = cz,
                    .create_chunk = [&column](int32 y) -> chunk_data& {
                        return column.create_chunk(y, chunk_data{});
                    },
                };
                gen.generate(ctx);

                for (auto& [cy, data] : column.get_all_chunk_data()) {
                    asset::chunk_occupancy occupancy;
                    if (!data.volume->voxels().build_occupancy(occupancy)) {
                        continue;
                    }
                    chunks_.emplace(vec3i{cx, cy, cz}, occupancy);

                    lo_ = vec3i{std::min(lo_.x, cx), std::min(lo_.y, cy), std::min(lo_.z, cz)};
                    hi_ = vec3i{std::max(hi_.x, cx), std::max(hi_.y, cy), std::max(hi_.z, cz)};
                }
            }
        }
    }

    [[nodiscard]] auto occupancy_at(vec3i coord) const -> const asset::chunk_occupancy* {
        const auto it = chunks_.find(coord);
        return it == chunks_.end() ? nullptr : &it->second;
    }

    [[nodiscard]] auto count() const -> std::size_t { return chunks_.size(); }
    [[nodiscard]] auto lo() const -> vec3i { return lo_; }
    [[nodiscard]] auto hi() const -> vec3i { return hi_; }

    [[nodiscard]] auto coords() const {
        return chunks_ | std::views::keys;
    }

private:
    std::unordered_map<vec3i, asset::chunk_occupancy> chunks_;
    vec3i lo_{std::numeric_limits<int32>::max(), std::numeric_limits<int32>::max(),
              std::numeric_limits<int32>::max()};
    vec3i hi_{std::numeric_limits<int32>::lowest(), std::numeric_limits<int32>::lowest(),
              std::numeric_limits<int32>::lowest()};
};

auto sub_links(
    const asset::chunk_occupancy& occupancy, vec3i origin, int32 size
) -> asset::cell_links {
    const auto solid = [&](int32 x, int32 y, int32 z) -> bool {
        return occupancy.test(origin.x + x, origin.y + y, origin.z + z);
    };

    std::vector<uint8> seen(static_cast<std::size_t>(size) * size * size, 0);
    const auto at = [size](int32 x, int32 y, int32 z) -> std::size_t {
        return (static_cast<std::size_t>(z) * size + y) * size + x;
    };

    asset::cell_links links;
    std::vector<vec3i> stack;

    const int32 block = std::max(1, size / asset::chunk_pocket::face_span);
    const auto block_bit = [block](int32 a, int32 b) -> uint64 {
        return uint64{1}
            << (((b / block) * asset::chunk_pocket::face_span) + (a / block));
    };

    for (int32 z = 0; z < size; ++z) {
        for (int32 y = 0; y < size; ++y) {
            for (int32 x = 0; x < size; ++x) {
                if (seen[at(x, y, z)] != 0 || solid(x, y, z)) {
                    continue;
                }

                asset::chunk_pocket pocket;
                stack.push_back({x, y, z});
                seen[at(x, y, z)] = 1;

                while (!stack.empty()) {
                    const auto p = stack.back();
                    stack.pop_back();

                    if (p.x == 0) pocket.faces[0] |= block_bit(p.y, p.z);
                    if (p.x == size - 1) pocket.faces[1] |= block_bit(p.y, p.z);
                    if (p.y == 0) pocket.faces[2] |= block_bit(p.x, p.z);
                    if (p.y == size - 1) pocket.faces[3] |= block_bit(p.x, p.z);
                    if (p.z == 0) pocket.faces[4] |= block_bit(p.x, p.y);
                    if (p.z == size - 1) pocket.faces[5] |= block_bit(p.x, p.y);
                    pocket.volume |= asset::chunk_pocket::volume_bit(
                        p.x, p.y, p.z, std::max(1, size / asset::chunk_pocket::volume_span)
                    );

                    for (const auto& d : ecs::chunk_face_offsets) {
                        const vec3i n{p.x + d.x, p.y + d.y, p.z + d.z};
                        if (n.x < 0 || n.y < 0 || n.z < 0 || n.x >= size || n.y >= size ||
                            n.z >= size) {
                            continue;
                        }
                        if (seen[at(n.x, n.y, n.z)] != 0 || solid(n.x, n.y, n.z)) {
                            continue;
                        }
                        seen[at(n.x, n.y, n.z)] = 1;
                        stack.push_back(n);
                    }
                }

                if (std::ranges::any_of(
                        pocket.faces, [](uint64 blocks) -> bool { return blocks != 0; }
                    )) {
                    links.pockets.push_back(pocket);
                }
            }
        }
    }

    return links;
}

struct culling_result {
    std::size_t chunks       = 0;
    std::size_t visible      = 0;
    std::size_t cells_walked = 0;
};

auto measure_culling(const occupancy_world& world, int32 cell_size) -> culling_result {
    const int32 per_chunk = chunk_side / cell_size;

    std::unordered_map<vec3i, asset::cell_links> cells;
    for (const auto& coord : world.coords()) {
        const auto* occupancy = world.occupancy_at(coord);
        for (int32 x = 0; x < per_chunk; ++x) {
            for (int32 y = 0; y < per_chunk; ++y) {
                for (int32 z = 0; z < per_chunk; ++z) {
                    const vec3i cell{
                        (coord.x * per_chunk) + x,
                        (coord.y * per_chunk) + y,
                        (coord.z * per_chunk) + z,
                    };
                    cells.emplace(
                        cell,
                        sub_links(
                            *occupancy, vec3i{x * cell_size, y * cell_size, z * cell_size},
                            cell_size
                        )
                    );
                }
            }
        }
    }

    const vec3i lo{
        world.lo().x * per_chunk, world.lo().y * per_chunk, world.lo().z * per_chunk};
    const vec3i hi{
        ((world.hi().x + 1) * per_chunk) - 1,
        ((world.hi().y + 1) * per_chunk) - 1 + per_chunk,
        ((world.hi().z + 1) * per_chunk) - 1,
    };

    const vec3i origin{
        (world.lo().x + world.hi().x) / 2 * per_chunk + (per_chunk / 2),
        hi.y,
        (world.lo().z + world.hi().z) / 2 * per_chunk + (per_chunk / 2),
    };

    std::unordered_map<vec2i, int32> top_of;
    for (const auto& [cell, links] : cells) {
        const vec2i column{cell.x, cell.z};
        const auto it = top_of.find(column);
        if (it == top_of.end() || it->second < cell.y) {
            top_of[column] = cell.y;
        }
    }

    std::set<std::tuple<int32, int32, int32>> visible_chunks;
    culling_result result;
    result.chunks = world.count();

    ecs::walk_visible_chunks(
        origin, lo, hi,
        [&cells](vec3i cell) -> const asset::cell_links* {
            const auto it = cells.find(cell);
            return it == cells.end() ? nullptr : &it->second;
        },
        [&top_of](vec3i cell) -> bool {
            const auto it = top_of.find(vec2i{cell.x, cell.z});
            return it == top_of.end() || cell.y > it->second;
        },
        [](const asset::chunk_pocket&) -> bool {
            return true;
        },
        [&](vec3i cell) {
            ++result.cells_walked;
            const auto floor_div = [](int32 a, int32 b) -> int32 {
                return a >= 0 ? a / b : (a - b + 1) / b;
            };
            const vec3i chunk_coord{
                floor_div(cell.x, per_chunk), floor_div(cell.y, per_chunk),
                floor_div(cell.z, per_chunk)};
            if (world.occupancy_at(chunk_coord) != nullptr) {
                visible_chunks.insert({chunk_coord.x, chunk_coord.y, chunk_coord.z});
            }
        }
    );

    result.visible = visible_chunks.size();
    return result;
}

}  // namespace

TEST_CASE("cave culling resolution sweep", "[world][culling][.sweep]") {
    asset::model_identity_pool identity_pool;
    asset::page_pool pages;

    perlin_terrain_generator gen{identity_pool, pages, perlin_terrain_generator::params{}};
    const occupancy_world world{16, gen};

    for (const int32 cell_size : {32, 16}) {
        const auto result = measure_culling(world, cell_size);

        std::println(
            "cell {:>2}: {} of {} chunks visible ({:.1f}% hidden), {} cells walked",
            cell_size,
            result.visible,
            result.chunks,
            result.chunks > 0
                ? 100.0 * static_cast<float64>(result.chunks - result.visible) /
                      static_cast<float64>(result.chunks)
                : 0.0,
            result.cells_walked
        );
    }
}

TEST_CASE("how much of the cave system the sky can reach", "[world][culling][.sweep]") {
    asset::model_identity_pool identity_pool;
    asset::page_pool pages;

    perlin_terrain_generator gen{identity_pool, pages, perlin_terrain_generator::params{}};
    const occupancy_world world{4, gen};

    const vec3i lo = world.lo();
    const vec3i hi = world.hi();

    const int32 size_x = (hi.x - lo.x + 1) * chunk_side;
    const int32 size_y = (hi.y - lo.y + 1) * chunk_side;
    const int32 size_z = (hi.z - lo.z + 1) * chunk_side;

    const auto index = [&](int32 x, int32 y, int32 z) -> std::size_t {
        return (static_cast<std::size_t>(y) * size_z + z) * size_x + x;
    };

    std::vector<uint8> solid(
        static_cast<std::size_t>(size_x) * size_y * size_z, 0
    );

    for (const auto& coord : world.coords()) {
        const auto* occupancy = world.occupancy_at(coord);
        for (int32 x = 0; x < chunk_side; ++x) {
            for (int32 y = 0; y < chunk_side; ++y) {
                for (int32 z = 0; z < chunk_side; ++z) {
                    if (!occupancy->test(x, y, z)) {
                        continue;
                    }
                    solid[index(
                        ((coord.x - lo.x) * chunk_side) + x, ((coord.y - lo.y) * chunk_side) + y,
                        ((coord.z - lo.z) * chunk_side) + z
                    )] = 1;
                }
            }
        }
    }

    std::vector<uint8> underground(solid.size(), 0);
    std::size_t underground_air = 0;

    for (int32 x = 0; x < size_x; ++x) {
        for (int32 z = 0; z < size_z; ++z) {
            int32 top = -1;
            for (int32 y = size_y - 1; y >= 0; --y) {
                if (solid[index(x, y, z)] != 0) {
                    top = y;
                    break;
                }
            }
            if (top < 0) {
                continue;
            }
            for (int32 y = 0; y < top; ++y) {
                if (solid[index(x, y, z)] == 0) {
                    underground[index(x, y, z)] = 1;
                    ++underground_air;
                }
            }
        }
    }

    std::vector<uint8> reached(solid.size(), 0);
    std::vector<vec3i> stack;

    for (int32 x = 0; x < size_x; ++x) {
        for (int32 z = 0; z < size_z; ++z) {
            const int32 y = size_y - 1;
            if (solid[index(x, y, z)] == 0 && reached[index(x, y, z)] == 0) {
                reached[index(x, y, z)] = 1;
                stack.push_back({x, y, z});
            }
        }
    }

    std::size_t reached_underground = 0;
    while (!stack.empty()) {
        const auto p = stack.back();
        stack.pop_back();

        if (underground[index(p.x, p.y, p.z)] != 0) {
            ++reached_underground;
        }

        for (const auto& d : ecs::chunk_face_offsets) {
            const vec3i n{p.x + d.x, p.y + d.y, p.z + d.z};
            if (n.x < 0 || n.y < 0 || n.z < 0 || n.x >= size_x || n.y >= size_y ||
                n.z >= size_z) {
                continue;
            }
            if (solid[index(n.x, n.y, n.z)] != 0 || reached[index(n.x, n.y, n.z)] != 0) {
                continue;
            }
            reached[index(n.x, n.y, n.z)] = 1;
            stack.push_back(n);
        }
    }

    std::println(
        "underground air {}, reachable from the sky {} ({:.1f}%)",
        underground_air,
        reached_underground,
        underground_air > 0 ? 100.0 * static_cast<float64>(reached_underground) /
                                  static_cast<float64>(underground_air)
                            : 0.0
    );
}

TEST_CASE("engine pockets agree with a voxel flood fill", "[world][culling][.sweep]") {
    asset::model_identity_pool identity_pool;
    asset::page_pool pages;

    perlin_terrain_generator gen{identity_pool, pages, perlin_terrain_generator::params{}};
    const occupancy_world world{2, gen};

    constexpr int32 cell_size = asset::chunk_links::cell_size;
    constexpr int32 per_side  = asset::chunk_links::cells_per_side;

    std::size_t compared = 0;
    std::size_t pocket_count_mismatch = 0;
    std::size_t face_mismatch = 0;

    for (const auto& coord : world.coords()) {
        const auto* occupancy = world.occupancy_at(coord);
        const auto engine     = asset::build_chunk_links(*occupancy);

        for (int32 x = 0; x < per_side; ++x) {
            for (int32 y = 0; y < per_side; ++y) {
                for (int32 z = 0; z < per_side; ++z) {
                    const auto reference = sub_links(
                        *occupancy, vec3i{x * cell_size, y * cell_size, z * cell_size}, cell_size
                    );
                    const auto& actual =
                        engine.cells[asset::chunk_links::cell_index(x, y, z)];

                    ++compared;
                    if (reference.pockets.size() != actual.pockets.size()) {
                        ++pocket_count_mismatch;
                        continue;
                    }

                    for (int32 face = 0; face < asset::chunk_pocket::face_count; ++face) {
                        uint64 a = 0;
                        uint64 b = 0;
                        for (const auto& pocket : reference.pockets) a |= pocket.faces[face];
                        for (const auto& pocket : actual.pockets) b |= pocket.faces[face];
                        if (a != b) {
                            ++face_mismatch;
                            break;
                        }
                    }
                }
            }
        }
    }

    std::println(
        "compared {} cells: {} differ in pocket count, {} in face openings",
        compared, pocket_count_mismatch, face_mismatch
    );

    REQUIRE(pocket_count_mismatch == 0);
    REQUIRE(face_mismatch == 0);
}
