export module vw.asset:model.occupancy;

import std;

import vw.core;
import :model.identity;

export namespace vw::asset {

struct face_occupancy {
    static constexpr int32 side = 64;

    std::array<uint64, side> rows{};

    [[nodiscard]] auto test(int32 a, int32 b) const -> bool {
        return ((rows[b] >> a) & 1U) != 0;
    }

    auto set(int32 a, int32 b) -> void {
        rows[b] |= uint64{1} << a;
    }

    auto clear() -> void {
        rows.fill(0);
    }
};

// см. docs/lod-plan.md#у-мешера-двадцать-шесть-соседей
struct model_boundary {
    per_face<face_occupancy> faces{};
    std::array<uint64, shell_edge_count> edges{};
    uint8 corners = 0;

    per_face<face_occupancy> leaf_faces{};
    std::array<uint64, shell_edge_count> leaf_edges{};
    uint8 leaf_corners = 0;

    uint8 valid         = 0;
    uint16 edges_valid  = 0;
    uint8 corners_valid = 0;

    [[nodiscard]] auto has_edge(vec3i step) const -> bool {
        return (edges_valid & (1U << shell_edge_index(step))) != 0;
    }

    [[nodiscard]] auto edge_holds(vec3i step, int32 along) const -> bool {
        return ((edges[static_cast<std::size_t>(shell_edge_index(step))] >> along) & 1U) != 0;
    }

    [[nodiscard]] auto has_corner(vec3i step) const -> bool {
        return (corners_valid & (1U << shell_corner_index(step))) != 0;
    }

    [[nodiscard]] auto corner_holds(vec3i step) const -> bool {
        return ((corners >> shell_corner_index(step)) & 1U) != 0;
    }

    [[nodiscard]] auto edge_holds_leaf(vec3i step, int32 along) const -> bool {
        return ((leaf_edges[static_cast<std::size_t>(shell_edge_index(step))] >> along) & 1U) != 0;
    }

    [[nodiscard]] auto corner_holds_leaf(vec3i step) const -> bool {
        return ((leaf_corners >> shell_corner_index(step)) & 1U) != 0;
    }
};

struct chunk_occupancy {
    static constexpr int32 side = 64;

    std::array<uint64, side * side> rows{};
    std::array<uint64, side * side> zrows{};

    auto clear() -> void {
        rows.fill(0);
        zrows.fill(0);
    }

    [[nodiscard]] auto row(int32 y, int32 z) const -> uint64 {
        return rows[(y * side) + z];
    }

    [[nodiscard]] auto zrow(int32 y, int32 x) const -> uint64 {
        return zrows[(y * side) + x];
    }

    [[nodiscard]] auto test(int32 x, int32 y, int32 z) const -> bool {
        return ((row(y, z) >> x) & 1U) != 0;
    }

    auto set_row(int32 y, int32 z, uint64 bits) -> void {
        rows[(y * side) + z] |= bits;
    }

    auto set_zrow(int32 y, int32 x, uint64 bits) -> void {
        zrows[(y * side) + x] |= bits;
    }
};

[[nodiscard]] auto occupancy_brick_count(int32 level) -> std::size_t;

// см. docs/rendering.md#занятость-на-gpu
auto pack_occupancy_bricks(const chunk_occupancy& solid, int32 level, std::span<uint8> out)
    -> void;

struct chunk_pocket {
    static constexpr int32 face_span = 8;

    per_face<uint64> faces{};

    static constexpr int32 volume_span = 4;
    uint64 volume = 0;

    [[nodiscard]] static constexpr auto volume_bit(int32 x, int32 y, int32 z, int32 block)
        -> uint64 {
        return uint64{1}
            << ((((y / block) * volume_span) + (z / block)) * volume_span + (x / block));
    }

    [[nodiscard]] auto holds(int32 x, int32 y, int32 z, int32 block) const -> bool {
        return (volume & volume_bit(x, y, z, block)) != 0;
    }

    [[nodiscard]] auto touches(face_direction face) const -> bool {
        return faces[face] != 0;
    }

    [[nodiscard]] auto meets(const chunk_pocket& other, face_direction face) const -> bool {
        return (faces[face] & other.faces[opposite(face)]) != 0;
    }

    [[nodiscard]] static auto wide_open() -> chunk_pocket {
        chunk_pocket pocket;
        std::ranges::fill(pocket.faces, ~uint64{0});
        return pocket;
    }
};

}  // namespace vw::asset
