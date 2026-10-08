export module vw.asset:model.links;

import std;

import vw.core;
import :model.identity;
import :model.occupancy;

export namespace vw::asset {

struct cell_links {
    // см. docs/world.md#карман
    static constexpr std::size_t max_pockets = 63;

    std::vector<chunk_pocket> pockets;
    bool merged = false;

    [[nodiscard]] auto is_sealed() const -> bool {
        return pockets.empty();
    }
};

struct chunk_links {
    static constexpr int32 cell_size      = 32;
    static constexpr int32 cells_per_side = chunk_occupancy::side / cell_size;
    static constexpr int32 cell_count = cells_per_side * cells_per_side * cells_per_side;

    std::array<cell_links, cell_count> cells;

    [[nodiscard]] static constexpr auto cell_index(int32 x, int32 y, int32 z) -> int32 {
        return (((y * cells_per_side) + z) * cells_per_side) + x;
    }

    [[nodiscard]] auto is_sealed() const -> bool {
        return std::ranges::all_of(cells, [](const cell_links& c) -> bool {
            return c.is_sealed();
        });
    }
};

struct chunk_link_scratch {
    std::vector<uint64> masks;
    std::vector<int32> row_begin;
    std::vector<uint8> seen;
    std::vector<int32> stack;
};

[[nodiscard]] auto build_chunk_links(
    const chunk_occupancy& occupancy, chunk_link_scratch& scratch
) -> chunk_links;

[[nodiscard]] auto build_chunk_links(const chunk_occupancy& occupancy) -> chunk_links;



}  // namespace vw::asset
